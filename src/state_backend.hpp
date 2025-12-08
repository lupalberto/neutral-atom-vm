#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <vector>

#include "vm/isa.hpp"

class StateBackend {
  public:
    virtual ~StateBackend() = default;

    virtual void alloc_array(int n) = 0;
    virtual int num_qubits() const = 0;

    virtual std::vector<std::complex<double>>& state() = 0;
    virtual const std::vector<std::complex<double>>& state() const = 0;

    virtual void apply_single_qubit_unitary(
        int q,
        const std::array<std::complex<double>, 4>& U
    ) = 0;

    virtual void apply_two_qubit_unitary(
        int q0,
        int q1,
        const std::array<std::complex<double>, 16>& U
    ) = 0;

    virtual void sync_host_to_device() {}
    virtual void sync_device_to_host() {}
    virtual bool is_gpu_backend() const { return false; }
};
