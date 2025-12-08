#pragma once

#ifdef NA_VM_WITH_ONEAPI
#include <cstdint>
#include <sycl/sycl.hpp>
#endif

#include "noise/device_noise.hpp"

namespace neutral_atom_vm::noise {

#ifdef NA_VM_WITH_ONEAPI
class DevicePauliNoiseSource : public DeviceNoiseEngine {
  public:
    explicit DevicePauliNoiseSource(
        double single_qubit_prob,
        double two_qubit_control_prob,
        double two_qubit_target_prob
    );

    void apply_single_qubit_gate_noise(
        sycl::queue& queue,
        sycl::buffer<std::complex<double>, 1>& state,
        const DeviceNoiseContext& context,
        int target
    ) const override;

    void apply_two_qubit_gate_noise(
        sycl::queue& queue,
        sycl::buffer<std::complex<double>, 1>& state,
        const DeviceNoiseContext& context,
        int control,
        int target
    ) const override;

  private:
    void apply_noise(
        sycl::queue& queue,
        sycl::buffer<std::complex<double>, 1>& state,
        const DeviceNoiseContext& context,
        int target,
        double probability
    ) const;

    static std::uint64_t splitmix64(std::uint64_t x);
    static double deterministic_uniform(std::uint64_t base_seed, std::size_t index);

    double single_qubit_prob_;
    double two_control_prob_;
    double two_target_prob_;
};
#endif

}  // namespace neutral_atom_vm::noise
