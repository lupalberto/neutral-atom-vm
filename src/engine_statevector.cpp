#include "engine_statevector.hpp"

#include "noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

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

std::vector<std::complex<double>>& StatevectorEngine::state_vector() {
    return backend_->state();
}

const std::vector<std::complex<double>>& StatevectorEngine::state_vector() const {
    return backend_->state();
}

void StatevectorEngine::set_noise_model(std::shared_ptr<const NoiseEngine> noise) {
    if (noise) {
        noise_ = noise->clone();
    } else {
        noise_.reset();
    }
}

void StatevectorEngine::set_random_seed(std::uint64_t seed) {
    rng_.seed(seed);
}

void StatevectorEngine::run(const std::vector<Instruction>& program) {
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
    backend_->sync_host_to_device();
}

void StatevectorEngine::apply_gate(const Gate& g) {
    backend_->sync_host_to_device();

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
        } else if (g.targets.size() == 2) {
            noise_->apply_two_qubit_gate_noise(
                g.targets[0],
                g.targets[1],
                state_.n_qubits,
                backend_->state(),
                noise_rng
            );
        }
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
}

void StatevectorEngine::wait_duration(const WaitInstruction& wait_instr) {
    if (wait_instr.duration < 0.0) {
        throw std::invalid_argument("Wait duration must be non-negative");
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
    state_.pulse_log.push_back(pulse);
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

    backend_->sync_host_to_device();
}
