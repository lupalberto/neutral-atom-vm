#pragma once

#include "noise.hpp"

class SingleQubitPauliSource : public NoiseEngine {
  public:
    explicit SingleQubitPauliSource(SingleQubitPauliConfig cfg);

    std::shared_ptr<const NoiseEngine> clone() const override;

    void apply_single_qubit_gate_noise(
        int target,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        RandomStream& rng
    ) const override;

  private:
    SingleQubitPauliConfig cfg_;
};
