#include "service/job.hpp"

#include <gtest/gtest.h>
#include <stdexcept>

namespace {

service::JobRequest make_blockade_job() {
    service::JobRequest job;
    job.device_id = "local-cpu";
    job.profile = "ideal_small_array";
    job.hardware.positions = {0.0, 3.0};
    job.hardware.site_ids = {0, 1};
    job.shots = 1;
    job.program = {
        {Op::AllocArray, 2},
        {Op::ApplyGate, Gate{"CX", {0, 1}}},
        {Op::Measure, std::vector<int>{0, 1}},
    };
    return job;
}

}  // namespace

TEST(ServiceJobValidationTests, BlockadeConstraintCheckedBeforeExecution) {
    service::JobRunner runner;
    auto job = make_blockade_job();
    job.hardware.blockade_radius = 1.5;

    const auto result = runner.run(job);
    EXPECT_EQ(result.status, service::JobStatus::Failed);
    EXPECT_NE(
        result.message.find("blockade radius"),
        std::string::npos
    );
}
