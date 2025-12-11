#include "service/job_service.hpp"

#include "vm/isa.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using service::JobRequest;
using service::JobResult;
using service::JobService;
using service::JobStatus;

namespace {

JobRequest make_simple_job() {
    JobRequest job;
    job.device_id = "state-vector";
    job.profile = "ideal_small_array";
    job.hardware.positions = {0.0};
    job.shots = 1;
    job.program = {
        {Op::AllocArray, 1},
        {Op::ApplyGate, Gate{"H", {0}}},
        {Op::Measure, std::vector<int>{0}},
    };
    return job;
}

}  // namespace

TEST(ServiceJobServiceTests, SubmitsAsyncJobAndReturnsResult) {
    JobService service;
    JobRequest job = make_simple_job();

    const std::string job_id = service.submit(job, 1);
    ASSERT_FALSE(job_id.empty());

    auto snapshot = service.status(job_id);
    EXPECT_TRUE(snapshot.status == JobStatus::Pending || snapshot.status == JobStatus::Running);

    std::optional<JobResult> result;
    for (int attempt = 0; attempt < 200 && !result; ++attempt) {
        result = service.poll_result(job_id);
        if (!result) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, JobStatus::Completed);
    EXPECT_EQ(result->measurements.size(), 1u);
    EXPECT_FALSE(result->measurements[0].bits.empty());
}
