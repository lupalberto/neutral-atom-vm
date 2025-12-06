#include "noise/phase_kick_noise_source.hpp"

#include "noise/pauli_utils.hpp"

PhaseKickNoiseSource::PhaseKickNoiseSource(PhaseNoiseConfig cfg)
    : cfg_(cfg) {}

std::shared_ptr<const NoiseEngine> PhaseKickNoiseSource::clone() const {
    return std::make_shared<PhaseKickNoiseSource>(*this);
}

void PhaseKickNoiseSource::apply_single_qubit_gate_noise(
    int target,
    int n_qubits,
    std::vector<std::complex<double>>& amplitudes,
    RandomStream& rng
) const {
    apply_phase_if_needed(cfg_.single_qubit, target, n_qubits, amplitudes, rng);
}

void PhaseKickNoiseSource::apply_two_qubit_gate_noise(
    int q0,
    int q1,
    int n_qubits,
    std::vector<std::complex<double>>& amplitudes,
    RandomStream& rng
) const {
    apply_phase_if_needed(cfg_.two_qubit_control, q0, n_qubits, amplitudes, rng);
    apply_phase_if_needed(cfg_.two_qubit_target, q1, n_qubits, amplitudes, rng);
}

void PhaseKickNoiseSource::apply_phase_if_needed(
    double magnitude,
    int target,
    int n_qubits,
    std::vector<std::complex<double>>& amplitudes,
    RandomStream& rng
) const {
    if (magnitude <= 0.0) {
        return;
    }
    const double theta = sample_phase_angle(magnitude, rng);
    apply_phase_rotation(amplitudes, n_qubits, target, theta);
}
