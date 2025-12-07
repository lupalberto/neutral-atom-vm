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

    for (std::size_t idx = 0; idx < record.bits.size(); ++idx) {
        int& bit = record.bits[idx];
        if (bit == -1) {
            continue;
        }

        const int original = bit;
        if (has_quantum) {
            if (rng.uniform(0.0, 1.0) < p_quantum_flip_) {
                bit = (bit == 0) ? 1 : 0;
                log_event(
                    "Noise",
                    "type=measure_quantum_flip qubit=" + std::to_string(record.targets[idx]) +
                        " before=" + std::to_string(original) +
                        " after=" + std::to_string(bit) +
                        " p_quantum_flip=" + std::to_string(p_quantum_flip_)
                );
            }
        }

        if (has_readout) {
            const double r = rng.uniform(0.0, 1.0);
            if (bit == 0) {
                if (r < readout_.p_flip0_to_1) {
                    bit = 1;
                    log_event(
                        "Noise",
                        "type=measure_readout_flip qubit=" + std::to_string(record.targets[idx]) +
                            " before=0 after=1 p01=" + std::to_string(readout_.p_flip0_to_1)
                    );
                }
            } else if (bit == 1) {
                if (r < readout_.p_flip1_to_0) {
                    bit = 0;
                    log_event(
                        "Noise",
                        "type=measure_readout_flip qubit=" + std::to_string(record.targets[idx]) +
                            " before=1 after=0 p10=" + std::to_string(readout_.p_flip1_to_0)
                    );
                }
            }
        }
    }
}
