#include "noise/idle_phase_drift_source.hpp"

#include "noise/pauli_utils.hpp"

IdlePhaseDriftSource::IdlePhaseDriftSource(double rate) : rate_(rate) {}

std::shared_ptr<const NoiseEngine> IdlePhaseDriftSource::clone() const {
    return std::make_shared<IdlePhaseDriftSource>(*this);
}

void IdlePhaseDriftSource::apply_idle_noise(
    int n_qubits,
    std::vector<std::complex<double>>& amplitudes,
    double duration,
    RandomStream& rng
) const {
    if (rate_ <= 0.0 || duration <= 0.0) {
        return;
    }
    const double magnitude = rate_ * duration;
    for (int q = 0; q < n_qubits; ++q) {
        const double theta = sample_phase_angle(magnitude, rng);
        apply_phase_rotation(amplitudes, n_qubits, q, theta);
    }
}
