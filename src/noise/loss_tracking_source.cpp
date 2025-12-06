#include "noise/loss_tracking_source.hpp"

#include <cmath>

LossTrackingSource::LossTrackingSource(
    double measurement_loss,
    LossRuntimeConfig cfg
)
    : measurement_loss_(measurement_loss)
    , cfg_(cfg) {}

std::shared_ptr<const NoiseEngine> LossTrackingSource::clone() const {
    return std::make_shared<LossTrackingSource>(*this);
}

void LossTrackingSource::apply_single_qubit_gate_noise(
    int target,
    int n_qubits,
    std::vector<std::complex<double>>& /*amplitudes*/,
    RandomStream& rng
) const {
    ensure_size(n_qubits);
    maybe_mark_loss(target, cfg_.per_gate, rng);
}

void LossTrackingSource::apply_two_qubit_gate_noise(
    int q0,
    int q1,
    int n_qubits,
    std::vector<std::complex<double>>& /*amplitudes*/,
    RandomStream& rng
) const {
    ensure_size(n_qubits);
    maybe_mark_loss(q0, cfg_.per_gate, rng);
    maybe_mark_loss(q1, cfg_.per_gate, rng);
}

void LossTrackingSource::apply_idle_noise(
    int n_qubits,
    std::vector<std::complex<double>>& /*amplitudes*/,
    double duration,
    RandomStream& rng
) const {
    ensure_size(n_qubits);
    if (cfg_.idle_rate <= 0.0 || duration <= 0.0) {
        return;
    }
    const double probability = 1.0 - std::exp(-cfg_.idle_rate * duration);
    for (int q = 0; q < n_qubits; ++q) {
        maybe_mark_loss(q, probability, rng);
    }
}

void LossTrackingSource::apply_measurement_noise(
    MeasurementRecord& record,
    RandomStream& rng
) const {
    for (std::size_t idx = 0; idx < record.targets.size(); ++idx) {
        const int q = record.targets[idx];
        ensure_target(q);
        if (q >= 0 && lost_[q]) {
            record.bits[idx] = -1;
            continue;
        }
        if (measurement_loss_ > 0.0) {
            const double r = rng.uniform(0.0, 1.0);
            if (r < measurement_loss_) {
                if (q >= 0) {
                    lost_[q] = true;
                }
                record.bits[idx] = -1;
            }
        }
    }
}

void LossTrackingSource::ensure_size(int n_qubits) const {
    if (n_qubits <= 0) {
        return;
    }
    if (static_cast<int>(lost_.size()) < n_qubits) {
        lost_.resize(static_cast<std::size_t>(n_qubits), false);
    }
}

void LossTrackingSource::ensure_target(int q) const {
    if (q < 0) {
        return;
    }
    if (q >= static_cast<int>(lost_.size())) {
        lost_.resize(static_cast<std::size_t>(q + 1), false);
    }
}

void LossTrackingSource::maybe_mark_loss(
    int q,
    double probability,
    RandomStream& rng
) const {
    if (probability <= 0.0 || q < 0) {
        return;
    }
    if (q >= static_cast<int>(lost_.size())) {
        lost_.resize(static_cast<std::size_t>(q + 1), false);
    }
    if (lost_[q]) {
        return;
    }
    if (rng.uniform(0.0, 1.0) < probability) {
        lost_[q] = true;
    }
}
