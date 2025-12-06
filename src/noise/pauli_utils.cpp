#include "noise/pauli_utils.hpp"

#include <cmath>

void apply_pauli_x(
    std::vector<std::complex<double>>& state,
    int /*n_qubits*/,
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

void apply_pauli_y(
    std::vector<std::complex<double>>& state,
    int /*n_qubits*/,
    int target
) {
    const std::size_t dim = state.size();
    const std::size_t bit = static_cast<std::size_t>(1) << target;
    const std::complex<double> imag(0.0, 1.0);
    const std::complex<double> minus_imag(0.0, -1.0);
    for (std::size_t i = 0; i < dim; ++i) {
        if ((i & bit) == 0) {
            const std::size_t j = i | bit;
            const auto a0 = state[i];
            const auto a1 = state[j];
            state[i] = minus_imag * a1;
            state[j] = imag * a0;
        }
    }
}

void apply_pauli_z(
    std::vector<std::complex<double>>& state,
    int /*n_qubits*/,
    int target
) {
    const std::size_t dim = state.size();
    const std::size_t bit = static_cast<std::size_t>(1) << target;
    for (std::size_t i = 0; i < dim; ++i) {
        if ((i & bit) != 0) {
            state[i] = -state[i];
        }
    }
}

char sample_pauli(
    const SingleQubitPauliConfig& cfg,
    RandomStream& rng
) {
    const double px = cfg.px;
    const double py = cfg.py;
    const double pz = cfg.pz;
    const double sum = px + py + pz;
    if (sum <= 0.0) {
        return 'I';
    }
    const double r = rng.uniform(0.0, 1.0);
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

double sample_phase_angle(double magnitude, RandomStream& rng) {
    if (magnitude <= 0.0) {
        return 0.0;
    }
    const double r = rng.uniform(0.0, 1.0);
    return (2.0 * r - 1.0) * magnitude;
}

void apply_phase_rotation(
    std::vector<std::complex<double>>& state,
    int n_qubits,
    int target,
    double theta
) {
    if (theta == 0.0) {
        return;
    }
    const std::size_t dim = state.size();
    const std::size_t bit = static_cast<std::size_t>(1) << target;
    const double half = 0.5 * theta;
    const std::complex<double> phase0(std::cos(-half), std::sin(-half));
    const std::complex<double> phase1(std::cos(half), std::sin(half));
    (void)n_qubits;
    for (std::size_t i = 0; i < dim; ++i) {
        if ((i & bit) == 0) {
            state[i] *= phase0;
        } else {
            state[i] *= phase1;
        }
    }
}
