#include "hardware_vm.hpp"

#include <algorithm>
#include <stdexcept>

HardwareVM::HardwareVM(DeviceProfile profile)
    : profile_(std::move(profile)) {
    if (!is_supported_isa_version(profile_.isa_version)) {
        throw std::runtime_error(
            "Unsupported ISA version " + to_string(profile_.isa_version) +
            " (supported: " + supported_versions_to_string() + ")"
        );
    }
}

std::vector<MeasurementRecord> HardwareVM::run(
    const std::vector<Instruction>& program,
    int shots
) {
    if (!is_supported_isa_version(profile_.isa_version)) {
        throw std::runtime_error(
            "Unsupported ISA version " + to_string(profile_.isa_version) +
            " (supported: " + supported_versions_to_string() + ")"
        );
    }

    const int num_shots = std::max(1, shots);
    std::vector<MeasurementRecord> all_measurements;

    for (int shot = 0; shot < num_shots; ++shot) {
        // Fresh hardware config per shot so that stateful instructions
        // (e.g., MoveAtom) do not leak across repetitions.
        HardwareConfig hw = profile_.hardware;
        StatevectorEngine engine(hw);
        if (profile_.noise_engine) {
            engine.set_noise_model(profile_.noise_engine);
        }
        engine.run(program);
        const auto& measurements = engine.state().measurements;
        all_measurements.insert(
            all_measurements.end(), measurements.begin(), measurements.end());
    }

    return all_measurements;
}
