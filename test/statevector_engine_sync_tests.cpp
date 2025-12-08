#include "engine_statevector.hpp"

#include <gtest/gtest.h>

namespace {

struct TrackingBackend : StateBackend {
  public:
    int sync_host_to_device_calls = 0;
    int sync_device_to_host_calls = 0;

    void alloc_array(int n) override {
        n_qubits_ = n;
        const std::size_t dim = static_cast<std::size_t>(1) << n;
        host_state_.assign(dim, std::complex<double>{0.0, 0.0});
        if (!host_state_.empty()) {
            host_state_[0] = std::complex<double>{1.0, 0.0};
        }
    }

    int num_qubits() const override {
        return n_qubits_;
    }

    std::vector<std::complex<double>>& state() override {
        return host_state_;
    }

    const std::vector<std::complex<double>>& state() const override {
        return host_state_;
    }

    void apply_single_qubit_unitary(int, const std::array<std::complex<double>, 4>&) override {}
    void apply_two_qubit_unitary(int, int, const std::array<std::complex<double>, 16>&) override {}

    void sync_host_to_device() override {
        ++sync_host_to_device_calls;
    }

    void sync_device_to_host() override {
        ++sync_device_to_host_calls;
    }

  private:
    std::vector<std::complex<double>> host_state_;
    int n_qubits_ = 0;
};

HardwareConfig make_simple_config() {
    return HardwareConfig{};
}

TEST(StatevectorEngineSync, ApplyGateWithoutNoiseSkipsDeviceSync) {
    auto backend = std::make_unique<TrackingBackend>();
    auto* tracker = backend.get();
    StatevectorEngine engine(make_simple_config(), std::move(backend));

    std::vector<Instruction> program = {
        {Op::AllocArray, 3},
        {Op::ApplyGate, Gate{"H", {0}}},
        {Op::ApplyGate, Gate{"X", {1}}},
    };

    engine.run(program);

    EXPECT_EQ(1, tracker->sync_host_to_device_calls);
    EXPECT_EQ(0, tracker->sync_device_to_host_calls);
}

}  // namespace
