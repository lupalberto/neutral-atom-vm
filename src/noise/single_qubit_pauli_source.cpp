#include "noise/single_qubit_pauli_source.hpp"

#include "noise/pauli_utils.hpp"

SingleQubitPauliSource::SingleQubitPauliSource(SingleQubitPauliConfig cfg)
    : cfg_(cfg) {}

std::shared_ptr<const NoiseEngine> SingleQubitPauliSource::clone() const {
    return std::make_shared<SingleQubitPauliSource>(*this);
}

void SingleQubitPauliSource::apply_single_qubit_gate_noise(
    int target,
    int n_qubits,
    std::vector<std::complex<double>>& amplitudes,
    RandomStream& rng
) const {
    const double sum = cfg_.px + cfg_.py + cfg_.pz;
    if (sum <= 0.0) {
        return;
    }
    const char pauli = sample_pauli(cfg_, rng);
    apply_single_qubit_pauli(pauli, amplitudes, n_qubits, target);
}
