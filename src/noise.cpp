#include "noise.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

SimpleNoiseEngine::SimpleNoiseEngine(SimpleNoiseConfig config)
    : config_(config) {
    validate_config(config_);
}

void SimpleNoiseEngine::apply_measurement_noise(
    MeasurementRecord& record,
    std::mt19937_64& rng
) const {
    const bool has_quantum = config_.p_quantum_flip > 0.0;
    const bool has_loss = config_.p_loss > 0.0;
    const bool has_readout = config_.readout.p_flip0_to_1 > 0.0 ||
                             config_.readout.p_flip1_to_0 > 0.0;

    if (!has_quantum && !has_loss && !has_readout) {
        return;
    }

    std::uniform_real_distribution<double> dist(0.0, 1.0);

    for (int& bit : record.bits) {
        double r = dist(rng);

        // Loss / erasure: mark bit as -1 and skip further processing.
        if (has_loss && r < config_.p_loss) {
            bit = -1;
            continue;
        }

        // Effective quantum bit-flip channel (symmetric).
        if (has_quantum) {
            r = dist(rng);
            if (r < config_.p_quantum_flip) {
                if (bit == 0) {
                    bit = 1;
                } else if (bit == 1) {
                    bit = 0;
                }
            }
        }

        // Classical readout noise.
        if (has_readout) {
            r = dist(rng);
            if (bit == 0) {
                if (r < config_.readout.p_flip0_to_1) {
                    bit = 1;
                }
            } else if (bit == 1) {
                if (r < config_.readout.p_flip1_to_0) {
                    bit = 0;
                }
            }
        }
    }
}

void SimpleNoiseEngine::validate_config(
    const SimpleNoiseConfig& config
) {
    if (config.p_quantum_flip < 0.0 || config.p_quantum_flip > 1.0 ||
        config.p_loss < 0.0 || config.p_loss > 1.0 ||
        config.readout.p_flip0_to_1 < 0.0 || config.readout.p_flip0_to_1 > 1.0 ||
        config.readout.p_flip1_to_0 < 0.0 || config.readout.p_flip1_to_0 > 1.0) {
        throw std::invalid_argument("Noise probabilities must be in [0, 1]");
    }

    const auto check_pauli = [](const SingleQubitPauliConfig& c) {
        const double sum = c.px + c.py + c.pz;
        if (c.px < 0.0 || c.px > 1.0 ||
            c.py < 0.0 || c.py > 1.0 ||
            c.pz < 0.0 || c.pz > 1.0 ||
            sum > 1.0 + 1e-12) {
            throw std::invalid_argument(
                "Pauli channel probabilities must be in [0, 1] "
                "and px + py + pz <= 1");
        }
    };

    check_pauli(config.gate.single_qubit);
    check_pauli(config.gate.two_qubit_control);
    check_pauli(config.gate.two_qubit_target);
}

namespace {

void apply_pauli_x(
    std::vector<std::complex<double>>& state,
    int n_qubits,
    int target
) {
    const std::size_t dim = state.size();
    const std::size_t bit = static_cast<std::size_t>(1) << target;
    for (std::size_t i = 0; i < dim; ++i) {
        if ((i & bit) == 0) {
            const std::size_t j = i | bit;
            std::swap(state[i], state[j]);
        }
    }
}

void apply_pauli_z(
    std::vector<std::complex<double>>& state,
    int n_qubits,
    int target
) {
    const std::size_t dim = state.size();
    const std::size_t bit = static_cast<std::size_t>(1) << target;
    (void)n_qubits;
    for (std::size_t i = 0; i < dim; ++i) {
        if ((i & bit) != 0) {
            state[i] = -state[i];
        }
    }
}

void apply_pauli_y(
    std::vector<std::complex<double>>& state,
    int n_qubits,
    int target
) {
    const std::size_t dim = state.size();
    const std::size_t bit = static_cast<std::size_t>(1) << target;
    (void)n_qubits;

    const std::complex<double> imag(0.0, 1.0);
    const std::complex<double> minus_imag(0.0, -1.0);

    for (std::size_t i = 0; i < dim; ++i) {
        if ((i & bit) == 0) {
            const std::size_t j = i | bit;
            const auto a0 = state[i];
            const auto a1 = state[j];
            // Y = [[0, -i], [i, 0]]
            state[i] = minus_imag * a1;
            state[j] = imag * a0;
        }
    }
}

char sample_pauli(
    const SingleQubitPauliConfig& cfg,
    std::mt19937_64& rng
) {
    const double px = cfg.px;
    const double py = cfg.py;
    const double pz = cfg.pz;
    const double sum = px + py + pz;
    if (sum <= 0.0) {
        return 'I';
    }
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    const double r = dist(rng);

    if (r < px) {
        return 'X';
    }
    if (r < px + py) {
        return 'Y';
    }
    if (r < px + py + pz) {
        return 'Z';
    }
    return 'I';
}

void apply_single_qubit_pauli(
    char pauli,
    std::vector<std::complex<double>>& state,
    int n_qubits,
    int target
) {
    switch (pauli) {
        case 'X':
            apply_pauli_x(state, n_qubits, target);
            break;
        case 'Y':
            apply_pauli_y(state, n_qubits, target);
            break;
        case 'Z':
            apply_pauli_z(state, n_qubits, target);
            break;
        default:
            break;
    }
}

}  // namespace

void SimpleNoiseEngine::apply_single_qubit_gate_noise(
    int target,
    int n_qubits,
    std::vector<std::complex<double>>& amplitudes,
    std::mt19937_64& rng
) const {
    const auto cfg = config_.gate.single_qubit;
    const double sum = cfg.px + cfg.py + cfg.pz;
    if (sum <= 0.0) {
        return;
    }
    const char pauli = sample_pauli(cfg, rng);
    apply_single_qubit_pauli(pauli, amplitudes, n_qubits, target);
}

void SimpleNoiseEngine::apply_two_qubit_gate_noise(
    int q0,
    int q1,
    int n_qubits,
    std::vector<std::complex<double>>& amplitudes,
    std::mt19937_64& rng
) const {
    const auto cfg_ctrl = config_.gate.two_qubit_control;
    const auto cfg_tgt = config_.gate.two_qubit_target;

    if (cfg_ctrl.px + cfg_ctrl.py + cfg_ctrl.pz > 0.0) {
        const char p0 = sample_pauli(cfg_ctrl, rng);
        apply_single_qubit_pauli(p0, amplitudes, n_qubits, q0);
    }
    if (cfg_tgt.px + cfg_tgt.py + cfg_tgt.pz > 0.0) {
        const char p1 = sample_pauli(cfg_tgt, rng);
        apply_single_qubit_pauli(p1, amplitudes, n_qubits, q1);
    }
}
