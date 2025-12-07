#include "noise.hpp"
#include "engine_statevector.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>
#include <cmath>
#include <random>

namespace {

class SequenceRandomStream : public RandomStream {
  public:
    explicit SequenceRandomStream(std::vector<double> samples)
        : samples_(std::move(samples)) {}

    double uniform(double lo, double hi) override {
        if (index_ >= samples_.size()) {
            return lo;
        }
        const double raw = samples_[index_++];
        return lo + (hi - lo) * raw;
    }

  private:
    std::vector<double> samples_;
    std::size_t index_ = 0;
};

class TaggingNoiseEngine : public NoiseEngine {
  public:
    explicit TaggingNoiseEngine(int tag) : tag_(tag) {}

    void apply_measurement_noise(
        MeasurementRecord& record,
        RandomStream& /*rng*/
    ) const override {
        record.bits.push_back(tag_);
    }

    std::shared_ptr<const NoiseEngine> clone() const override {
        return std::make_shared<TaggingNoiseEngine>(*this);
    }

  private:
    int tag_;
};

class ThresholdNoiseEngine : public NoiseEngine {
  public:
    explicit ThresholdNoiseEngine(double threshold) : threshold_(threshold) {}

    void apply_measurement_noise(
        MeasurementRecord& record,
        RandomStream& rng
    ) const override {
        const double sample = rng.uniform(0.0, 1.0);
        record.bits[0] = (sample >= threshold_) ? 1 : 0;
    }

    std::shared_ptr<const NoiseEngine> clone() const override {
        return std::make_shared<ThresholdNoiseEngine>(*this);
    }

  private:
    double threshold_;
};

TEST(CompositeNoiseEngineTests, AppliesSourcesInOrder) {
    auto first = std::make_shared<TaggingNoiseEngine>(1);
    auto second = std::make_shared<TaggingNoiseEngine>(2);

    CompositeNoiseEngine engine({first, second});

    MeasurementRecord record;
    SequenceRandomStream rng({});

    engine.apply_measurement_noise(record, rng);

    EXPECT_EQ(record.bits, (std::vector<int>{1, 2}));
}

TEST(CompositeNoiseEngineTests, UsesRandomStreamAbstraction) {
    auto noise = std::make_shared<ThresholdNoiseEngine>(0.5);
    CompositeNoiseEngine engine({noise});

    MeasurementRecord record;
    record.bits = {0};

    SequenceRandomStream low({0.1});
    engine.apply_measurement_noise(record, low);
    EXPECT_EQ(record.bits[0], 0);

    record.bits = {0};
    SequenceRandomStream high({0.9});
    engine.apply_measurement_noise(record, high);
    EXPECT_EQ(record.bits[0], 1);
}

TEST(PhaseNoiseSourceTests, SingleQubitPhaseKickMatchesRandomStream) {
    HardwareConfig hw;
    hw.positions = {0.0};

    SimpleNoiseConfig cfg;
    cfg.phase.single_qubit = M_PI;
    auto noise = std::make_shared<SimpleNoiseEngine>(cfg);

    StatevectorEngine engine(hw);
    engine.set_noise_model(noise);
    engine.set_random_seed(4242);

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 1});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"H", {0}, 0.0},
    });

    engine.run(program);

    const auto& state = engine.state_vector();
    ASSERT_EQ(state.size(), 2u);

    std::mt19937_64 rng_copy(4242);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    const double r = dist(rng_copy);
    const double theta = (2.0 * r - 1.0) * cfg.phase.single_qubit;
    const double half = 0.5 * theta;
    const std::complex<double> phase0(
        std::cos(-half),
        std::sin(-half)
    );
    const std::complex<double> phase1(
        std::cos(half),
        std::sin(half)
    );
    const double inv_sqrt2 = 1.0 / std::sqrt(2.0);
    const std::complex<double> expected0 = inv_sqrt2 * phase0;
    const std::complex<double> expected1 = inv_sqrt2 * phase1;

    EXPECT_NEAR(std::real(state[0]), std::real(expected0), 1e-9);
    EXPECT_NEAR(std::imag(state[0]), std::imag(expected0), 1e-9);
    EXPECT_NEAR(std::real(state[1]), std::real(expected1), 1e-9);
    EXPECT_NEAR(std::imag(state[1]), std::imag(expected1), 1e-9);
}

TEST(PhaseNoiseSourceTests, IdlePhaseNoiseScalesWithDuration) {
    HardwareConfig hw;
    hw.positions = {0.0};

    SimpleNoiseConfig cfg;
    cfg.phase.idle = M_PI;
    auto noise = std::make_shared<SimpleNoiseEngine>(cfg);

    StatevectorEngine engine(hw);
    engine.set_noise_model(noise);
    engine.set_random_seed(2025);

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 1});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"H", {0}, 0.0},
    });
    program.push_back(Instruction{
        Op::Wait,
        WaitInstruction{0.25},
    });

    engine.run(program);

    const auto& state = engine.state_vector();
    ASSERT_EQ(state.size(), 2u);

    std::mt19937_64 rng_copy(2025);
    // No gate/rng draws prior to idle, so first draw corresponds to wait noise.
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    const double r = dist(rng_copy);
    const double theta = (2.0 * r - 1.0) * (cfg.phase.idle * 0.25);
    const double half = 0.5 * theta;
    const std::complex<double> phase0(
        std::cos(-half),
        std::sin(-half)
    );
    const std::complex<double> phase1(
        std::cos(half),
        std::sin(half)
    );
    const double inv_sqrt2 = 1.0 / std::sqrt(2.0);
    const std::complex<double> expected0 = inv_sqrt2 * phase0;
    const std::complex<double> expected1 = inv_sqrt2 * phase1;

    EXPECT_NEAR(std::real(state[0]), std::real(expected0), 1e-9);
    EXPECT_NEAR(std::imag(state[0]), std::imag(expected0), 1e-9);
    EXPECT_NEAR(std::real(state[1]), std::real(expected1), 1e-9);
    EXPECT_NEAR(std::imag(state[1]), std::imag(expected1), 1e-9);
}

TEST(CorrelatedPauliSourceTests, AppliesConfiguredPair) {
    HardwareConfig hw;
    hw.positions = {0.0, 1.0};

    SimpleNoiseConfig cfg;
    cfg.correlated_gate.matrix[4 * 1 + 3] = 1.0;  // X on control, Z on target.

    auto noise = std::make_shared<SimpleNoiseEngine>(cfg);
    StatevectorEngine engine(hw);
    engine.set_noise_model(noise);

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 2});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"CX", {0, 1}, 0.0},
    });
    program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0, 1},
    });

    engine.run(program);

    const auto& records = engine.state().measurements;
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].bits, (std::vector<int>{1, 0}));
}

TEST(LossTrackingSourceTests, GateLossSetsMeasurementToErasure) {
    HardwareConfig hw;
    hw.positions = {0.0};

    SimpleNoiseConfig cfg;
    cfg.loss_runtime.per_gate = 1.0;

    auto noise = std::make_shared<SimpleNoiseEngine>(cfg);
    StatevectorEngine engine(hw);
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
    EXPECT_EQ(records[0].bits, (std::vector<int>{-1}));
}

}  // namespace
