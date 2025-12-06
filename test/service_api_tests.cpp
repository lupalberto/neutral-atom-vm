#include "service/job.hpp"

#include "vm.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

TEST(ServiceApiTests, JobRequestJson) {
    HardwareConfig cfg;
    cfg.positions = {0.0, 1.0};
    cfg.blockade_radius = 1.5;

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 2});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"H", {0}, 0.0},
    });
    program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0},
    });

    service::JobRequest job;
    job.job_id = "job-test";
    job.hardware = cfg;
    job.program = program;
    job.shots = 8;
    job.metadata = {{"user", "alice"}};

    const std::string json = service::to_json(job);
    const std::string expected =
        "{\"job_id\":\"job-test\"," \
        "\"shots\":8," \
        "\"hardware\":{\"positions\":[0,1],\"blockade_radius\":1.5}," \
        "\"program\":[{\"op\":\"AllocArray\",\"n_qubits\":2}," \
        "{\"op\":\"ApplyGate\",\"gate\":{\"name\":\"H\",\"targets\":[0],\"param\":0}}," \
        "{\"op\":\"Measure\",\"targets\":[0]}]," \
        "\"metadata\":{\"user\":\"alice\"}}";

    EXPECT_EQ(json, expected);
}

TEST(ServiceApiTests, JobRunnerExecutesProgram) {
    service::JobRequest job;
    job.job_id = "job-runner";
    job.hardware.positions = {0.0, 1.0};
    job.hardware.blockade_radius = 1.0;
    job.program.push_back(Instruction{Op::AllocArray, 2});
    job.program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"X", {1}, 0.0},
    });
    job.program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0, 1},
    });

    service::JobRunner runner;
    auto result = runner.run(job);

    ASSERT_EQ(result.job_id, job.job_id);
    ASSERT_EQ(result.status, service::JobStatus::Completed);
    ASSERT_EQ(result.measurements.size(), 1u);
    EXPECT_EQ(result.measurements[0].bits, std::vector<int>({0, 1}));
}

}  // namespace
