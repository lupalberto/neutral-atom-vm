#pragma once

#ifdef NA_VM_WITH_ONEAPI
#include <sycl/sycl.hpp>
#endif

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace neutral_atom_vm::noise {

#ifdef NA_VM_WITH_ONEAPI
struct DeviceRngState {
    std::uint64_t state = 0xdeadbeefcafebabeULL;

    std::uint64_t next_u64() {
        // simple xorshift64
        std::uint64_t x = state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        state = x;
        return x;
    }

    double uniform() {
        return std::fmod(next_u64() / static_cast<double>(std::numeric_limits<std::uint64_t>::max()), 1.0);
    }
};

struct DeviceNoiseContext {
    int n_qubits = 0;
    int shot_index = 0;
    int gate_index = 0;
    std::size_t shot_stride = 0;
    int n_shots = 1;
    DeviceRngState rng{};
};

class DeviceNoiseEngine {
  public:
    virtual ~DeviceNoiseEngine() = default;

    virtual void apply_single_qubit_gate_noise(
        sycl::queue& queue,
        sycl::buffer<std::complex<double>, 1>& state,
        const DeviceNoiseContext& context,
        int target
    ) const {}

    virtual void apply_two_qubit_gate_noise(
        sycl::queue& queue,
        sycl::buffer<std::complex<double>, 1>& state,
        const DeviceNoiseContext& context,
        int control,
        int target
    ) const {}

    virtual void apply_idle_noise(
        sycl::queue& queue,
        sycl::buffer<std::complex<double>, 1>& state,
        const DeviceNoiseContext& context,
        double duration_ns
    ) const {}
};

class CompositeDeviceNoiseEngine : public DeviceNoiseEngine {
  public:
    explicit CompositeDeviceNoiseEngine(std::vector<std::shared_ptr<const DeviceNoiseEngine>> sources)
        : sources_(std::move(sources)) {}

    void apply_single_qubit_gate_noise(
        sycl::queue& queue,
        sycl::buffer<std::complex<double>, 1>& state,
        const DeviceNoiseContext& context,
        int target
    ) const override {
        for (const auto& src : sources_) {
            if (src) {
                src->apply_single_qubit_gate_noise(queue, state, context, target);
            }
        }
    }

    void apply_two_qubit_gate_noise(
        sycl::queue& queue,
        sycl::buffer<std::complex<double>, 1>& state,
        const DeviceNoiseContext& context,
        int control,
        int target
    ) const override {
        for (const auto& src : sources_) {
            if (src) {
                src->apply_two_qubit_gate_noise(queue, state, context, control, target);
            }
        }
    }

    void apply_idle_noise(
        sycl::queue& queue,
        sycl::buffer<std::complex<double>, 1>& state,
        const DeviceNoiseContext& context,
        double duration_ns
    ) const override {
        for (const auto& src : sources_) {
            if (src) {
                src->apply_idle_noise(queue, state, context, duration_ns);
            }
        }
    }

  private:
    std::vector<std::shared_ptr<const DeviceNoiseEngine>> sources_;
};
#endif

#ifndef NA_VM_WITH_ONEAPI
class DeviceNoiseEngine;
#endif

}  // namespace neutral_atom_vm::noise
