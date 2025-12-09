#include "hardware_vm.hpp"

#include <gtest/gtest.h>

#include <vector>

#include <memory>

namespace {

TEST(HardwareVMTests, AppliesNoiseEngine) {
    DeviceProfile profile;
    profile.id = "noise-test";
    profile.hardware.positions = {0.0};
    profile.hardware.blockade_radius = 1.0;

    SimpleNoiseConfig cfg;
    cfg.p_loss = 1.0;
    profile.noise_engine = std::make_shared<SimpleNoiseEngine>(cfg);

    HardwareVM vm(profile);

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 1});
    program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0},
    });

    const auto result = vm.run(program, 1);
    const auto& measurements = result.measurements;
    ASSERT_EQ(measurements.size(), 1u);
    EXPECT_EQ(measurements[0].bits, std::vector<int>({-1}));
}

TEST(HardwareVMTests, RunsMultipleShots) {
    DeviceProfile profile;
    profile.id = "multi-shot";
    profile.hardware.positions = {0.0, 1.0};
    profile.hardware.blockade_radius = 1.0;

    HardwareVM vm(profile);

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 2});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"H", {0}, 0.0},
    });
    program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0, 1},
    });

    const auto result = vm.run(program, 3);
    EXPECT_EQ(result.measurements.size(), 3u);
}

TEST(HardwareVMTests, IdleNoiseInducesPhaseFlip) {
    DeviceProfile profile;
    profile.id = "idle-noise";
    profile.hardware.positions = {0.0};
    profile.hardware.blockade_radius = 1.0;

    SimpleNoiseConfig cfg;
    cfg.idle_rate = 1000.0;
    profile.noise_engine = std::make_shared<SimpleNoiseEngine>(cfg);

    HardwareVM vm(profile);

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 1});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"H", {0}, 0.0},
    });
    program.push_back(Instruction{
        Op::Wait,
        WaitInstruction{1.0},
    });
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"H", {0}, 0.0},
    });
    program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0},
    });

    const auto result = vm.run(program, 1);
    const auto& measurements = result.measurements;
    ASSERT_EQ(measurements.size(), 1u);
    // With strong idle-phase noise (Z), the H-Z-H sequence acts like X, so measurement yields 1.
    EXPECT_EQ(measurements[0].bits, std::vector<int>({1}));
}

TEST(HardwareVMTests, LossStateResetsEachShot) {
    DeviceProfile profile;
    profile.id = "loss-reset";
    profile.hardware.positions = {0.0};
    profile.hardware.blockade_radius = 1.0;

    SimpleNoiseConfig cfg;
    cfg.loss_runtime.per_gate = 1.0;
    profile.noise_engine = std::make_shared<SimpleNoiseEngine>(cfg);

    HardwareVM vm(profile);

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 1});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"X", {0}, 0.0},
    });
    program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0},
    });

    const auto result = vm.run(program, 2);
    const auto& measurements = result.measurements;
    ASSERT_EQ(measurements.size(), 2u);
    EXPECT_EQ(measurements[0].bits, std::vector<int>({-1}));
    EXPECT_EQ(measurements[1].bits, std::vector<int>({-1}));
}

#ifdef NA_VM_WITH_STIM
TEST(HardwareVMTests, StabilizerBackendGeneratesBellPair) {
    DeviceProfile profile;
    profile.id = "stim-bell";
    profile.hardware.positions = {0.0, 1.0};
    profile.hardware.blockade_radius = 1.0;
    profile.backend = BackendKind::kStabilizer;

    HardwareVM vm(profile);

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 2});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"H", {0}, 0.0},
    });
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"CX", {0, 1}, 0.0},
    });
    program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0, 1},
    });

    const auto result = vm.run(program, 5);
    ASSERT_EQ(result.measurements.size(), 5u);
    for (const auto& record : result.measurements) {
        ASSERT_EQ(record.bits.size(), 2u);
        const int b0 = record.bits[0];
        const int b1 = record.bits[1];
        EXPECT_TRUE((b0 == 0 && b1 == 0) || (b0 == 1 && b1 == 1));
    }
}

TEST(HardwareVMTests, StabilizerBackendHandlesMultipleQallocs) {
    DeviceProfile profile;
    profile.id = "stim-multi-qalloc";
    profile.hardware.positions = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    profile.hardware.blockade_radius = 1.0;
    profile.backend = BackendKind::kStabilizer;

    HardwareVM vm(profile);

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 4});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"X", {3}, 0.0},
    });
    program.push_back(Instruction{Op::AllocArray, 2});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"CX", {0, 4}, 0.0},
    });
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"CX", {1, 4}, 0.0},
    });
    program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{4, 5},
    });

    const auto result = vm.run(program, 1);
    ASSERT_EQ(result.measurements.size(), 1u);
    ASSERT_EQ(result.measurements[0].bits.size(), 2u);
}
#endif

}  // namespace
