#pragma once

#include <vector>

#include "noise.hpp"

class LossTrackingSource : public NoiseEngine {
  public:
    LossTrackingSource(double measurement_loss, LossRuntimeConfig cfg);

    std::shared_ptr<const NoiseEngine> clone() const override;

    void apply_single_qubit_gate_noise(
        int target,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        RandomStream& rng
    ) const override;

    void apply_two_qubit_gate_noise(
        int q0,
        int q1,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        RandomStream& rng
    ) const override;

    void apply_idle_noise(
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        double duration,
        RandomStream& rng
    ) const override;

    void apply_measurement_noise(
        MeasurementRecord& record,
        RandomStream& rng
    ) const override;

  private:
    double measurement_loss_;
    LossRuntimeConfig cfg_;
    mutable std::vector<bool> lost_;

    void ensure_size(int n_qubits) const;
    void ensure_target(int q) const;
    void maybe_mark_loss(int q, double probability, RandomStream& rng, const char* context) const;
};
