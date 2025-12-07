#include "noise/amplitude_damping_source.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

AmplitudeDampingSource::AmplitudeDampingSource(AmplitudeDampingConfig config)
    : config_(std::move(config)) {}

std::shared_ptr<const NoiseEngine> AmplitudeDampingSource::clone() const {
    return std::make_shared<AmplitudeDampingSource>(config_);
}

void AmplitudeDampingSource::apply_single_qubit_gate_noise(
    int target,
    int /*n_qubits*/,
    std::vector<std::complex<double>>& amplitudes,
    RandomStream& /*rng*/
) const {
    if (config_.per_gate <= 0.0) {
        return;
    }
    const double gamma = std::clamp(config_.per_gate, 0.0, 1.0);
    apply_to_qubit(target, amplitudes, gamma);
}

void AmplitudeDampingSource::apply_idle_noise(
    int n_qubits,
    std::vector<std::complex<double>>& amplitudes,
    double duration,
    RandomStream& /*rng*/
) const {
    if (config_.idle_rate <= 0.0 || duration <= 0.0) {
        return;
    }
    const double gamma_raw = 1.0 - std::exp(-config_.idle_rate * duration);
    const double gamma = std::clamp(gamma_raw, 0.0, 1.0);
    if (gamma <= 0.0) {
        return;
    }
    for (int target = 0; target < n_qubits; ++target) {
        apply_to_qubit(target, amplitudes, gamma);
    }
}

void AmplitudeDampingSource::apply_to_qubit(
    int target,
    std::vector<std::complex<double>>& amplitudes,
    double gamma
) {
    if (gamma <= 0.0) {
        return;
    }
    const std::size_t dim = amplitudes.size();
    if (dim == 0) {
        return;
    }
    const std::size_t mask = static_cast<std::size_t>(1ull << target);
    const double sqrt_gamma = std::sqrt(gamma);
    const double sqrt_one_minus = std::sqrt(std::max(0.0, 1.0 - gamma));

    for (std::size_t idx = 0; idx < dim; ++idx) {
        if ((idx & mask) != 0) {
            continue;
        }
        const std::size_t idx0 = idx;
        const std::size_t idx1 = idx | mask;
        if (idx1 >= dim) {
            continue;
        }
        const auto a0 = amplitudes[idx0];
        const auto a1 = amplitudes[idx1];
        amplitudes[idx0] = a0 + sqrt_gamma * a1;
        amplitudes[idx1] = sqrt_one_minus * a1;
    }
}
