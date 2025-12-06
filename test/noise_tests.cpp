#include "noise.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

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

}  // namespace

