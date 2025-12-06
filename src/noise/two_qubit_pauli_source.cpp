#include "noise/two_qubit_pauli_source.hpp"

#include "noise/pauli_utils.hpp"

TwoQubitPauliSource::TwoQubitPauliSource(
    SingleQubitPauliConfig control,
    SingleQubitPauliConfig target
)
    : control_(control)
    , target_(target) {}

std::shared_ptr<const NoiseEngine> TwoQubitPauliSource::clone() const {
    return std::make_shared<TwoQubitPauliSource>(*this);
}

void TwoQubitPauliSource::apply_two_qubit_gate_noise(
    int q0,
    int q1,
    int n_qubits,
    std::vector<std::complex<double>>& amplitudes,
    RandomStream& rng
) const {
    if (control_.px + control_.py + control_.pz > 0.0) {
        const char p0 = sample_pauli(control_, rng);
        apply_single_qubit_pauli(p0, amplitudes, n_qubits, q0);
    }
    if (target_.px + target_.py + target_.pz > 0.0) {
        const char p1 = sample_pauli(target_, rng);
        apply_single_qubit_pauli(p1, amplitudes, n_qubits, q1);
    }
}
