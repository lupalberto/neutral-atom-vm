#include "engine_statevector.hpp"

#include "noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>

StatevectorEngine::StatevectorEngine(HardwareConfig cfg) {
    state_.hw = std::move(cfg);
    std::random_device rd;
    rng_.seed(rd());
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
    state_.n_qubits = n;
    state_.state.assign(static_cast<std::size_t>(1) << n, {0.0, 0.0});
    state_.state[0] = {1.0, 0.0};  // |0...0>
    if (state_.hw.positions.size() < static_cast<std::size_t>(n)) {
        state_.hw.positions.resize(static_cast<std::size_t>(n), 0.0);
    }
}

void StatevectorEngine::apply_single_qubit_unitary(
    int q,
    const std::array<std::complex<double>, 4>& U
) {
    const int n = state_.n_qubits;
    if (q < 0 || q >= n) {
        throw std::out_of_range("Invalid qubit index");
    }

    const std::size_t dim = state_.state.size();
    const std::size_t bit = static_cast<std::size_t>(1) << q;
    for (std::size_t i = 0; i < dim; ++i) {
        if ((i & bit) == 0) {
            const std::size_t j = i | bit;
            const auto a0 = state_.state[i];
            const auto a1 = state_.state[j];
            state_.state[i] = U[0] * a0 + U[1] * a1;
            state_.state[j] = U[2] * a0 + U[3] * a1;
        }
    }
}

void StatevectorEngine::apply_two_qubit_unitary(
    int q0,
    int q1,
    const std::array<std::complex<double>, 16>& U
) {
    if (q0 == q1) {
        throw std::invalid_argument("Two-qubit gate requires distinct targets");
    }
    if (q0 > q1) {
        std::swap(q0, q1);
    }

    const int n = state_.n_qubits;
    if (q0 < 0 || q1 < 0 || q0 >= n || q1 >= n) {
        throw std::out_of_range("Invalid qubit index");
    }

    const std::size_t dim = state_.state.size();
    const std::size_t b0 = static_cast<std::size_t>(1) << q0;
    const std::size_t b1 = static_cast<std::size_t>(1) << q1;

    for (std::size_t i = 0; i < dim; ++i) {
        if (((i & b0) == 0) && ((i & b1) == 0)) {
            const std::size_t i01 = i | b0;
            const std::size_t i10 = i | b1;
            const std::size_t i11 = i | b0 | b1;

            const auto a00 = state_.state[i];
            const auto a01 = state_.state[i01];
            const auto a10 = state_.state[i10];
            const auto a11 = state_.state[i11];

            const std::array<std::complex<double>, 4> in = {a00, a01, a10, a11};
            std::array<std::complex<double>, 4> out{};

            for (int row = 0; row < 4; ++row) {
                out[row] = {0.0, 0.0};
                for (int col = 0; col < 4; ++col) {
                    out[row] += U[4 * row + col] * in[col];
                }
            }

            state_.state[i] = out[0];
            state_.state[i01] = out[1];
            state_.state[i10] = out[2];
            state_.state[i11] = out[3];
        }
    }
}

void StatevectorEngine::apply_gate(const Gate& g) {
    if (g.name == "X" && g.targets.size() == 1) {
        std::array<std::complex<double>, 4> U{
            {{0.0, 0.0}, {1.0, 0.0}, {1.0, 0.0}, {0.0, 0.0}}};
        apply_single_qubit_unitary(g.targets[0], U);
    } else if (g.name == "H" && g.targets.size() == 1) {
        const double inv_sqrt2 = 1.0 / std::sqrt(2.0);
        std::array<std::complex<double>, 4> U{
            {{inv_sqrt2, 0.0}, {inv_sqrt2, 0.0}, {inv_sqrt2, 0.0}, {-inv_sqrt2, 0.0}}};
        apply_single_qubit_unitary(g.targets[0], U);
    } else if (g.name == "Z" && g.targets.size() == 1) {
        std::array<std::complex<double>, 4> U{
            {{1.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {-1.0, 0.0}}};
        apply_single_qubit_unitary(g.targets[0], U);
    } else if (g.name == "CX" && g.targets.size() == 2) {
        enforce_blockade(g.targets[0], g.targets[1]);
        // Standard CNOT matrix in computational basis order |00>,|01>,|10>,|11|
        std::array<std::complex<double>, 16> U{{
            {1.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0},
            {0.0, 0.0}, {1.0, 0.0}, {0.0, 0.0}, {0.0, 0.0},
            {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {1.0, 0.0},
            {0.0, 0.0}, {0.0, 0.0}, {1.0, 0.0}, {0.0, 0.0},
        }};
        apply_two_qubit_unitary(g.targets[0], g.targets[1], U);
    } else if (g.name == "CZ" && g.targets.size() == 2) {
        enforce_blockade(g.targets[0], g.targets[1]);
        std::array<std::complex<double>, 16> U{};
        // identity except -1 phase on |11>
        U[0] = {1.0, 0.0};
        U[5] = {1.0, 0.0};
        U[10] = {1.0, 0.0};
        U[15] = {-1.0, 0.0};
        apply_two_qubit_unitary(g.targets[0], g.targets[1], U);
    } else {
        throw std::runtime_error("Unsupported gate: " + g.name);
    }

    if (noise_) {
        StdRandomStream noise_rng(rng_);
        if (g.targets.size() == 1) {
            noise_->apply_single_qubit_gate_noise(
                g.targets[0],
                state_.n_qubits,
                state_.state,
                noise_rng
            );
        } else if (g.targets.size() == 2) {
            noise_->apply_two_qubit_gate_noise(
                g.targets[0],
                g.targets[1],
                state_.n_qubits,
                state_.state,
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
            state_.state,
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

    const std::size_t dim = state_.state.size();
    const std::size_t k = targets.size();
    const std::size_t combos = static_cast<std::size_t>(1) << k;
    std::vector<double> outcome_probs(combos, 0.0);

    for (std::size_t i = 0; i < dim; ++i) {
        const double p = std::norm(state_.state[i]);
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
            state_.state[i] /= norm_factor;
        } else {
            state_.state[i] = {0.0, 0.0};
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
}
