#include "oneapi_state_backend.hpp"

#include <algorithm>
#include <stdexcept>

#ifdef NA_VM_WITH_ONEAPI
#include <sycl/sycl.hpp>
#endif

#ifndef NA_VM_WITH_ONEAPI
OneApiStateBackend::OneApiStateBackend() {}

void OneApiStateBackend::alloc_array(int) {
    throw std::runtime_error("oneAPI backend is not enabled; rebuild with NA_VM_WITH_ONEAPI=ON");
}

int OneApiStateBackend::num_qubits() const { return 0; }

std::vector<std::complex<double>>& OneApiStateBackend::state() { return host_state_; }

const std::vector<std::complex<double>>& OneApiStateBackend::state() const { return host_state_; }

void OneApiStateBackend::apply_single_qubit_unitary(
    int,
    const std::array<std::complex<double>, 4>&
) {
    throw std::runtime_error("oneAPI backend is not enabled");
}

void OneApiStateBackend::apply_two_qubit_unitary(
    int,
    int,
    const std::array<std::complex<double>, 16>&
) {
    throw std::runtime_error("oneAPI backend is not enabled");
}

void OneApiStateBackend::sync_host_to_device() {}
void OneApiStateBackend::sync_device_to_host() {}
#else
OneApiStateBackend::OneApiStateBackend()
    : queue_(sycl::default_selector{}),
      state_buffer_(sycl::range<1>(1)) {}

void OneApiStateBackend::alloc_array(int n) {
    if (n <= 0) {
        throw std::invalid_argument("AllocArray requires positive number of qubits");
    }
    n_qubits_ = n;
    const std::size_t dim = static_cast<std::size_t>(1) << n;
    host_state_.assign(dim, std::complex<double>{0.0, 0.0});
    host_state_[0] = std::complex<double>{1.0, 0.0};
    state_buffer_ = sycl::buffer<std::complex<double>, 1>(sycl::range<1>(dim));
    sync_host_to_device();
}

int OneApiStateBackend::num_qubits() const {
    return n_qubits_;
}

std::vector<std::complex<double>>& OneApiStateBackend::state() {
    return host_state_;
}

const std::vector<std::complex<double>>& OneApiStateBackend::state() const {
    return host_state_;
}

void OneApiStateBackend::apply_single_qubit_unitary(
    int q,
    const std::array<std::complex<double>, 4>& U
) {
    if (q < 0 || q >= n_qubits_) {
        throw std::out_of_range("Invalid qubit index");
    }
    const std::size_t dim = host_state_.size();
    const std::size_t bit = static_cast<std::size_t>(1) << q;
    sync_host_to_device();
    queue_.submit([&](sycl::handler& cgh) {
        auto acc = state_buffer_.template get_access<sycl::access::mode::read_write>(cgh);
        cgh.parallel_for(sycl::range<1>(dim), [=](sycl::id<1> idx) {
            const std::size_t i = idx[0];
            if ((i & bit) == 0) {
                const std::size_t j = i | bit;
                const std::complex<double> a0 = acc[i];
                const std::complex<double> a1 = acc[j];
                acc[i] = U[0] * a0 + U[1] * a1;
                acc[j] = U[2] * a0 + U[3] * a1;
            }
        });
    });
    queue_.wait();
    sync_device_to_host();
}

void OneApiStateBackend::apply_two_qubit_unitary(
    int q0,
    int q1,
    const std::array<std::complex<double>, 16>& U
) {
    if (q0 == q1) {
        throw std::invalid_argument("Two-qubit gate requires distinct targets");
    }
    if (q0 > q1) {
        std::swap(q0, q1);
    }
    if (q0 < 0 || q1 < 0 || q0 >= n_qubits_ || q1 >= n_qubits_) {
        throw std::out_of_range("Invalid qubit index");
    }
    const std::size_t dim = host_state_.size();
    const std::size_t b0 = static_cast<std::size_t>(1) << q0;
    const std::size_t b1 = static_cast<std::size_t>(1) << q1;
    sync_host_to_device();
    queue_.submit([&](sycl::handler& cgh) {
        auto acc = state_buffer_.template get_access<sycl::access::mode::read_write>(cgh);
        cgh.parallel_for(sycl::range<1>(dim), [=](sycl::id<1> idx) {
            const std::size_t i = idx[0];
            if (((i & b0) == 0) && ((i & b1) == 0)) {
                const std::size_t i01 = i | b0;
                const std::size_t i10 = i | b1;
                const std::size_t i11 = i | b0 | b1;
                std::array<std::complex<double>, 4> elems = {
                    acc[i], acc[i01], acc[i10], acc[i11]
                };
                std::array<std::complex<double>, 4> out{};
                for (int row = 0; row < 4; ++row) {
                    out[row] = std::complex<double>{0.0, 0.0};
                    for (int col = 0; col < 4; ++col) {
                        out[row] += U[4 * row + col] * elems[col];
                    }
                }
                acc[i] = out[0];
                acc[i01] = out[1];
                acc[i10] = out[2];
                acc[i11] = out[3];
            }
        });
    });
    queue_.wait();
    sync_device_to_host();
}

void OneApiStateBackend::sync_host_to_device() {
    if (n_qubits_ == 0) {
        return;
    }
    queue_.submit([&](sycl::handler& cgh) {
        auto acc = state_buffer_.template get_access<sycl::access::mode::write>(cgh);
        cgh.copy(host_state_.data(), acc);
    });
    queue_.wait();
}

void OneApiStateBackend::sync_device_to_host() {
    if (n_qubits_ == 0) {
        return;
    }
    queue_.submit([&](sycl::handler& cgh) {
        auto acc = state_buffer_.template get_access<sycl::access::mode::read>(cgh);
        cgh.copy(acc, host_state_.data());
    });
    queue_.wait();
}
#endif
