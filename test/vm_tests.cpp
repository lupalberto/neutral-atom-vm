#include "engine_statevector.hpp"
#include "hardware_vm.hpp"
#include "noise.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <random>

namespace {

class SeededFlipNoiseEngine : public NoiseEngine {
 public:
  std::shared_ptr<const NoiseEngine> clone() const override {
    return std::make_shared<SeededFlipNoiseEngine>();
  }

  void apply_single_qubit_gate_noise(
      int target,
      int /*n_qubits*/,
      std::vector<std::complex<double>>& amplitudes,
      RandomStream& rng) const override {
    const double value = rng.uniform(0.0, 1.0);
    if (value <= 0.5) {
      return;
    }
    const std::size_t mask = static_cast<std::size_t>(1) << target;
    const std::size_t dim = amplitudes.size();
    for (std::size_t idx = 0; idx < dim; ++idx) {
      if ((idx & mask) == 0) {
        const std::size_t flipped = idx | mask;
        std::swap(amplitudes[idx], amplitudes[flipped]);
      }
    }
  }
};

int expected_seeded_flip_bit(std::uint64_t seed) {
  std::mt19937_64 rng(seed);
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  return (dist(rng) > 0.5) ? 0 : 1;
}

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

    const auto& state = engine.state_vector();
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

    const auto& state = engine.state_vector();
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

    const auto& state = engine.state_vector();
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

TEST(StatevectorEngineTests, EnforcesNativeGateConnectivityForNearestNeighborChain) {
    HardwareConfig cfg;
    cfg.positions = {0.0, 1.0, 2.0};
    NativeGate cx_native;
    cx_native.name = "CX";
    cx_native.arity = 2;
    cx_native.connectivity = ConnectivityKind::NearestNeighborChain;
    cfg.native_gates.push_back(cx_native);

    StatevectorEngine engine(cfg);

    // Neighboring qubits 0-1 are allowed.
    std::vector<Instruction> ok_program;
    ok_program.push_back(Instruction{Op::AllocArray, 3});
    ok_program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"CX", {0, 1}, 0.0},
    });
    EXPECT_NO_THROW(engine.run(ok_program));

    // Non-neighboring qubits 0-2 violate the chain connectivity.
    std::vector<Instruction> bad_program;
    bad_program.push_back(Instruction{Op::AllocArray, 3});
    bad_program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"CX", {0, 2}, 0.0},
    });
    EXPECT_THROW(engine.run(bad_program), std::runtime_error);
}

TEST(StatevectorEngineTests, EnforcesWaitDurationLimitsWhenConfigured) {
    HardwareConfig cfg;
    cfg.positions = {0.0};
    cfg.timing_limits.min_wait_ns = 1.0;
    cfg.timing_limits.max_wait_ns = 5.0;

    StatevectorEngine engine(cfg);

    std::vector<Instruction> short_wait_prog;
    short_wait_prog.push_back(Instruction{Op::AllocArray, 1});
    short_wait_prog.push_back(Instruction{
        Op::Wait,
        WaitInstruction{0.5},
    });
    EXPECT_THROW(engine.run(short_wait_prog), std::invalid_argument);

    std::vector<Instruction> long_wait_prog;
    long_wait_prog.push_back(Instruction{Op::AllocArray, 1});
    long_wait_prog.push_back(Instruction{
        Op::Wait,
        WaitInstruction{10.0},
    });
    EXPECT_THROW(engine.run(long_wait_prog), std::invalid_argument);

    std::vector<Instruction> ok_wait_prog;
    ok_wait_prog.push_back(Instruction{Op::AllocArray, 1});
    ok_wait_prog.push_back(Instruction{
        Op::Wait,
        WaitInstruction{3.0},
    });
    EXPECT_NO_THROW(engine.run(ok_wait_prog));
}

TEST(StatevectorEngineTests, EnforcesPulseLimitsWhenConfigured) {
    HardwareConfig cfg;
    cfg.positions = {0.0};
    cfg.pulse_limits.detuning_min = -1.0;
    cfg.pulse_limits.detuning_max = 1.0;
    cfg.pulse_limits.duration_min_ns = 1.0;
    cfg.pulse_limits.duration_max_ns = 10.0;

    StatevectorEngine engine(cfg);

    std::vector<Instruction> bad_detuning_prog;
    bad_detuning_prog.push_back(Instruction{Op::AllocArray, 1});
    bad_detuning_prog.push_back(Instruction{
        Op::Pulse,
        PulseInstruction{0, 2.0, 5.0},
    });
    EXPECT_THROW(engine.run(bad_detuning_prog), std::invalid_argument);

    std::vector<Instruction> bad_duration_prog;
    bad_duration_prog.push_back(Instruction{Op::AllocArray, 1});
    bad_duration_prog.push_back(Instruction{
        Op::Pulse,
        PulseInstruction{0, 0.0, 0.5},
    });
    EXPECT_THROW(engine.run(bad_duration_prog), std::invalid_argument);

    std::vector<Instruction> ok_pulse_prog;
    ok_pulse_prog.push_back(Instruction{Op::AllocArray, 1});
    ok_pulse_prog.push_back(Instruction{
        Op::Pulse,
        PulseInstruction{0, 0.5, 5.0},
    });
    EXPECT_NO_THROW(engine.run(ok_pulse_prog));
}

TEST(StatevectorEngineTests, EnforcesMeasurementCooldown) {
    HardwareConfig cfg;
    cfg.positions = {0.0};
    cfg.timing_limits.measurement_cooldown_ns = 2.0;

    StatevectorEngine engine(cfg);

    std::vector<Instruction> bad_prog;
    bad_prog.push_back(Instruction{Op::AllocArray, 1});
    bad_prog.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0},
    });
    bad_prog.push_back(Instruction{
        Op::ApplyGate,
        Gate{"X", {0}, 0.0},
    });

    EXPECT_THROW(engine.run(bad_prog), std::runtime_error);

    StatevectorEngine engine_ok(cfg);
    std::vector<Instruction> ok_prog;
    ok_prog.push_back(Instruction{Op::AllocArray, 1});
    ok_prog.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0},
    });
    ok_prog.push_back(Instruction{
        Op::Wait,
        WaitInstruction{2.5},
    });
    ok_prog.push_back(Instruction{
        Op::ApplyGate,
        Gate{"X", {0}, 0.0},
    });

    EXPECT_NO_THROW(engine_ok.run(ok_prog));
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
    const auto result = hvm.run(program, /*shots=*/1);
    const auto& measurements = result.measurements;

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
        const std::string msg = ex.what();
        EXPECT_NE(msg.find("Unsupported ISA version 0.9"), std::string::npos);
        EXPECT_NE(msg.find("supported:"), std::string::npos);
    }
}

TEST(HardwareVMTests, RespectsShotSeeds) {
    DeviceProfile profile;
    profile.id = "seeded-shot-profile";
    profile.hardware.positions = {0.0};
    profile.hardware.blockade_radius = 1.0;
    profile.noise_engine = std::make_shared<SeededFlipNoiseEngine>();

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

    std::vector<std::uint64_t> seeds = {42ull, 99ull, 123456ull};
    HardwareVM hvm(profile);
    const auto result = hvm.run(program, static_cast<int>(seeds.size()), seeds);
    const auto& measurements = result.measurements;

    ASSERT_EQ(measurements.size(), seeds.size());
    for (std::size_t idx = 0; idx < seeds.size(); ++idx) {
        EXPECT_EQ(measurements[idx].targets, std::vector<int>({0}));
        ASSERT_EQ(measurements[idx].bits.size(), 1u);
        EXPECT_EQ(measurements[idx].bits[0], expected_seeded_flip_bit(seeds[idx]));
    }
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
