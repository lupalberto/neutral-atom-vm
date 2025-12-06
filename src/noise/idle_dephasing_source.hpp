#pragma once

#include "noise.hpp"

class IdleDephasingSource : public NoiseEngine {
  public:
    explicit IdleDephasingSource(double idle_rate);

    std::shared_ptr<const NoiseEngine> clone() const override;

    void apply_idle_noise(
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        double duration,
        RandomStream& rng
    ) const override;

  private:
    double idle_rate_;
};
