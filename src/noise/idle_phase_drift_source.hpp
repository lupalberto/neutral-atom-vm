#pragma once

#include "noise.hpp"

class IdlePhaseDriftSource : public NoiseEngine {
  public:
    explicit IdlePhaseDriftSource(double rate);

    std::shared_ptr<const NoiseEngine> clone() const override;

    void apply_idle_noise(
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        double duration,
        RandomStream& rng
    ) const override;

  private:
    double rate_;
};
