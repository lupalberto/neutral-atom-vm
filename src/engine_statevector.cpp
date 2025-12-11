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

constexpr double kNanosecondsPerMicrosecond = 1000.0;
constexpr double kMicrosecondsPerNanosecond = 1.0 / kNanosecondsPerMicrosecond;

double to_microseconds(double nanoseconds) {
    return nanoseconds * kMicrosecondsPerNanosecond;
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
}

void StatevectorEngine::run(const std::vector<Instruction>& program) {
    state_.logs.clear();
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


void StatevectorEngine::alloc_array(int n) {
    if (n <= 0) {
        throw std::invalid_argument("AllocArray requires positive number of qubits");
    }
    backend_->alloc_array(n);
    state_.n_qubits = backend_->num_qubits();
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
        if (noise_) {
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
    if (noise_) {
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
    if (auto reason = blockade_violation_reason(state_.hw, state_.site_index, q0, q1)) {
        throw std::runtime_error("Gate violates " + *reason);
    }
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

    if (noise_) {
        StdRandomStream noise_rng(rng_);
        noise_->apply_measurement_noise(record, noise_rng);
    }

    state_.measurements.push_back(std::move(record));

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


bool StatevectorEngine::should_emit_logs() const {
    return true;
}
