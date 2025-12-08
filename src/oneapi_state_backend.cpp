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

bool OneApiStateBackend::is_gpu_backend() const {
    return false;
}

void OneApiStateBackend::set_shot_count(int) {
    throw std::runtime_error("oneAPI backend is not enabled; rebuild with NA_VM_WITH_ONEAPI=ON");
}

int OneApiStateBackend::shot_count() const {
    return 1;
}

std::size_t OneApiStateBackend::total_dimension() const {
    return host_state_.size();
}
#else
OneApiStateBackend::OneApiStateBackend()
    : queue_(sycl::default_selector{}),
      state_buffer_(sycl::range<1>(1)),
      num_shots_(1) {}

void OneApiStateBackend::alloc_array(int n) {
    if (n <= 0) {
        throw std::invalid_argument("AllocArray requires positive number of qubits");
    }
    n_qubits_ = n;
    const std::size_t shot_dim = static_cast<std::size_t>(1) << n;
    const std::size_t total_dim = shot_dim * static_cast<std::size_t>(num_shots_);
    host_state_.assign(shot_dim, std::complex<double>{0.0, 0.0});
    host_state_[0] = std::complex<double>{1.0, 0.0};
    state_buffer_ = sycl::buffer<std::complex<double>, 1>(sycl::range<1>(total_dim));
    queue_.submit([&](sycl::handler& cgh) {
        auto acc = state_buffer_.template get_access<sycl::access::mode::write>(cgh);
        cgh.parallel_for(sycl::range<1>(total_dim), [=](sycl::id<1> idx) {
            acc[idx] = std::complex<double>{0.0, 0.0};
        });
    });
    queue_.submit([&](sycl::handler& cgh) {
        auto acc = state_buffer_.template get_access<sycl::access::mode::write>(cgh);
        cgh.parallel_for(sycl::range<1>(static_cast<std::size_t>(num_shots_)), [=](sycl::id<1> shot_id) {
            acc[shot_id[0] * shot_dim] = std::complex<double>{1.0, 0.0};
        });
    });
    queue_.wait();
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
    const std::size_t shot_dim = dimension();
    const std::size_t total_dim = total_dimension();
    const std::size_t bit = static_cast<std::size_t>(1) << q;
    queue_.submit([&](sycl::handler& cgh) {
        auto acc = state_buffer_.template get_access<sycl::access::mode::read_write>(cgh);
        cgh.parallel_for(sycl::range<1>(total_dim), [=](sycl::id<1> idx) {
            const std::size_t flat = idx[0];
            const std::size_t local = flat % shot_dim;
            if ((local & bit) != 0) {
                return;
            }
            const std::size_t shot_base = flat - local;
            const std::size_t partner = shot_base + (local | bit);
            const std::complex<double> a0 = acc[shot_base + local];
            const std::complex<double> a1 = acc[partner];
            acc[shot_base + local] = U[0] * a0 + U[1] * a1;
            acc[partner] = U[2] * a0 + U[3] * a1;
        });
    });
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
    const std::size_t shot_dim = dimension();
    const std::size_t total_dim = total_dimension();
    const std::size_t b0 = static_cast<std::size_t>(1) << q0;
    const std::size_t b1 = static_cast<std::size_t>(1) << q1;
    queue_.submit([&](sycl::handler& cgh) {
        auto acc = state_buffer_.template get_access<sycl::access::mode::read_write>(cgh);
        cgh.parallel_for(sycl::range<1>(total_dim), [=](sycl::id<1> idx) {
            const std::size_t flat = idx[0];
            const std::size_t local = flat % shot_dim;
            if (((local & b0) == 0) && ((local & b1) == 0)) {
                const std::size_t shot_base = flat - local;
                const std::size_t i01 = shot_base + (local | b0);
                const std::size_t i10 = shot_base + (local | b1);
                const std::size_t i11 = shot_base + (local | b0 | b1);
                std::array<std::complex<double>, 4> elems = {
                    acc[shot_base + local], acc[i01], acc[i10], acc[i11]
                };
                std::array<std::complex<double>, 4> out{};
                for (int row = 0; row < 4; ++row) {
                    out[row] = std::complex<double>{0.0, 0.0};
                    for (int col = 0; col < 4; ++col) {
                        out[row] += U[4 * row + col] * elems[col];
                    }
                }
                acc[shot_base + local] = out[0];
                acc[i01] = out[1];
                acc[i10] = out[2];
                acc[i11] = out[3];
            }
        });
    });
}

void OneApiStateBackend::sync_host_to_device() {
    if (n_qubits_ == 0) {
        return;
    }
    const std::size_t shot_dim = dimension();
    queue_.submit([&](sycl::handler& cgh) {
        auto acc = state_buffer_.template get_access<sycl::access::mode::write>(
            cgh,
            sycl::range<1>(shot_dim),
            sycl::id<1>(0)
        );
        cgh.copy(host_state_.data(), acc);
    });
    queue_.wait();
}

void OneApiStateBackend::sync_device_to_host() {
    if (n_qubits_ == 0) {
        return;
    }
    const std::size_t shot_dim = dimension();
    queue_.submit([&](sycl::handler& cgh) {
        auto acc = state_buffer_.template get_access<sycl::access::mode::read>(
            cgh,
            sycl::range<1>(shot_dim),
            sycl::id<1>(0)
        );
        cgh.copy(acc, host_state_.data());
    });
    queue_.wait();
}

bool OneApiStateBackend::is_gpu_backend() const {
    return true;
}

#ifdef NA_VM_WITH_ONEAPI
sycl::buffer<std::complex<double>, 1>& OneApiStateBackend::state_buffer() {
    return state_buffer_;
}

sycl::queue& OneApiStateBackend::queue() {
    return queue_;
}

std::size_t OneApiStateBackend::dimension() const {
    return static_cast<std::size_t>(1) << n_qubits_;
}

void OneApiStateBackend::set_shot_count(int shots) {
    num_shots_ = shots > 0 ? shots : 1;
}

int OneApiStateBackend::shot_count() const {
    return num_shots_;
}

std::size_t OneApiStateBackend::total_dimension() const {
    return dimension() * static_cast<std::size_t>(num_shots_);
}
#endif
#endif
