#include "noise/measurement_noise_source.hpp"

MeasurementNoiseSource::MeasurementNoiseSource(
    double p_quantum_flip,
    MeasurementNoiseConfig readout
)
    : p_quantum_flip_(p_quantum_flip)
    , readout_(readout) {}

std::shared_ptr<const NoiseEngine> MeasurementNoiseSource::clone() const {
    return std::make_shared<MeasurementNoiseSource>(*this);
}

void MeasurementNoiseSource::apply_measurement_noise(
    MeasurementRecord& record,
    RandomStream& rng
) const {
    const bool has_quantum = p_quantum_flip_ > 0.0;
    const bool has_readout =
        readout_.p_flip0_to_1 > 0.0 || readout_.p_flip1_to_0 > 0.0;

    if (!has_quantum && !has_readout) {
        return;
    }

    for (int& bit : record.bits) {
        if (bit == -1) {
            continue;
        }

        if (has_quantum) {
            if (rng.uniform(0.0, 1.0) < p_quantum_flip_) {
                bit = (bit == 0) ? 1 : 0;
            }
        }

        if (has_readout) {
            const double r = rng.uniform(0.0, 1.0);
            if (bit == 0) {
                if (r < readout_.p_flip0_to_1) {
                    bit = 1;
                }
            } else if (bit == 1) {
                if (r < readout_.p_flip1_to_0) {
                    bit = 0;
                }
            }
        }
    }
}
