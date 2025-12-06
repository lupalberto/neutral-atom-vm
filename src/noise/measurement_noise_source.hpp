#pragma once

#include "noise.hpp"

class MeasurementNoiseSource : public NoiseEngine {
  public:
    MeasurementNoiseSource(
        double p_quantum_flip,
        MeasurementNoiseConfig readout
    );

    std::shared_ptr<const NoiseEngine> clone() const override;

    void apply_measurement_noise(
        MeasurementRecord& record,
        RandomStream& rng
    ) const override;

  private:
    double p_quantum_flip_;
    MeasurementNoiseConfig readout_;
};
