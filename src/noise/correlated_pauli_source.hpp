#pragma once

#include "noise.hpp"

class CorrelatedPauliSource : public NoiseEngine {
  public:
    explicit CorrelatedPauliSource(TwoQubitCorrelatedPauliConfig cfg);

    std::shared_ptr<const NoiseEngine> clone() const override;

    void apply_two_qubit_gate_noise(
        int q0,
        int q1,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        RandomStream& rng
    ) const override;

  private:
    TwoQubitCorrelatedPauliConfig cfg_;
};
