#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <vector>

#include "state_backend.hpp"

#ifdef NA_VM_WITH_ONEAPI
#include <sycl/sycl.hpp>
#endif

class OneApiStateBackend : public StateBackend {
  public:
    OneApiStateBackend();

    void alloc_array(int n) override;
    int num_qubits() const override;

    std::vector<std::complex<double>>& state() override;
    const std::vector<std::complex<double>>& state() const override;

    void apply_single_qubit_unitary(
        int q,
        const std::array<std::complex<double>, 4>& U
    ) override;

    void apply_two_qubit_unitary(
        int q0,
        int q1,
        const std::array<std::complex<double>, 16>& U
    ) override;

    void sync_host_to_device() override;
    void sync_device_to_host() override;

  private:
    std::vector<std::complex<double>> host_state_;
    int n_qubits_{0};
#ifdef NA_VM_WITH_ONEAPI
    sycl::queue queue_;
    sycl::buffer<std::complex<double>, 1> state_buffer_;
#endif
};
