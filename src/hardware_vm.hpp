#pragma once

#include <string>
#include <vector>

#include "noise.hpp"
#include "engine_statevector.hpp"

// High-level hardware VM façade that executes ISA programs on a concrete
// backend engine (currently the statevector runtime) using a device profile.

struct DeviceProfile {
    std::string id;
    ISAVersion isa_version = kCurrentISAVersion;
    HardwareConfig hardware;
    // Future extensions: noise configuration, backend kind, resource limits.
};

class HardwareVM {
  public:
    explicit HardwareVM(DeviceProfile profile);

    const DeviceProfile& profile() const { return profile_; }

    // Execute the given program for the requested number of shots using
    // the configured device profile. Returns concatenated measurement
    // records across all shots.
    std::vector<MeasurementRecord> run(
        const std::vector<Instruction>& program,
        int shots = 1
    );

  private:
    DeviceProfile profile_;
};
