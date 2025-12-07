#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "noise.hpp"
#include "engine_statevector.hpp"

// High-level hardware VM façade that executes ISA programs on a concrete
// backend engine (currently the statevector runtime) using a device profile.

enum class BackendKind {
    kCpu,
    kOneApi,
};

struct DeviceProfile {
    std::string id;
    ISAVersion isa_version = kCurrentISAVersion;
    HardwareConfig hardware;
    std::shared_ptr<const NoiseEngine> noise_engine;
    BackendKind backend = BackendKind::kCpu;
    // Future extensions: noise configuration, resource limits, diagnostics.
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
        int shots = 1,
        const std::vector<std::uint64_t>& shot_seeds = {}
    );

  private:
    DeviceProfile profile_;
};
