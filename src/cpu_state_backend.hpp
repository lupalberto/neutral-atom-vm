#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <vector>

#include "state_backend.hpp"

class CpuStateBackend : public StateBackend {
  public:
    CpuStateBackend() = default;

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

    void sync_host_to_device() override {}
    void sync_device_to_host() override {}

  private:
    int n_qubits_{0};
    std::vector<std::complex<double>> state_;
};
