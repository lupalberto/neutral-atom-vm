#include "service/job.hpp"
#include "service/job_validation.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

TEST(ValidatorRegistryTests, PropagatesValidatorExceptions) {
    service::ValidatorRegistry registry;
    registry.register_validator(std::make_unique<service::LambdaValidator>(
        "throws",
        [](const HardwareConfig&, const std::vector<Instruction>&) {
            throw std::runtime_error("boom");
        }
    ));
    HardwareConfig hw;
    hw.site_ids = {0};
    EXPECT_THROW(
        registry.run_all_validators(hw, {}),
        std::runtime_error
    );
}

TEST(ValidatorRegistryTests, RunsLambdaValidatorsInOrder) {
    service::ValidatorRegistry registry;
    bool first = false;
    bool second = false;
    registry.register_validator(std::make_unique<service::LambdaValidator>(
        "first",
        [&](const HardwareConfig&, const std::vector<Instruction>&) {
            first = true;
        }
    ));
    registry.register_validator(std::make_unique<service::LambdaValidator>(
        "second",
        [&](const HardwareConfig&, const std::vector<Instruction>&) {
            if (!first) {
                throw std::runtime_error("order");
            }
            second = true;
        }
    ));
    HardwareConfig hw;
    hw.site_ids = {0};
    EXPECT_NO_THROW(registry.run_all_validators(hw, {}));
    EXPECT_TRUE(first);
    EXPECT_TRUE(second);
}

TEST(ValidatorRegistryTests, FactorySelectsValidators) {
    service::JobRequest job;
    job.device_id = "local-cpu";
    HardwareConfig hw;
    hw.site_ids = {0, 1};
    hw.transport_edges = {TransportEdge{0, 1}};

    auto baseline = service::make_validator_registry_for(job, hw);
    const std::vector<std::string> expected_baseline = {"active_qubits", "transport"};
    EXPECT_EQ(baseline.validator_names(), expected_baseline);

    hw.blockade_radius = 2.0;
    auto with_blockade = service::make_validator_registry_for(job, hw);
    const std::vector<std::string> expected_blockade = {
        "active_qubits",
        "blockade",
        "transport",
    };
    EXPECT_EQ(with_blockade.validator_names(), expected_blockade);

    hw.blockade_radius = 0.0;
    hw.blockade_model.radius = 0.0;
    job.metadata["blockade_validator"] = "1";
    auto with_metadata = service::make_validator_registry_for(job, hw);
    EXPECT_EQ(with_metadata.validator_names(), expected_blockade);
}
