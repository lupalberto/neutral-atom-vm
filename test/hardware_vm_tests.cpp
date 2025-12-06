#include "hardware_vm.hpp"

#include <gtest/gtest.h>

#include <vector>

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

    const auto measurements = vm.run(program, 1);
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

    const auto measurements = vm.run(program, 3);
    EXPECT_EQ(measurements.size(), 3u);
}

}  // namespace
