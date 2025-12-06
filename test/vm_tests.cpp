#include "engine_statevector.hpp"
#include "hardware_vm.hpp"
#include "noise.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

namespace {

TEST(StatevectorEngineTests, BellState) {
    HardwareConfig cfg;
    cfg.positions = {0.0, 1.0};
    cfg.blockade_radius = 1.5;

    StatevectorEngine engine(cfg);

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 2});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"H", {1}, 0.0},
    });
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"CX", {1, 0}, 0.0},
    });

    engine.run(program);

    const auto& state = engine.state().state;
    const double inv_sqrt2 = 1.0 / std::sqrt(2.0);

    ASSERT_EQ(state.size(), 4u);
    EXPECT_NEAR(std::abs(state[0]), inv_sqrt2, 1e-6);
    EXPECT_NEAR(std::abs(state[3]), inv_sqrt2, 1e-6);
    EXPECT_NEAR(std::abs(state[1]), 0.0, 1e-6);
    EXPECT_NEAR(std::abs(state[2]), 0.0, 1e-6);
}

TEST(StatevectorEngineTests, MoveAtomInstruction) {
    HardwareConfig cfg;
    cfg.positions = {0.0, 1.0, 2.0};

    StatevectorEngine engine(cfg);

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 3});
    program.push_back(Instruction{
        Op::MoveAtom,
        MoveAtomInstruction{1, 4.5},
    });
    program.push_back(Instruction{
        Op::MoveAtom,
        MoveAtomInstruction{2, -1.0},
    });

    engine.run(program);

    const auto& positions = engine.state().hw.positions;
    ASSERT_GE(positions.size(), 3u);
    EXPECT_NEAR(positions[1], 4.5, 1e-9);
    EXPECT_NEAR(positions[2], -1.0, 1e-9);
}

TEST(StatevectorEngineTests, WaitInstruction) {
    HardwareConfig cfg;
    cfg.positions = {0.0};

    StatevectorEngine engine(cfg);
    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 1});
    program.push_back(Instruction{
        Op::Wait,
        WaitInstruction{5.0},
    });
    program.push_back(Instruction{
        Op::Wait,
        WaitInstruction{2.5},
    });

    engine.run(program);
    EXPECT_NEAR(engine.state().logical_time, 7.5, 1e-9);
}

TEST(StatevectorEngineTests, PulseInstruction) {
    HardwareConfig cfg;
    cfg.positions = {0.0, 1.0};

    StatevectorEngine engine(cfg);
    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 2});
    program.push_back(Instruction{
        Op::Pulse,
        PulseInstruction{0, 1.5, 20.0},
    });
    program.push_back(Instruction{
        Op::Pulse,
        PulseInstruction{1, -0.5, 10.0},
    });

    engine.run(program);
    const auto& pulses = engine.state().pulse_log;
    ASSERT_EQ(pulses.size(), 2u);
    EXPECT_EQ(pulses[0].target, 0);
    EXPECT_NEAR(pulses[0].detuning, 1.5, 1e-9);
    EXPECT_NEAR(pulses[1].duration, 10.0, 1e-9);
}

TEST(StatevectorEngineTests, MeasureSingleQubitTargets) {
    HardwareConfig cfg;
    cfg.positions = {0.0, 1.0};

    StatevectorEngine engine(cfg);
    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 2});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"H", {1}, 0.0},
    });
    program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0},
    });

    engine.run(program);

    const auto& records = engine.state().measurements;
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].targets, std::vector<int>({0}));
    EXPECT_EQ(records[0].bits, std::vector<int>({0}));

    const auto& state = engine.state().state;
    const double inv_sqrt2 = 1.0 / std::sqrt(2.0);
    EXPECT_NEAR(std::abs(state[0]), inv_sqrt2, 1e-6);
    EXPECT_NEAR(std::abs(state[2]), inv_sqrt2, 1e-6);
    EXPECT_NEAR(std::abs(state[1]), 0.0, 1e-6);
    EXPECT_NEAR(std::abs(state[3]), 0.0, 1e-6);
}

TEST(StatevectorEngineTests, MeasureAllQubitsRecordsBits) {
    HardwareConfig cfg;
    cfg.positions = {0.0, 1.0};

    StatevectorEngine engine(cfg);
    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 2});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"X", {1}, 0.0},
    });
    program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0, 1},
    });

    engine.run(program);

    const auto& records = engine.state().measurements;
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].targets, std::vector<int>({0, 1}));
    EXPECT_EQ(records[0].bits, std::vector<int>({0, 1}));

    const auto& state = engine.state().state;
    EXPECT_NEAR(std::abs(state[2] - std::complex<double>(1.0, 0.0)), 0.0, 1e-9);
}

TEST(StatevectorEngineTests, BlockadeAllowsCloseQubits) {
    HardwareConfig cfg;
    cfg.positions = {0.0, 0.5};
    cfg.blockade_radius = 1.0;

    StatevectorEngine engine(cfg);
    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 2});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"CX", {0, 1}, 0.0},
    });

    EXPECT_NO_THROW(engine.run(program));
}

TEST(StatevectorEngineTests, BlockadeBlocksDistantQubits) {
    HardwareConfig cfg;
    cfg.positions = {0.0, 5.0};
    cfg.blockade_radius = 1.0;

    StatevectorEngine engine(cfg);
    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 2});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"CX", {0, 1}, 0.0},
    });

    EXPECT_THROW(engine.run(program), std::runtime_error);
}

TEST(StatevectorEngineTests, MeasurementNoiseBitFlipAllOnes) {
    HardwareConfig cfg;
    cfg.positions = {0.0};

    StatevectorEngine engine(cfg);
    engine.set_random_seed(1234);

    SimpleNoiseConfig noise_cfg;
    noise_cfg.readout.p_flip0_to_1 = 1.0;
    noise_cfg.readout.p_flip1_to_0 = 0.0;
    auto noise = std::make_shared<SimpleNoiseEngine>(noise_cfg);
    engine.set_noise_model(noise);

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 1});
    program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0},
    });

    engine.run(program);

    const auto& records = engine.state().measurements;
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].targets, std::vector<int>({0}));
    EXPECT_EQ(records[0].bits, std::vector<int>({1}));
}

TEST(StatevectorEngineTests, MeasurementNoiseLossMarksMinusOne) {
    HardwareConfig cfg;
    cfg.positions = {0.0};

    StatevectorEngine engine(cfg);
    engine.set_random_seed(42);

    SimpleNoiseConfig noise_cfg;
    noise_cfg.p_loss = 1.0;
    auto noise = std::make_shared<SimpleNoiseEngine>(noise_cfg);
    engine.set_noise_model(noise);

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 1});
    program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0},
    });

    engine.run(program);

    const auto& records = engine.state().measurements;
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].targets, std::vector<int>({0}));
    EXPECT_EQ(records[0].bits, std::vector<int>({-1}));
}

TEST(StatevectorEngineTests, SingleQubitGatePauliNoiseActsAfterGate) {
    HardwareConfig cfg;
    cfg.positions = {0.0};

    StatevectorEngine engine(cfg);
    engine.set_random_seed(7);

    SimpleNoiseConfig noise_cfg;
    noise_cfg.gate.single_qubit.px = 1.0;
    auto noise = std::make_shared<SimpleNoiseEngine>(noise_cfg);
    engine.set_noise_model(noise);

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

    engine.run(program);

    const auto& records = engine.state().measurements;
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].targets, std::vector<int>({0}));
    // Ideal X|0> = |1>, but with an additional X error after the gate
    // we should measure |0>.
    EXPECT_EQ(records[0].bits, std::vector<int>({0}));
}

TEST(HardwareVMTests, RunsProgramWithIdealEngine) {
    DeviceProfile profile;
    profile.id = "ideal-statevector";
    profile.hardware.positions = {0.0, 1.0};
    profile.hardware.blockade_radius = 1.0;

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 2});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"X", {1}, 0.0},
    });
    program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0, 1},
    });

    HardwareVM hvm(profile);
    const auto measurements = hvm.run(program, /*shots=*/1);

    ASSERT_EQ(measurements.size(), 1u);
    EXPECT_EQ(measurements[0].targets, std::vector<int>({0, 1}));
    EXPECT_EQ(measurements[0].bits, std::vector<int>({0, 1}));
}

TEST(HardwareVMTests, RejectsUnsupportedISAVersion) {
    DeviceProfile profile;
    profile.id = "unsupported-isa-device";
    profile.isa_version = ISAVersion{0, 9};
    profile.hardware.positions = {0.0};
    profile.hardware.blockade_radius = 1.0;

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 1});
    program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0},
    });

    try {
        HardwareVM hvm(profile);
        (void)hvm.run(program, /*shots=*/1);
        FAIL() << "Expected std::runtime_error for unsupported ISA version";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(
            ex.what(),
            "Unsupported ISA version 0.9 (supported: 1.0)"
        );
    }
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
