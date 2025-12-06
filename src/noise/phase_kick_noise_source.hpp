#pragma once

#include "noise.hpp"

class PhaseKickNoiseSource : public NoiseEngine {
  public:
    explicit PhaseKickNoiseSource(PhaseNoiseConfig cfg);

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

  private:
    PhaseNoiseConfig cfg_;

    void apply_phase_if_needed(
        double magnitude,
        int target,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        RandomStream& rng
    ) const;
};
