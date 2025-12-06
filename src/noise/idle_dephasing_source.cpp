#include "noise/idle_dephasing_source.hpp"

#include <cmath>

#include "noise/pauli_utils.hpp"

IdleDephasingSource::IdleDephasingSource(double idle_rate)
    : idle_rate_(idle_rate) {}

std::shared_ptr<const NoiseEngine> IdleDephasingSource::clone() const {
    return std::make_shared<IdleDephasingSource>(*this);
}

void IdleDephasingSource::apply_idle_noise(
    int n_qubits,
    std::vector<std::complex<double>>& amplitudes,
    double duration,
    RandomStream& rng
) const {
    if (idle_rate_ <= 0.0 || duration <= 0.0) {
        return;
    }
    const double probability = 1.0 - std::exp(-idle_rate_ * duration);
    if (probability <= 0.0) {
        return;
    }
    for (int q = 0; q < n_qubits; ++q) {
        if (rng.uniform(0.0, 1.0) < probability) {
            apply_pauli_z(amplitudes, n_qubits, q);
        }
    }
}
