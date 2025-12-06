#pragma once

#include "noise.hpp"

class TwoQubitPauliSource : public NoiseEngine {
  public:
    TwoQubitPauliSource(
        SingleQubitPauliConfig control,
        SingleQubitPauliConfig target
    );

    std::shared_ptr<const NoiseEngine> clone() const override;

    void apply_two_qubit_gate_noise(
        int q0,
        int q1,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        RandomStream& rng
    ) const override;

  private:
    SingleQubitPauliConfig control_;
    SingleQubitPauliConfig target_;
};
