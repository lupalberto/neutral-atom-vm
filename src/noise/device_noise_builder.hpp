#pragma once

#include <memory>

#include "noise/device_noise.hpp"
#include "noise.hpp"

namespace neutral_atom_vm::noise {

std::shared_ptr<const DeviceNoiseEngine> build_device_noise_engine(
    const SimpleNoiseConfig& config
);

}  // namespace neutral_atom_vm::noise
