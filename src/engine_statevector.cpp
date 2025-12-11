#include "engine_statevector.hpp"

#include "noise.hpp"
#include "progress_reporter.hpp"

#include <algorithm>
#include <cstdlib>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>
#ifdef NA_VM_WITH_ONEAPI
#include <sycl/sycl.hpp>
#endif

namespace {

std::string format_targets(const std::vector<int>& targets) {
    std::ostringstream oss;
    oss << "[";
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << targets[i];
    }
    oss << "]";
    return oss.str();
}

constexpr std::size_t kDefaultOneApiMaxStateElements = 1ULL << 28;  // 4 GiB of complex<double> entries
constexpr double kNanosecondsPerMicrosecond = 1000.0;
constexpr double kMicrosecondsPerNanosecond = 1.0 / kNanosecondsPerMicrosecond;

double to_microseconds(double nanoseconds) {
    return nanoseconds * kMicrosecondsPerNanosecond;
}

std::size_t oneapi_max_state_elements() {
    static const std::size_t value = [] {
        const char* env = std::getenv("NA_VM_ONEAPI_MAX_STATE_ELEMENTS");
        if (!env || *env == '\0') {
            return kDefaultOneApiMaxStateElements;
        }
        try {
            const std::size_t parsed = std::stoull(env);
            return parsed > 0 ? parsed : kDefaultOneApiMaxStateElements;
        } catch (const std::invalid_argument&) {
            return kDefaultOneApiMaxStateElements;
        } catch (const std::out_of_range&) {
            return kDefaultOneApiMaxStateElements;
        }
    }();
    return value;
}

int find_allocated_qubits(const std::vector<Instruction>& program) {
    for (const auto& instr : program) {
        if (instr.op == Op::AllocArray) {
            return std::get<int>(instr.payload);
        }
    }
    return -1;
}

}  // namespace

StatevectorEngine::StatevectorEngine(
    HardwareConfig cfg,
    std::unique_ptr<StateBackend> backend,
    std::uint64_t seed
)
    : backend_(backend ? std::move(backend) : std::make_unique<CpuStateBackend>()) {
    state_.hw = std::move(cfg);
    refresh_site_mapping();
    if (seed != std::numeric_limits<std::uint64_t>::max()) {
        rng_.seed(seed);
    } else {
        std::random_device rd;
        rng_.seed(rd());
    }
}

void StatevectorEngine::set_shot_index(int shot) {
    state_.shot_index = shot;
#ifdef NA_VM_WITH_ONEAPI
    device_noise_context_.shot_index = shot;
#endif
}

void StatevectorEngine::log_event(const std::string& category, const std::string& message) {
    state_.logs.push_back(
        ExecutionLog{state_.shot_index, state_.logical_time, category, message});
    if (progress_reporter_) {
        progress_reporter_->record_log(state_.logs.back());
    }
}

std::vector<std::complex<double>>& StatevectorEngine::state_vector() {
    backend_->sync_device_to_host();
    return backend_->state();
}

const std::vector<std::complex<double>>& StatevectorEngine::state_vector() const {
    backend_->sync_device_to_host();
    return backend_->state();
}

void StatevectorEngine::set_noise_model(std::shared_ptr<const NoiseEngine> noise) {
    if (noise) {
        noise_ = noise->clone();
        auto sink = [this](const std::string& category, const std::string& message) {
            this->log_event(category, message);
        };
        // const_cast is safe here because we immediately drop the shared_ptr
        // reference and only hold the cloned, mutable engine.
        const_cast<NoiseEngine*>(noise_.get())->set_log_sink(std::move(sink));
    } else {
        noise_.reset();
    }
}

void StatevectorEngine::set_progress_reporter(neutral_atom_vm::ProgressReporter* reporter) {
    progress_reporter_ = reporter;
}

void StatevectorEngine::set_random_seed(std::uint64_t seed) {
    rng_.seed(seed);
#ifdef NA_VM_WITH_ONEAPI
    device_noise_context_.rng.state = seed;
#endif
}

void StatevectorEngine::run(const std::vector<Instruction>& program) {
    state_.logs.clear();
#ifdef NA_VM_WITH_ONEAPI
    use_batched_shots_ = false;
    batched_shots_ = 1;
    batched_measurements_.clear();
    batched_measurement_rngs_.clear();
    batched_seeds_.clear();
    batched_device_noise_contexts_.clear();
    device_noise_context_.gate_index = 0;
    device_noise_context_.rng.state = rng_();
#endif
    execute_program(program);
}

void StatevectorEngine::execute_program(const std::vector<Instruction>& program) {
    for (const auto& instr : program) {
        switch (instr.op) {
            case Op::AllocArray:
                alloc_array(std::get<int>(instr.payload));
                break;
            case Op::ApplyGate:
                apply_gate(std::get<Gate>(instr.payload));
                break;
            case Op::Measure:
                measure(std::get<std::vector<int>>(instr.payload));
                break;
            case Op::MoveAtom:
                move_atom(std::get<MoveAtomInstruction>(instr.payload));
                break;
            case Op::Wait:
                wait_duration(std::get<WaitInstruction>(instr.payload));
                break;
            case Op::Pulse:
                apply_pulse(std::get<PulseInstruction>(instr.payload));
                break;
        }
        if (progress_reporter_) {
            progress_reporter_->increment_completed_steps();
        }
    }
}

#ifdef NA_VM_WITH_ONEAPI
void StatevectorEngine::run_batched(
    const std::vector<Instruction>& program,
    int shots,
    const std::vector<std::uint64_t>& shot_seeds
) {
    if (shots <= 0) {
        throw std::invalid_argument("Shot count must be positive");
    }
    if (static_cast<int>(shot_seeds.size()) != shots) {
        throw std::invalid_argument("Shot seeds must cover every shot");
    }
    state_.logs.clear();
    use_batched_shots_ = true;
    batched_total_shots_ = static_cast<std::size_t>(shots);
    batched_shot_offset_ = 0;
    batched_seeds_ = shot_seeds;
    batched_measurements_.assign(static_cast<std::size_t>(shots), {});
    batched_measurement_rngs_.assign(
        static_cast<std::size_t>(shots),
        std::mt19937_64{}
    );
    for (int shot = 0; shot < shots; ++shot) {
        batched_measurement_rngs_[static_cast<std::size_t>(shot)].seed(
            batched_seeds_[static_cast<std::size_t>(shot)]);
    }
    batched_device_noise_contexts_.assign(
        static_cast<std::size_t>(shots),
        noise::DeviceNoiseContext{}
    );

    const int alloc_qubits = find_allocated_qubits(program);
    if (alloc_qubits < 0) {
        throw std::invalid_argument("Batched execution requires an AllocArray instruction");
    }
    if (alloc_qubits >= static_cast<int>(sizeof(std::size_t) * 8)) {
        throw std::runtime_error("Requested qubit count exceeds OneAPI addressable space");
    }
    const std::size_t shot_dim = static_cast<std::size_t>(1) << alloc_qubits;
    const std::size_t max_elements = oneapi_max_state_elements();
    if (shot_dim > max_elements) {
        throw std::runtime_error(
            "Requested statevector dimension exceeds configured OneAPI memory limit");
    }

    const std::size_t max_shots_per_batch = max_elements / shot_dim;
    const std::size_t max_int_shots = static_cast<std::size_t>(std::numeric_limits<int>::max());
    const std::size_t target_batch =
        std::min<std::size_t>(max_shots_per_batch > 0 ? max_shots_per_batch : 1, max_int_shots);
    const std::size_t limited_shots = std::min<std::size_t>(
        static_cast<std::size_t>(shots),
        target_batch > 0 ? target_batch : 1);
    if (limited_shots == 0) {
        throw std::runtime_error("Unable to schedule a shot batch under current memory limits");
    }

    std::size_t offset = 0;
    while (offset < static_cast<std::size_t>(shots)) {
        const std::size_t remaining = static_cast<std::size_t>(shots) - offset;
        const int chunk_shots = static_cast<int>(
            std::min<std::size_t>(limited_shots, remaining));
        batched_shots_ = chunk_shots;
        batched_shot_offset_ = offset;
        state_.logical_time = 0.0;
        execute_program(program);
        offset += static_cast<std::size_t>(chunk_shots);
    }

    batched_shot_offset_ = 0;
}

const std::vector<std::vector<MeasurementRecord>>& StatevectorEngine::batched_measurements() const {
    return batched_measurements_;
}
#endif

void StatevectorEngine::alloc_array(int n) {
    if (n <= 0) {
        throw std::invalid_argument("AllocArray requires positive number of qubits");
    }
#ifdef NA_VM_WITH_ONEAPI
    if (auto* gpu_backend = dynamic_cast<OneApiStateBackend*>(backend_.get())) {
        gpu_backend->set_shot_count(use_batched_shots_ ? batched_shots_ : 1);
    }
#endif
    backend_->alloc_array(n);
    state_.n_qubits = backend_->num_qubits();
#ifdef NA_VM_WITH_ONEAPI
    reset_device_noise_contexts();
#endif
    if (state_.hw.positions.size() < static_cast<std::size_t>(n)) {
        state_.hw.positions.resize(static_cast<std::size_t>(n), 0.0);
    }
    state_.last_measurement_time.assign(
        static_cast<std::size_t>(state_.n_qubits),
        std::numeric_limits<double>::lowest()
    );
    backend_->sync_host_to_device();
    if (should_emit_logs()) {
        std::ostringstream oss;
        oss << "AllocArray n_qubits=" << n;
        log_event("AllocArray", oss.str());
    }
}

void StatevectorEngine::apply_gate(const Gate& g) {
    const double gate_start = state_.logical_time;
    const double cooldown = state_.hw.timing_limits.measurement_cooldown_ns;
    if (cooldown > 0.0) {
        for (int target : g.targets) {
            if (target < 0 || target >= static_cast<int>(state_.last_measurement_time.size())) {
                continue;
            }
            const double last = state_.last_measurement_time[static_cast<std::size_t>(target)];
            if (gate_start - last < cooldown) {
                std::ostringstream oss;
                oss << "Gate violates measurement cooldown on qubit " << target
                    << " (start_us=" << to_microseconds(gate_start)
                    << " last_measurement_us=" << to_microseconds(last)
                    << " cooldown_us=" << to_microseconds(cooldown) << ")";
                log_event("TimingConstraint", oss.str());
                throw std::runtime_error("Gate violates measurement cooldown on qubit " + std::to_string(target));
            }
        }
    }

    // Enforce native-gate catalog constraints when configured (ISA v1.1).
    const NativeGate* native_desc = nullptr;
    if (!state_.hw.native_gates.empty()) {
        const int arity = static_cast<int>(g.targets.size());
        for (const auto& candidate : state_.hw.native_gates) {
            if (candidate.name == g.name && candidate.arity == arity) {
                native_desc = &candidate;
                break;
            }
        }
        if (!native_desc) {
            throw std::runtime_error("Gate not supported by hardware: " + g.name);
        }
        if (native_desc->angle_max > native_desc->angle_min) {
            if (g.param < native_desc->angle_min || g.param > native_desc->angle_max) {
                throw std::invalid_argument("Gate parameter out of range for " + g.name);
            }
        }
        if (arity >= 2) {
            if (native_desc->connectivity == ConnectivityKind::NearestNeighborChain) {
                // Simple nearest-neighbor chain constraint over logical indices.
                for (int i = 0; i < arity; ++i) {
                    for (int j = i + 1; j < arity; ++j) {
                        const int a = g.targets[static_cast<std::size_t>(i)];
                        const int b = g.targets[static_cast<std::size_t>(j)];
                        if (std::abs(a - b) != 1) {
                            throw std::runtime_error("Gate violates nearest-neighbor chain connectivity");
                        }
                    }
                }
            } else if (native_desc->connectivity == ConnectivityKind::NearestNeighborGrid) {
                // Enforce 2D grid connectivity using the v1.1 site descriptors.
                if (state_.hw.sites.empty()) {
                    throw std::runtime_error(
                        "Nearest-neighbor grid connectivity requires site coordinates");
                }
                for (int i = 0; i < arity; ++i) {
                    for (int j = i + 1; j < arity; ++j) {
                        const int a = g.targets[static_cast<std::size_t>(i)];
                        const int b = g.targets[static_cast<std::size_t>(j)];
                        const SiteDescriptor* sa = site_descriptor_for_qubit(a);
                        const SiteDescriptor* sb = site_descriptor_for_qubit(b);
                        if (!sa || !sb) {
                            throw std::runtime_error("Gate targets out of range for grid connectivity");
                        }
                        const double dx = std::abs(sa->x - sb->x);
                        const double dy = std::abs(sa->y - sb->y);
                        // Use Manhattan distance 1 as the definition of nearest neighbors.
                    if (std::abs(dx) + std::abs(dy) != 1.0) {
                        throw std::runtime_error("Gate violates nearest-neighbor grid connectivity");
                    }
                }
            }
        }
    }
    }

    if (g.name == "X" && g.targets.size() == 1) {
        std::array<std::complex<double>, 4> U{
            {{0.0, 0.0}, {1.0, 0.0}, {1.0, 0.0}, {0.0, 0.0}}};
        backend_->apply_single_qubit_unitary(g.targets[0], U);
    } else if (g.name == "H" && g.targets.size() == 1) {
        const double inv_sqrt2 = 1.0 / std::sqrt(2.0);
        std::array<std::complex<double>, 4> U{
            {{inv_sqrt2, 0.0}, {inv_sqrt2, 0.0}, {inv_sqrt2, 0.0}, {-inv_sqrt2, 0.0}}};
        backend_->apply_single_qubit_unitary(g.targets[0], U);
    } else if (g.name == "Z" && g.targets.size() == 1) {
        std::array<std::complex<double>, 4> U{
            {{1.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {-1.0, 0.0}}};
        backend_->apply_single_qubit_unitary(g.targets[0], U);
    } else if (g.name == "CX" && g.targets.size() == 2) {
        enforce_blockade(g.targets[0], g.targets[1]);
        // CX with control on g.targets[0] and target on g.targets[1].
        // Basis ordering for the 4x4 block is |q0,q1> with q0 = control and
        // q1 = target, laid out as [|00>, |10>, |01>, |11>].
        std::array<std::complex<double>, 16> U{{
            {1.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0},
            {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {1.0, 0.0},
            {0.0, 0.0}, {0.0, 0.0}, {1.0, 0.0}, {0.0, 0.0},
            {0.0, 0.0}, {1.0, 0.0}, {0.0, 0.0}, {0.0, 0.0},
        }};
        backend_->apply_two_qubit_unitary(g.targets[0], g.targets[1], U);
    } else if (g.name == "CZ" && g.targets.size() == 2) {
        enforce_blockade(g.targets[0], g.targets[1]);
        std::array<std::complex<double>, 16> U{};
        U[0] = {1.0, 0.0};
        U[5] = {1.0, 0.0};
        U[10] = {1.0, 0.0};
        U[15] = {-1.0, 0.0};
        backend_->apply_two_qubit_unitary(g.targets[0], g.targets[1], U);
    } else {
        throw std::runtime_error("Unsupported gate: " + g.name);
    }
    const double duration = native_desc ? native_desc->duration_ns : 0.0;
    state_.logical_time = gate_start + duration;
    std::ostringstream oss;
    oss << g.name << " targets=" << format_targets(g.targets)
        << " param=" << g.param
        << " start_us=" << to_microseconds(gate_start)
        << " duration_us=" << to_microseconds(duration);
#ifdef NA_VM_WITH_ONEAPI
    if (device_noise_engine_ && backend_->is_gpu_backend()) {
        if (auto* gpu_backend = dynamic_cast<OneApiStateBackend*>(backend_.get())) {
            if (use_batched_shots_) {
                const std::size_t base = batched_shot_offset_;
                for (int shot = 0; shot < batched_shots_; ++shot) {
                    const std::size_t global_shot = base + static_cast<std::size_t>(shot);
                    auto& context = batched_device_noise_contexts_[global_shot];
                    context.rng.next_u64();
                    if (g.targets.size() == 1) {
                        device_noise_engine_->apply_single_qubit_gate_noise(
                            gpu_backend->queue(),
                            gpu_backend->state_buffer(),
                            context,
                            g.targets[0]
                        );
                    } else if (g.targets.size() == 2) {
                        device_noise_engine_->apply_two_qubit_gate_noise(
                            gpu_backend->queue(),
                            gpu_backend->state_buffer(),
                            context,
                            g.targets[0],
                            g.targets[1]
                        );
                    }
                    context.gate_index += 1;
                }
            } else {
                device_noise_context_.rng.next_u64();
                if (g.targets.size() == 1) {
                    device_noise_engine_->apply_single_qubit_gate_noise(
                        gpu_backend->queue(),
                        gpu_backend->state_buffer(),
                        device_noise_context_,
                        g.targets[0]
                    );
                } else if (g.targets.size() == 2) {
                    device_noise_engine_->apply_two_qubit_gate_noise(
                        gpu_backend->queue(),
                        gpu_backend->state_buffer(),
                        device_noise_context_,
                        g.targets[0],
                        g.targets[1]
                    );
                }
                device_noise_context_.gate_index += 1;
            }
        }
    }
#endif

        if (noise_ && !backend_->is_gpu_backend()) {
            backend_->sync_device_to_host();
            StdRandomStream noise_rng(rng_);
            if (g.targets.size() == 1) {
                noise_->apply_single_qubit_gate_noise(
                    g.targets[0],
                    state_.n_qubits,
                    backend_->state(),
                    noise_rng
                );
                if (should_emit_logs()) {
                    std::ostringstream oss_noise;
                    oss_noise << "Single-qubit noise applied to target=" << g.targets[0];
                    log_event("Noise", oss_noise.str());
                }
            } else if (g.targets.size() == 2) {
                noise_->apply_two_qubit_gate_noise(
                    g.targets[0],
                    g.targets[1],
                    state_.n_qubits,
                    backend_->state(),
                    noise_rng
                );
                if (should_emit_logs()) {
                    std::ostringstream oss_noise;
                    oss_noise << "Two-qubit noise applied to targets=" << format_targets(g.targets);
                    log_event("Noise", oss_noise.str());
                }
            }
            backend_->sync_host_to_device();
        }

        if (should_emit_logs()) {
            log_event("ApplyGate", oss.str());
        }
}

void StatevectorEngine::move_atom(const MoveAtomInstruction& move) {
    if (state_.n_qubits == 0) {
        throw std::runtime_error("Cannot move atoms before allocation");
    }
    if (move.atom < 0 || move.atom >= state_.n_qubits) {
        throw std::out_of_range("MoveAtom target out of range");
    }
    state_.hw.positions[static_cast<std::size_t>(move.atom)] = move.position;
    std::ostringstream oss;
    oss << "MoveAtom atom=" << move.atom << " position=" << move.position;
    if (should_emit_logs()) {
        log_event("MoveAtom", oss.str());
    }
}

void StatevectorEngine::wait_duration(const WaitInstruction& wait_instr) {
    if (wait_instr.duration < 0.0) {
        throw std::invalid_argument("Wait duration must be non-negative");
    }
    if (state_.hw.timing_limits.min_wait_ns > 0.0 &&
        wait_instr.duration < state_.hw.timing_limits.min_wait_ns) {
        std::ostringstream oss;
        oss << "Wait duration below minimum limit: " << wait_instr.duration
            << " < " << state_.hw.timing_limits.min_wait_ns;
        log_event("TimingConstraint", oss.str());
        throw std::invalid_argument("Wait duration below hardware minimum");
    }
    if (state_.hw.timing_limits.max_wait_ns > 0.0 &&
        wait_instr.duration > state_.hw.timing_limits.max_wait_ns) {
        std::ostringstream oss;
        oss << "Wait duration above maximum limit: " << wait_instr.duration
            << " > " << state_.hw.timing_limits.max_wait_ns;
        log_event("TimingConstraint", oss.str());
        throw std::invalid_argument("Wait duration above hardware maximum");
    }
    state_.logical_time += wait_instr.duration;
#ifdef NA_VM_WITH_ONEAPI
    if (device_noise_engine_ && backend_->is_gpu_backend()) {
        if (auto* gpu_backend = dynamic_cast<OneApiStateBackend*>(backend_.get())) {
            if (use_batched_shots_) {
                const std::size_t base = batched_shot_offset_;
                for (int shot = 0; shot < batched_shots_; ++shot) {
                    const std::size_t global_shot = base + static_cast<std::size_t>(shot);
                    auto& context = batched_device_noise_contexts_[global_shot];
                    context.rng.next_u64();
                    device_noise_engine_->apply_idle_noise(
                        gpu_backend->queue(),
                        gpu_backend->state_buffer(),
                        context,
                        wait_instr.duration
                    );
                    context.gate_index += 1;
                }
            } else {
                device_noise_context_.rng.next_u64();
                device_noise_engine_->apply_idle_noise(
                    gpu_backend->queue(),
                    gpu_backend->state_buffer(),
                    device_noise_context_,
                    wait_instr.duration
                );
                device_noise_context_.gate_index += 1;
            }
        }
    }
#endif
    if (noise_ && !backend_->is_gpu_backend()) {
        backend_->sync_device_to_host();
        StdRandomStream noise_rng(rng_);
        noise_->apply_idle_noise(
            state_.n_qubits,
            backend_->state(),
            wait_instr.duration,
            noise_rng
        );
        backend_->sync_host_to_device();
    }

    if (should_emit_logs()) {
        std::ostringstream oss;
        oss << "Wait duration_us=" << to_microseconds(wait_instr.duration);
        log_event("Wait", oss.str());
    }
}

void StatevectorEngine::apply_pulse(const PulseInstruction& pulse) {
    if (state_.n_qubits == 0) {
        throw std::runtime_error("Cannot apply pulse before allocation");
    }
    if (pulse.target < 0 || pulse.target >= state_.n_qubits) {
        throw std::out_of_range("Pulse target out of range");
    }
    if (pulse.duration < 0.0) {
        throw std::invalid_argument("Pulse duration must be non-negative");
    }
    const auto& limits = state_.hw.pulse_limits;
    if (limits.detuning_max > limits.detuning_min) {
        if (pulse.detuning < limits.detuning_min || pulse.detuning > limits.detuning_max) {
            std::ostringstream oss;
            oss << "Pulse detuning " << pulse.detuning << " outside "
                << limits.detuning_min << ".." << limits.detuning_max;
            log_event("TimingConstraint", oss.str());
            throw std::invalid_argument("Pulse detuning outside hardware limits");
        }
    }
    if (limits.duration_max_ns > limits.duration_min_ns) {
        if (pulse.duration < limits.duration_min_ns || pulse.duration > limits.duration_max_ns) {
            std::ostringstream oss;
            oss << "Pulse duration " << pulse.duration << " outside "
                << limits.duration_min_ns << ".." << limits.duration_max_ns;
            log_event("TimingConstraint", oss.str());
            throw std::invalid_argument("Pulse duration outside hardware limits");
        }
    }
    state_.pulse_log.push_back(pulse);
    std::ostringstream oss;
    oss << "Pulse target=" << pulse.target << " detuning=" << pulse.detuning
        << " duration_us=" << to_microseconds(pulse.duration);
    if (should_emit_logs()) {
        log_event("Pulse", oss.str());
    }
}

void StatevectorEngine::enforce_blockade(int q0, int q1) const {
    if (state_.hw.blockade_radius <= 0.0) {
        return;
    }
    const int max_index = std::max(q0, q1);
    if (max_index >= static_cast<int>(state_.hw.positions.size())) {
        throw std::runtime_error("Insufficient position data for blockade check");
    }
    const double distance = distance_between_qubits(q0, q1);
    if (distance > state_.hw.blockade_radius) {
        throw std::runtime_error("Gate violates blockade radius");
    }
}

double StatevectorEngine::distance_between_qubits(int q0, int q1) const {
    if (!state_.hw.coordinates.empty()) {
        if (q0 >= 0 && q1 >= 0 &&
            q0 < static_cast<int>(state_.hw.coordinates.size()) &&
            q1 < static_cast<int>(state_.hw.coordinates.size())) {
            const auto& vec0 = state_.hw.coordinates[static_cast<std::size_t>(q0)];
            const auto& vec1 = state_.hw.coordinates[static_cast<std::size_t>(q1)];
            const std::size_t dim = std::min(vec0.size(), vec1.size());
            if (dim > 0) {
                double sum = 0.0;
                for (std::size_t d = 0; d < dim; ++d) {
                    const double diff = vec0[d] - vec1[d];
                    sum += diff * diff;
                }
                return std::sqrt(sum);
            }
        }
    }
    const SiteDescriptor* sa = site_descriptor_for_qubit(q0);
    const SiteDescriptor* sb = site_descriptor_for_qubit(q1);
    if (sa && sb) {
        const double dx = sa->x - sb->x;
        const double dy = sa->y - sb->y;
        return std::sqrt(dx * dx + dy * dy);
    }
    const auto& positions = state_.hw.positions;
    return std::abs(positions[static_cast<std::size_t>(q0)] - positions[static_cast<std::size_t>(q1)]);
}

void StatevectorEngine::refresh_site_mapping() {
    state_.site_index.clear();
    state_.site_index.reserve(state_.hw.sites.size());
    for (std::size_t idx = 0; idx < state_.hw.sites.size(); ++idx) {
        state_.site_index[state_.hw.sites[idx].id] = idx;
    }
    state_.slot_site_indices.clear();
    if (state_.hw.site_ids.empty()) {
        state_.slot_site_indices.resize(state_.hw.sites.size());
        for (std::size_t idx = 0; idx < state_.hw.sites.size(); ++idx) {
            state_.slot_site_indices[idx] = idx;
        }
        return;
    }
    state_.slot_site_indices.resize(
        state_.hw.site_ids.size(),
        std::numeric_limits<std::size_t>::max()
    );
    for (std::size_t slot = 0; slot < state_.hw.site_ids.size(); ++slot) {
        const int site_id = state_.hw.site_ids[slot];
        const auto it = state_.site_index.find(site_id);
        if (it != state_.site_index.end()) {
            state_.slot_site_indices[slot] = it->second;
        }
    }
}

const SiteDescriptor* StatevectorEngine::site_descriptor_for_qubit(int qubit) const {
    if (qubit < 0) {
        return nullptr;
    }
    const std::size_t slot = static_cast<std::size_t>(qubit);
    if (slot < state_.slot_site_indices.size()) {
        const std::size_t site_idx = state_.slot_site_indices[slot];
        if (site_idx < state_.hw.sites.size()) {
            return &state_.hw.sites[site_idx];
        }
    }
    if (slot < state_.hw.sites.size()) {
        return &state_.hw.sites[slot];
    }
    return nullptr;
}

void StatevectorEngine::measure(const std::vector<int>& targets) {
    if (targets.empty()) {
        return;
    }
    if (state_.n_qubits == 0) {
        throw std::runtime_error("Cannot measure before allocation");
    }

    const int n = state_.n_qubits;
    for (int t : targets) {
        if (t < 0 || t >= n) {
            throw std::out_of_range("Measurement target out of range");
        }
    }

    MeasurementRecord record;
    bool measured_on_device = false;
    const double measurement_duration = state_.hw.timing_limits.measurement_duration_ns;
    const double measurement_start = state_.logical_time;
#ifdef NA_VM_WITH_ONEAPI
    if (auto* gpu_backend = dynamic_cast<OneApiStateBackend*>(backend_.get())) {
        if (use_batched_shots_) {
            measure_on_device_batched(*gpu_backend, targets);
            measured_on_device = true;
        } else {
            measure_on_device(*gpu_backend, targets, record);
            measured_on_device = true;
        }
    }
#endif

    if (!measured_on_device) {
        backend_->sync_device_to_host();
        auto& amps = backend_->state();
        const std::size_t dim = amps.size();
        const std::size_t k = targets.size();
        const std::size_t combos = static_cast<std::size_t>(1) << k;
        std::vector<double> outcome_probs(combos, 0.0);

        for (std::size_t i = 0; i < dim; ++i) {
            const double p = std::norm(amps[i]);
            if (p == 0.0) {
                continue;
            }
            std::size_t outcome = 0;
            for (std::size_t idx = 0; idx < k; ++idx) {
                const int target = targets[idx];
                const std::size_t bit = (i >> target) & 1ULL;
                outcome |= (bit << idx);
            }
            outcome_probs[outcome] += p;
        }

        double total_prob = 0.0;
        for (double p : outcome_probs) {
            total_prob += p;
        }

        if (total_prob == 0.0) {
            throw std::runtime_error("State has zero norm before measurement");
        }

        for (auto& p : outcome_probs) {
            p /= total_prob;
        }

        std::discrete_distribution<std::size_t> dist(outcome_probs.begin(), outcome_probs.end());
        const std::size_t selected = dist(rng_);

        const double selected_prob = outcome_probs[selected];
        if (selected_prob == 0.0) {
            throw std::runtime_error("Selected measurement outcome has zero probability");
        }
        const double norm_factor = std::sqrt(selected_prob);

        for (std::size_t i = 0; i < dim; ++i) {
            std::size_t outcome = 0;
            for (std::size_t idx = 0; idx < k; ++idx) {
                const int target = targets[idx];
                const std::size_t bit = (i >> target) & 1ULL;
                outcome |= (bit << idx);
            }
            if (outcome == selected) {
                amps[i] /= norm_factor;
            } else {
                amps[i] = {0.0, 0.0};
            }
        }

        record.targets = targets;
        record.bits.reserve(k);
        for (std::size_t idx = 0; idx < k; ++idx) {
            record.bits.push_back(static_cast<int>((selected >> idx) & 1ULL));
        }

        backend_->sync_host_to_device();
    }

    if (noise_ && !use_batched_shots_) {
        StdRandomStream noise_rng(rng_);
        noise_->apply_measurement_noise(record, noise_rng);
    }

    if (!use_batched_shots_) {
        state_.measurements.push_back(std::move(record));
    }

    state_.logical_time += measurement_duration;
    for (int target : targets) {
        if (target >= 0 && target < static_cast<int>(state_.last_measurement_time.size())) {
            state_.last_measurement_time[static_cast<std::size_t>(target)] = state_.logical_time;
        }
    }

    if (should_emit_logs() && !state_.measurements.empty()) {
        const auto& latest = state_.measurements.back();
        std::ostringstream oss;
        oss << "Measure targets=" << format_targets(latest.targets)
            << " bits=" << format_targets(latest.bits)
            << " start_us=" << to_microseconds(measurement_start)
            << " duration_us=" << to_microseconds(measurement_duration)
            << " end_us=" << to_microseconds(state_.logical_time);
        log_event("Measure", oss.str());
    }
}

#ifdef NA_VM_WITH_ONEAPI
void StatevectorEngine::measure_on_device(
    OneApiStateBackend& backend,
    const std::vector<int>& targets,
    MeasurementRecord& record
) {
    const std::size_t k = targets.size();
    const std::size_t combos = static_cast<std::size_t>(1) << k;

    sycl::buffer<double, 1> prob_buffer{sycl::range<1>(combos)};
    sycl::buffer<int, 1> target_buffer{targets.data(), sycl::range<1>(targets.size())};

    const std::size_t dim = backend.dimension();
    auto& queue = backend.queue();

    queue.submit([&](sycl::handler& cgh) {
        auto acc = prob_buffer.template get_access<sycl::access::mode::discard_write>(cgh);
        cgh.parallel_for(sycl::range<1>(combos), [=](sycl::id<1> idx) {
            acc[idx] = 0.0;
        });
    });

    queue.submit([&](sycl::handler& cgh) {
        auto state_acc = backend.state_buffer().template get_access<sycl::access::mode::read>(cgh);
        auto prob_acc = prob_buffer.template get_access<sycl::access::mode::read_write>(cgh);
        auto target_acc = target_buffer.template get_access<sycl::access::mode::read>(cgh);
        const auto prob_ptr =
            prob_acc.template get_multi_ptr<sycl::access::decorated::yes>();
        cgh.parallel_for(sycl::range<1>(dim), [=](sycl::id<1> idx) {
            const std::size_t i = idx[0];
            std::size_t outcome = 0;
            const std::size_t target_count = target_acc.size();
            for (std::size_t tidx = 0; tidx < target_count; ++tidx) {
                const std::size_t bit = (i >> static_cast<std::size_t>(target_acc[tidx])) & 1ULL;
                outcome |= bit << tidx;
            }
            const double amp_real = state_acc[i].real();
            const double amp_imag = state_acc[i].imag();
            const double prob = amp_real * amp_real + amp_imag * amp_imag;
            sycl::atomic_ref<double, sycl::memory_order::relaxed, sycl::memory_scope::device,
                sycl::access::address_space::global_space> atomic_ref(prob_ptr[outcome]);
            atomic_ref.fetch_add(prob);
        });
    });

    queue.wait();

    std::vector<double> outcome_probs(combos, 0.0);
    {
        auto read_acc = prob_buffer.template get_access<sycl::access::mode::read>();
        for (std::size_t i = 0; i < combos; ++i) {
            outcome_probs[i] = read_acc[i];
        }
    }

    double total_prob = 0.0;
    for (double p : outcome_probs) {
        total_prob += p;
    }
    if (total_prob == 0.0) {
        throw std::runtime_error("State has zero norm before measurement");
    }

    for (auto& p : outcome_probs) {
        p /= total_prob;
    }

    std::discrete_distribution<std::size_t> dist(outcome_probs.begin(), outcome_probs.end());
    const std::size_t selected = dist(rng_);
    const double selected_prob = outcome_probs[selected];
    if (selected_prob == 0.0) {
        throw std::runtime_error("Selected measurement outcome has zero probability");
    }
    const double norm_factor = std::sqrt(selected_prob);

    queue.submit([&](sycl::handler& cgh) {
        auto state_acc = backend.state_buffer().template get_access<sycl::access::mode::read_write>(cgh);
        auto target_acc = target_buffer.template get_access<sycl::access::mode::read>(cgh);
        cgh.parallel_for(sycl::range<1>(dim), [=](sycl::id<1> idx) {
            const std::size_t i = idx[0];
            std::size_t outcome = 0;
            const std::size_t target_count = target_acc.size();
            for (std::size_t tidx = 0; tidx < target_count; ++tidx) {
                const std::size_t bit = (i >> static_cast<std::size_t>(target_acc[tidx])) & 1ULL;
                outcome |= bit << tidx;
            }
            if (outcome == selected) {
                state_acc[i] /= norm_factor;
            } else {
                state_acc[i] = std::complex<double>{0.0, 0.0};
            }
        });
    });

    queue.wait();

    record.targets = targets;
    record.bits.reserve(k);
    for (std::size_t idx = 0; idx < k; ++idx) {
        record.bits.push_back(static_cast<int>((selected >> idx) & 1ULL));
    }
}

void StatevectorEngine::measure_on_device_batched(
    OneApiStateBackend& backend,
    const std::vector<int>& targets
) {
    const std::size_t k = targets.size();
    const std::size_t combos = static_cast<std::size_t>(1) << k;
    const int shots = batched_shots_;
    const std::size_t shot_dim = backend.dimension();
    const std::size_t total_dim = backend.total_dimension();

    if (shots <= 0) {
        throw std::runtime_error("Invalid shot count for batched measurement");
    }

    sycl::buffer<double, 1> prob_buffer{
        sycl::range<1>(static_cast<std::size_t>(shots) * combos)};
    sycl::buffer<int, 1> target_buffer{
        targets.data(),
        sycl::range<1>(targets.size())};

    auto& queue = backend.queue();

    queue.submit([&](sycl::handler& cgh) {
        auto acc = prob_buffer.template get_access<sycl::access::mode::discard_write>(cgh);
        cgh.parallel_for(sycl::range<1>(static_cast<std::size_t>(shots) * combos), [=](sycl::id<1> idx) {
            acc[idx] = 0.0;
        });
    });

    queue.submit([&](sycl::handler& cgh) {
        auto state_acc = backend.state_buffer().template get_access<sycl::access::mode::read>(cgh);
        auto prob_acc = prob_buffer.template get_access<sycl::access::mode::read_write>(cgh);
        auto target_acc = target_buffer.template get_access<sycl::access::mode::read>(cgh);
        const auto prob_ptr =
            prob_acc.template get_multi_ptr<sycl::access::decorated::yes>();
        cgh.parallel_for(sycl::range<1>(total_dim), [=](sycl::id<1> idx) {
            const std::size_t flat = idx[0];
            const std::size_t shot_id = flat / shot_dim;
            const std::size_t local = flat % shot_dim;
            std::size_t outcome = 0;
            const std::size_t target_count = target_acc.size();
            for (std::size_t tidx = 0; tidx < target_count; ++tidx) {
                const std::size_t bit = (local >> static_cast<std::size_t>(target_acc[tidx])) & 1ULL;
                outcome |= bit << tidx;
            }
            const std::size_t index = static_cast<std::size_t>(shot_id) * combos + outcome;
            const double amp_real = state_acc[flat].real();
            const double amp_imag = state_acc[flat].imag();
            const double prob = amp_real * amp_real + amp_imag * amp_imag;
            sycl::atomic_ref<double, sycl::memory_order::relaxed, sycl::memory_scope::device,
                sycl::access::address_space::global_space> atomic_ref(prob_ptr[index]);
            atomic_ref.fetch_add(prob);
        });
    });

    queue.wait();

    std::vector<double> outcome_probs(
        static_cast<std::size_t>(shots) * combos, 0.0);
    {
        auto read_acc = prob_buffer.template get_access<sycl::access::mode::read>();
        for (std::size_t i = 0; i < outcome_probs.size(); ++i) {
            outcome_probs[i] = read_acc[i];
        }
    }

    std::vector<int> selected_outcomes(static_cast<std::size_t>(shots));
    std::vector<double> selected_probs(static_cast<std::size_t>(shots));
    const std::size_t global_offset = batched_shot_offset_;
    for (int shot = 0; shot < shots; ++shot) {
        const std::size_t base = static_cast<std::size_t>(shot) * combos;
        double total_prob = 0.0;
        for (std::size_t idx = 0; idx < combos; ++idx) {
            total_prob += outcome_probs[base + idx];
        }
        if (total_prob == 0.0) {
            throw std::runtime_error("State has zero norm before measurement");
        }

        std::vector<double> normalized(combos);
        for (std::size_t idx = 0; idx < combos; ++idx) {
            normalized[idx] = outcome_probs[base + idx] / total_prob;
        }

        std::discrete_distribution<std::size_t> dist(normalized.begin(), normalized.end());
        const std::size_t global_shot = global_offset + static_cast<std::size_t>(shot);
        const std::size_t selected = dist(batched_measurement_rngs_[global_shot]);
        const double selected_prob = normalized[selected];
        if (selected_prob == 0.0) {
            throw std::runtime_error("Selected measurement outcome has zero probability");
        }

        selected_outcomes[static_cast<std::size_t>(shot)] = static_cast<int>(selected);
        selected_probs[static_cast<std::size_t>(shot)] = selected_prob;

        MeasurementRecord shot_record;
        shot_record.targets = targets;
        shot_record.bits.reserve(k);
        for (std::size_t idx = 0; idx < k; ++idx) {
            shot_record.bits.push_back(static_cast<int>((selected >> idx) & 1ULL));
        }

        if (noise_) {
            StdRandomStream noise_rng(batched_measurement_rngs_[global_shot]);
            noise_->apply_measurement_noise(shot_record, noise_rng);
        }

        batched_measurements_[global_shot].push_back(std::move(shot_record));
    }

    sycl::buffer<int, 1> outcome_buffer{
        selected_outcomes.data(),
        sycl::range<1>(static_cast<std::size_t>(shots))};
    sycl::buffer<double, 1> probs_buffer{
        selected_probs.data(),
        sycl::range<1>(static_cast<std::size_t>(shots))};
    queue.submit([&](sycl::handler& cgh) {
        auto state_acc = backend.state_buffer().template get_access<sycl::access::mode::read_write>(cgh);
        auto target_acc = target_buffer.template get_access<sycl::access::mode::read>(cgh);
        auto outcome_acc = outcome_buffer.template get_access<sycl::access::mode::read>(cgh);
        auto prob_acc = probs_buffer.template get_access<sycl::access::mode::read>(cgh);
        cgh.parallel_for(sycl::range<1>(total_dim), [=](sycl::id<1> idx) {
            const std::size_t flat = idx[0];
            const std::size_t shot_id = flat / shot_dim;
            const std::size_t local = flat % shot_dim;
            std::size_t outcome = 0;
            const std::size_t target_count = target_acc.size();
            for (std::size_t tidx = 0; tidx < target_count; ++tidx) {
                const std::size_t bit = (local >> static_cast<std::size_t>(target_acc[tidx])) & 1ULL;
                outcome |= bit << tidx;
            }
            const int desired = outcome_acc[shot_id];
            if (static_cast<std::size_t>(desired) == outcome) {
                const double norm_factor = std::sqrt(prob_acc[shot_id]);
                state_acc[flat] /= norm_factor;
            } else {
                state_acc[flat] = std::complex<double>{0.0, 0.0};
            }
        });
    });

    queue.wait();

    state_.measurements.push_back(batched_measurements_[0].back());
}
#endif

#ifdef NA_VM_WITH_ONEAPI
void StatevectorEngine::reset_device_noise_contexts() {
    if (!backend_->is_gpu_backend()) {
        return;
    }
    auto* gpu_backend = dynamic_cast<OneApiStateBackend*>(backend_.get());
    if (!gpu_backend) {
        return;
    }
    const std::size_t stride = gpu_backend->dimension();
    if (device_noise_engine_) {
        device_noise_context_.n_qubits = state_.n_qubits;
        device_noise_context_.shot_stride = stride;
        device_noise_context_.gate_index = 0;
        device_noise_context_.rng.state = rng_();
    }
    if (use_batched_shots_) {
        const std::size_t base = batched_shot_offset_;
        for (int shot = 0; shot < batched_shots_; ++shot) {
            const std::size_t global_shot = base + static_cast<std::size_t>(shot);
            auto& context = batched_device_noise_contexts_[global_shot];
            context.n_qubits = state_.n_qubits;
            context.n_shots = batched_shots_;
            context.shot_index = static_cast<int>(global_shot);
            context.shot_stride = stride;
            context.gate_index = 0;
            if (global_shot < batched_seeds_.size()) {
                context.rng.state = batched_seeds_[global_shot];
            } else {
                context.rng.state = rng_();
            }
        }
    }
}
#endif
void StatevectorEngine::set_device_noise_engine(
    std::shared_ptr<const noise::DeviceNoiseEngine> noise
) {
#ifdef NA_VM_WITH_ONEAPI
    device_noise_engine_ = std::move(noise);
    device_noise_context_ = noise::DeviceNoiseContext{};
    device_noise_context_.n_qubits = state_.n_qubits;
#else
    (void)noise;
#endif
}

bool StatevectorEngine::should_emit_logs() const {
#ifdef NA_VM_WITH_ONEAPI
    return !(use_batched_shots_ && backend_->is_gpu_backend());
#else
    return true;
#endif
}
