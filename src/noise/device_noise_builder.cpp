#include "noise/device_noise_builder.hpp"

#ifdef NA_VM_WITH_ONEAPI
#include "noise/device_pauli_noise_source.hpp"
#endif

namespace neutral_atom_vm::noise {

std::shared_ptr<const DeviceNoiseEngine> build_device_noise_engine(
    const SimpleNoiseConfig& config
) {
#ifdef NA_VM_WITH_ONEAPI
    const double single_prob = config.gate.single_qubit.px +
                               config.gate.single_qubit.py +
                               config.gate.single_qubit.pz;
    const double control_prob = config.gate.two_qubit_control.px +
                                config.gate.two_qubit_control.py +
                                config.gate.two_qubit_control.pz;
    const double target_prob = config.gate.two_qubit_target.px +
                               config.gate.two_qubit_target.py +
                               config.gate.two_qubit_target.pz;
    if (single_prob <= 0.0 && control_prob <= 0.0 && target_prob <= 0.0) {
        return nullptr;
    }
    return std::make_shared<DevicePauliNoiseSource>(
        single_prob, control_prob, target_prob);
#else
    (void)config;
    return nullptr;
#endif
}

}  // namespace neutral_atom_vm::noise
