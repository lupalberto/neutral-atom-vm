#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "noise.hpp"
#include "noise/device_noise.hpp"
#include "engine_statevector.hpp"
#include "progress_reporter.hpp"

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
    std::shared_ptr<const neutral_atom_vm::noise::DeviceNoiseEngine> device_noise_engine;
    BackendKind backend = BackendKind::kCpu;
    // Future extensions: noise configuration, resource limits, diagnostics.
};

class HardwareVM {
  public:
    explicit HardwareVM(DeviceProfile profile);

    void set_progress_reporter(neutral_atom_vm::ProgressReporter* reporter);

    const DeviceProfile& profile() const { return profile_; }

    // Execute the given program for the requested number of shots using
    // the configured device profile. Returns concatenated measurement
    // records across all shots.
    struct RunResult {
        std::vector<MeasurementRecord> measurements;
        std::vector<ExecutionLog> logs;
    };

    RunResult run(
        const std::vector<Instruction>& program,
        int shots = 1,
        const std::vector<std::uint64_t>& shot_seeds = {},
        std::size_t max_threads = 0
    );

  private:
#ifdef NA_VM_WITH_ONEAPI
    RunResult run_oneapi_batched(
        const std::vector<Instruction>& program,
        int num_shots,
        const std::vector<std::uint64_t>& shot_seeds
    );
#endif
    DeviceProfile profile_;
    neutral_atom_vm::ProgressReporter* progress_reporter_ = nullptr;
};
