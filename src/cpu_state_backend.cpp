#include "cpu_state_backend.hpp"

#include <algorithm>
#include <stdexcept>

void CpuStateBackend::alloc_array(int n) {
    if (n <= 0) {
        throw std::invalid_argument("AllocArray requires positive number of qubits");
    }
    n_qubits_ = n;
    const std::size_t dim = static_cast<std::size_t>(1) << n;
    state_.assign(dim, std::complex<double>{0.0, 0.0});
    state_[0] = std::complex<double>{1.0, 0.0};
}

int CpuStateBackend::num_qubits() const {
    return n_qubits_;
}

std::vector<std::complex<double>>& CpuStateBackend::state() {
    return state_;
}

const std::vector<std::complex<double>>& CpuStateBackend::state() const {
    return state_;
}

void CpuStateBackend::apply_single_qubit_unitary(
    int q,
    const std::array<std::complex<double>, 4>& U
) {
    if (q < 0 || q >= n_qubits_) {
        throw std::out_of_range("Invalid qubit index");
    }
    const std::size_t dim = state_.size();
    const std::size_t bit = static_cast<std::size_t>(1) << q;
    for (std::size_t i = 0; i < dim; ++i) {
        if ((i & bit) == 0) {
            const std::size_t j = i | bit;
            const auto a0 = state_[i];
            const auto a1 = state_[j];
            state_[i] = U[0] * a0 + U[1] * a1;
            state_[j] = U[2] * a0 + U[3] * a1;
        }
    }
}

void CpuStateBackend::apply_two_qubit_unitary(
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
    if (q0 < 0 || q1 < 0 || q0 >= n_qubits_ || q1 >= n_qubits_) {
        throw std::out_of_range("Invalid qubit index");
    }
    const std::size_t dim = state_.size();
    const std::size_t b0 = static_cast<std::size_t>(1) << q0;
    const std::size_t b1 = static_cast<std::size_t>(1) << q1;

    for (std::size_t i = 0; i < dim; ++i) {
        if (((i & b0) == 0) && ((i & b1) == 0)) {
            const std::size_t i01 = i | b0;
            const std::size_t i10 = i | b1;
            const std::size_t i11 = i | b0 | b1;

            const auto a00 = state_[i];
            const auto a01 = state_[i01];
            const auto a10 = state_[i10];
            const auto a11 = state_[i11];

            const std::array<std::complex<double>, 4> in = {a00, a01, a10, a11};
            std::array<std::complex<double>, 4> out{};

            for (int row = 0; row < 4; ++row) {
                out[row] = std::complex<double>{0.0, 0.0};
                for (int col = 0; col < 4; ++col) {
                    out[row] += U[4 * row + col] * in[col];
                }
            }

            state_[i] = out[0];
            state_[i01] = out[1];
            state_[i10] = out[2];
            state_[i11] = out[3];
        }
    }
}
