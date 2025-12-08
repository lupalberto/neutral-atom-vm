#ifdef NA_VM_WITH_ONEAPI
#include "engine_statevector.hpp"
#include "hardware_vm.hpp"
#include "oneapi_state_backend.hpp"

#include <gtest/gtest.h>
#include <cmath>

using neutral_atom_vm::BackendKind;
using neutral_atom_vm::DeviceProfile;
using neutral_atom_vm::HardwareConfig;
using neutral_atom_vm::HardwareVM;
using neutral_atom_vm::Instruction;
using neutral_atom_vm::Op;
using neutral_atom_vm::Gate;

TEST(OneApiGpuMeasurement, CollapsesToSampledOutcome) {
    HardwareConfig cfg;
    auto backend = std::make_unique<OneApiStateBackend>();
    StatevectorEngine engine(cfg, std::move(backend));

    std::vector<Instruction> program = {
        {Op::AllocArray, 1},
        {Op::ApplyGate, Gate{"H", {0}}},
        {Op::Measure, std::vector<int>{0}},
    };

    engine.run(program);
    ASSERT_FALSE(engine.state().measurements.empty());
    const auto& record = engine.state().measurements.back();
    EXPECT_EQ(record.targets, std::vector<int>{0});
    ASSERT_EQ(record.bits.size(), 1);

    const int measured_bit = record.bits[0];
    auto state = engine.state_vector();
    ASSERT_EQ(state.size(), 2);
    if (measured_bit == 0) {
        EXPECT_NEAR(std::abs(state[0]), 1.0, 1e-12);
        EXPECT_NEAR(std::abs(state[1]), 0.0, 1e-12);
    } else {
        EXPECT_NEAR(std::abs(state[1]), 1.0, 1e-12);
        EXPECT_NEAR(std::abs(state[0]), 0.0, 1e-12);
    }
}

TEST(OneApiGpuMeasurement, BatchedShotsProduceAllRecords) {
    DeviceProfile profile;
    profile.backend = BackendKind::kOneApi;
    profile.hardware.positions = {0.0};

    std::vector<Instruction> program = {
        {Op::AllocArray, 1},
        {Op::ApplyGate, Gate{"H", {0}}},
        {Op::Measure, std::vector<int>{0}},
    };

    HardwareVM vm(profile);
    std::vector<std::uint64_t> seeds = {11ull, 22ull, 33ull};
    const auto result = vm.run(program, static_cast<int>(seeds.size()), seeds);

    ASSERT_EQ(result.measurements.size(), seeds.size());
    for (const auto& record : result.measurements) {
        EXPECT_EQ(record.targets, std::vector<int>{0});
        ASSERT_EQ(record.bits.size(), 1u);
        EXPECT_TRUE(record.bits[0] == 0 || record.bits[0] == 1);
    }
}

#endif
