#include "engine_statevector.hpp"

#include "noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

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

}  // namespace

StatevectorEngine::StatevectorEngine(
    HardwareConfig cfg,
    std::unique_ptr<StateBackend> backend,
    std::uint64_t seed
)
    : backend_(backend ? std::move(backend) : std::make_unique<CpuStateBackend>()) {
    state_.hw = std::move(cfg);
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
}

std::vector<std::complex<double>>& StatevectorEngine::state_vector() {
    return backend_->state();
}

const std::vector<std::complex<double>>& StatevectorEngine::state_vector() const {
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

void StatevectorEngine::set_random_seed(std::uint64_t seed) {
    rng_.seed(seed);
}

void StatevectorEngine::run(const std::vector<Instruction>& program) {
    state_.logs.clear();
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
    std::ostringstream oss;
    oss << "AllocArray n_qubits=" << n;
    log_event("AllocArray", oss.str());
}

void StatevectorEngine::apply_gate(const Gate& g) {
    backend_->sync_host_to_device();

    const double cooldown = state_.hw.timing_limits.measurement_cooldown_ns;
    if (cooldown > 0.0) {
        const double now = state_.logical_time;
        for (int target : g.targets) {
            if (target < 0 || target >= static_cast<int>(state_.last_measurement_time.size())) {
                continue;
            }
            const double last = state_.last_measurement_time[static_cast<std::size_t>(target)];
            if (now - last < cooldown) {
                std::ostringstream oss;
                oss << "Gate violates measurement cooldown on qubit " << target
                    << " (now=" << now << " last=" << last << " cooldown=" << cooldown << ")";
                log_event("TimingConstraint", oss.str());
                throw std::runtime_error("Gate violates measurement cooldown on qubit " + std::to_string(target));
            }
        }
    }

    // Enforce native-gate catalog constraints when configured (ISA v1.1).
    if (!state_.hw.native_gates.empty()) {
        const int arity = static_cast<int>(g.targets.size());
        const NativeGate* desc = nullptr;
        for (const auto& candidate : state_.hw.native_gates) {
            if (candidate.name == g.name && candidate.arity == arity) {
                desc = &candidate;
                break;
            }
        }
        if (!desc) {
            throw std::runtime_error("Gate not supported by hardware: " + g.name);
        }
        if (desc->angle_max > desc->angle_min) {
            if (g.param < desc->angle_min || g.param > desc->angle_max) {
                throw std::invalid_argument("Gate parameter out of range for " + g.name);
            }
        }
        if (arity >= 2) {
            if (desc->connectivity == ConnectivityKind::NearestNeighborChain) {
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
            } else if (desc->connectivity == ConnectivityKind::NearestNeighborGrid) {
                // Enforce 2D grid connectivity using the v1.1 site descriptors.
                if (state_.hw.sites.empty()) {
                    throw std::runtime_error(
                        "Nearest-neighbor grid connectivity requires site coordinates");
                }
                const auto& sites = state_.hw.sites;
                for (int i = 0; i < arity; ++i) {
                    for (int j = i + 1; j < arity; ++j) {
                        const int a = g.targets[static_cast<std::size_t>(i)];
                        const int b = g.targets[static_cast<std::size_t>(j)];
                        if (a < 0 || b < 0 ||
                            a >= static_cast<int>(sites.size()) ||
                            b >= static_cast<int>(sites.size())) {
                            throw std::runtime_error("Gate targets out of range for grid connectivity");
                        }
                        const auto& sa = sites[static_cast<std::size_t>(a)];
                        const auto& sb = sites[static_cast<std::size_t>(b)];
                        const double dx = std::abs(sa.x - sb.x);
                        const double dy = std::abs(sa.y - sb.y);
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
        std::array<std::complex<double>, 16> U{{
            {1.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0},
            {0.0, 0.0}, {1.0, 0.0}, {0.0, 0.0}, {0.0, 0.0},
            {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {1.0, 0.0},
            {0.0, 0.0}, {0.0, 0.0}, {1.0, 0.0}, {0.0, 0.0},
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

    backend_->sync_device_to_host();

    if (noise_) {
        StdRandomStream noise_rng(rng_);
        if (g.targets.size() == 1) {
            noise_->apply_single_qubit_gate_noise(
                g.targets[0],
                state_.n_qubits,
                backend_->state(),
                noise_rng
            );
            std::ostringstream oss;
            oss << "Single-qubit noise applied to target=" << g.targets[0];
            log_event("Noise", oss.str());
        } else if (g.targets.size() == 2) {
            noise_->apply_two_qubit_gate_noise(
                g.targets[0],
                g.targets[1],
                state_.n_qubits,
                backend_->state(),
                noise_rng
            );
            std::ostringstream oss;
            oss << "Two-qubit noise applied to targets=" << format_targets(g.targets);
            log_event("Noise", oss.str());
        }
    }

    std::ostringstream oss;
    oss << g.name << " targets=" << format_targets(g.targets) << " param=" << g.param;
    log_event("ApplyGate", oss.str());
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
    log_event("MoveAtom", oss.str());
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
        StdRandomStream noise_rng(rng_);
        noise_->apply_idle_noise(
            state_.n_qubits,
            backend_->state(),
            wait_instr.duration,
            noise_rng
        );
    }

    std::ostringstream oss;
    oss << "Wait duration=" << wait_instr.duration;
    log_event("Wait", oss.str());
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
        << " duration=" << pulse.duration;
    log_event("Pulse", oss.str());
}

void StatevectorEngine::enforce_blockade(int q0, int q1) const {
    if (state_.hw.blockade_radius <= 0.0) {
        return;
    }
    const int max_index = std::max(q0, q1);
    if (max_index >= static_cast<int>(state_.hw.positions.size())) {
        throw std::runtime_error("Insufficient position data for blockade check");
    }
    const double distance = std::abs(state_.hw.positions[q0] - state_.hw.positions[q1]);
    if (distance > state_.hw.blockade_radius) {
        throw std::runtime_error("Gate violates blockade radius");
    }
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

    MeasurementRecord record;
    record.targets = targets;
    record.bits.reserve(k);
    for (std::size_t idx = 0; idx < k; ++idx) {
        record.bits.push_back(static_cast<int>((selected >> idx) & 1ULL));
    }

    if (noise_) {
        StdRandomStream noise_rng(rng_);
        noise_->apply_measurement_noise(record, noise_rng);
    }

    state_.measurements.push_back(std::move(record));

    for (int target : state_.measurements.back().targets) {
        if (target >= 0 && target < static_cast<int>(state_.last_measurement_time.size())) {
            state_.last_measurement_time[static_cast<std::size_t>(target)] = state_.logical_time;
        }
    }

    backend_->sync_host_to_device();

    const auto& latest = state_.measurements.back();
    std::ostringstream oss;
    oss << "Measure targets=" << format_targets(latest.targets)
        << " bits=" << format_targets(latest.bits);
    log_event("Measure", oss.str());
}
