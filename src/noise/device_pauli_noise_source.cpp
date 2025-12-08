#ifdef NA_VM_WITH_ONEAPI

#include "noise/device_pauli_noise_source.hpp"

#include <limits>

namespace neutral_atom_vm::noise {

DevicePauliNoiseSource::DevicePauliNoiseSource(
    double single_qubit_prob,
    double two_qubit_control_prob,
    double two_qubit_target_prob
) : single_qubit_prob_(single_qubit_prob),
    two_control_prob_(two_qubit_control_prob),
    two_target_prob_(two_qubit_target_prob) {}

void DevicePauliNoiseSource::apply_single_qubit_gate_noise(
    sycl::queue& queue,
    sycl::buffer<std::complex<double>, 1>& state,
    const DeviceNoiseContext& context,
    int target
) const {
    apply_noise(queue, state, context, target, single_qubit_prob_);
}

void DevicePauliNoiseSource::apply_two_qubit_gate_noise(
    sycl::queue& queue,
    sycl::buffer<std::complex<double>, 1>& state,
    const DeviceNoiseContext& context,
    int control,
    int target
) const {
    apply_noise(queue, state, context, control, two_control_prob_);
    apply_noise(queue, state, context, target, two_target_prob_);
}

void DevicePauliNoiseSource::apply_noise(
    sycl::queue& queue,
    sycl::buffer<std::complex<double>, 1>& state,
    const DeviceNoiseContext& context,
    int target,
    double probability
) const {
    if (probability <= 0.0 || context.n_qubits <= 0) {
        return;
    }
    const std::size_t dim = static_cast<std::size_t>(1) << context.n_qubits;
    const std::size_t bit = static_cast<std::size_t>(1) << target;
    const std::uint64_t base_seed =
        context.rng.state ^
        (static_cast<std::uint64_t>(context.gate_index) << 32) ^
        (static_cast<std::uint64_t>(context.shot_index) << 16);

    queue.submit([&](sycl::handler& cgh) {
        auto acc = state.template get_access<sycl::access::mode::read_write>(cgh);
        cgh.parallel_for(sycl::range<1>(dim), [=](sycl::id<1> idx) {
            const std::size_t i = idx[0];
            if ((i & bit) != 0) {
                return;
            }
            const std::size_t j = i | bit;
            const double r = deterministic_uniform(base_seed, i);
            if (r < probability) {
                const auto temp = acc[i];
                acc[i] = acc[j];
                acc[j] = temp;
            }
        });
    });
    queue.wait();
}

std::uint64_t DevicePauliNoiseSource::splitmix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

double DevicePauliNoiseSource::deterministic_uniform(
    std::uint64_t base_seed,
    std::size_t index
) {
    std::uint64_t value = splitmix64(base_seed + static_cast<std::uint64_t>(index));
    constexpr double kScale = 1.0 / static_cast<double>(std::numeric_limits<std::uint64_t>::max());
    return static_cast<double>(value) * kScale;
}

}  // namespace neutral_atom_vm::noise

#endif
