#include "noise/correlated_pauli_source.hpp"

#include "noise/pauli_utils.hpp"

#include <numeric>

namespace {

constexpr char kPaulis[4] = {'I', 'X', 'Y', 'Z'};

}

CorrelatedPauliSource::CorrelatedPauliSource(
    TwoQubitCorrelatedPauliConfig cfg
)
    : cfg_(cfg) {}

std::shared_ptr<const NoiseEngine> CorrelatedPauliSource::clone() const {
    return std::make_shared<CorrelatedPauliSource>(*this);
}

void CorrelatedPauliSource::apply_two_qubit_gate_noise(
    int q0,
    int q1,
    int n_qubits,
    std::vector<std::complex<double>>& amplitudes,
    RandomStream& rng
) const {
    double cumulative = 0.0;
    const double total = std::accumulate(
        cfg_.matrix.begin(), cfg_.matrix.end(), 0.0
    );
    if (total <= 0.0) {
        return;
    }
    const double r = rng.uniform(0.0, 1.0);
    for (int ctrl = 0; ctrl < 4; ++ctrl) {
        for (int tgt = 0; tgt < 4; ++tgt) {
            const double p = cfg_.matrix[4 * ctrl + tgt];
            if (p <= 0.0) {
                continue;
            }
            cumulative += p;
            if (r < cumulative) {
                apply_single_qubit_pauli(kPaulis[ctrl], amplitudes, n_qubits, q0);
                apply_single_qubit_pauli(kPaulis[tgt], amplitudes, n_qubits, q1);
                return;
            }
        }
    }
}
