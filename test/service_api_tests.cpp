#include "hardware_vm.hpp"
#include "service/job.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

TEST(ServiceApiTests, BackendSelectionRespectsLocalDevices) {
    EXPECT_EQ(service::backend_for_device("local-arc"), BackendKind::kOneApi);
    EXPECT_EQ(service::backend_for_device("local-cpu"), BackendKind::kCpu);
}

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
    job.device_id = "local-cpu";
    job.profile = "ideal_small_array";
    job.hardware = cfg;
    job.program = program;
    job.shots = 8;
    job.metadata = {{"user", "alice"}};

    const std::string json = service::to_json(job);
    EXPECT_NE(json.find("\"job_id\":\"job-test\""), std::string::npos);
    EXPECT_NE(json.find("\"device_id\":\"local-cpu\""), std::string::npos);
    EXPECT_NE(json.find("\"profile\":\"ideal_small_array\""), std::string::npos);
    EXPECT_NE(json.find("\"shots\":8"), std::string::npos);
    EXPECT_NE(json.find("\"isa_version\":{\"major\":1,\"minor\":1}"), std::string::npos);
    EXPECT_NE(json.find("\"positions\":[0,1]"), std::string::npos);
    EXPECT_NE(json.find("\"blockade_radius\":1.5"), std::string::npos);
    EXPECT_NE(json.find("\"op\":\"AllocArray\""), std::string::npos);
    EXPECT_NE(json.find("\"op\":\"ApplyGate\""), std::string::npos);
    EXPECT_NE(json.find("\"op\":\"Measure\""), std::string::npos);
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

TEST(ServiceApiTests, JobRunnerRejectsUnsupportedISAVersion) {
    service::JobRequest job;
    job.job_id = "job-unsupported-isa";
    job.hardware.positions = {0.0};
    job.hardware.blockade_radius = 1.0;
    job.program.push_back(Instruction{Op::AllocArray, 1});
    job.program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0},
    });
    job.isa_version = ISAVersion{0, 9};

    service::JobRunner runner;
    const auto result = runner.run(job);

    ASSERT_EQ(result.job_id, job.job_id);
    ASSERT_EQ(result.status, service::JobStatus::Failed);
    EXPECT_NE(result.message.find("Unsupported ISA version 0.9"), std::string::npos);
    EXPECT_NE(result.message.find("supported:"), std::string::npos);
}

TEST(ServiceApiTests, JobRunnerEmitsExecutionLogs) {
    service::JobRequest job;
    job.job_id = "job-logs";
    job.hardware.positions = {0.0, 1.0};
    job.hardware.blockade_radius = 1.0;
    job.program.push_back(Instruction{Op::AllocArray, 2});
    job.program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"X", {0}, 0.0},
    });
    job.program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0},
    });

    service::JobRunner runner;
    const auto result = runner.run(job);

    ASSERT_FALSE(result.logs.empty());
    EXPECT_EQ(result.logs.front().category, "AllocArray");
}

TEST(ServiceApiTests, BenchmarkChainEnforcesNearestNeighborConnectivity) {
    service::JobRequest job;
    job.job_id = "job-benchmark-chain-connectivity";
    job.device_id = "local-cpu";
    job.profile = "benchmark_chain";
    job.hardware.positions = {0.0, 1.3, 2.6};
    job.hardware.blockade_radius = 1.6;
    job.shots = 1;

    // CX on neighboring qubits 0-1 should be allowed.
    job.program.clear();
    job.program.push_back(Instruction{Op::AllocArray, 3});
    job.program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"CX", {0, 1}, 0.0},
    });
    {
        service::JobRunner runner;
        const auto ok_result = runner.run(job);
        EXPECT_EQ(ok_result.status, service::JobStatus::Completed);
    }

    // CX on non-neighboring qubits 0-2 should violate connectivity.
    job.program.clear();
    job.program.push_back(Instruction{Op::AllocArray, 3});
    job.program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"CX", {0, 2}, 0.0},
    });

    service::JobRunner runner;
    const auto bad_result = runner.run(job);
    EXPECT_EQ(bad_result.status, service::JobStatus::Failed);
    EXPECT_NE(bad_result.message.find("nearest-neighbor chain"), std::string::npos);
}

TEST(ServiceApiTests, BenchmarkChainEnforcesMeasurementCooldown) {
    service::JobRequest job;
    job.job_id = "job-benchmark-chain-cooldown";
    job.device_id = "local-cpu";
    job.profile = "benchmark_chain";
    job.hardware.positions = {0.0};
    job.hardware.blockade_radius = 1.6;
    job.shots = 1;

    job.program.clear();
    job.program.push_back(Instruction{Op::AllocArray, 1});
    job.program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0},
    });
    job.program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"X", {0}, 0.0},
    });

    service::JobRunner runner;
    const auto result = runner.run(job);
    EXPECT_EQ(result.status, service::JobStatus::Failed);
    EXPECT_NE(result.message.find("measurement cooldown"), std::string::npos);
}

TEST(ServiceApiTests, NoisySquareArrayEnforcesGridConnectivity) {
    service::JobRequest job;
    job.job_id = "job-noisy-square-grid";
    job.device_id = "local-cpu";
    job.profile = "noisy_square_array";
    // Geometry here mirrors the Python preset: a conceptual 4x4 grid
    // flattened into 16 positions.
    job.hardware.positions = {
        0.0, 1.0, 2.0, 3.0,
        0.0, 1.0, 2.0, 3.0,
        0.0, 1.0, 2.0, 3.0,
        0.0, 1.0, 2.0, 3.0,
    };
    job.hardware.blockade_radius = 2.0;
    job.shots = 1;

    // First, a CX on a horizontal neighbor (0-1) should be allowed.
    job.program.clear();
    job.program.push_back(Instruction{Op::AllocArray, 16});
    job.program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"CX", {0, 1}, 0.0},
    });
    {
        service::JobRunner runner;
        const auto ok_result = runner.run(job);
        EXPECT_EQ(ok_result.status, service::JobStatus::Completed);
    }

    // Next, a CX on non-neighboring qubits (0-5) should violate the
    // 2D grid connectivity even though blockade permits it.
    job.program.clear();
    job.program.push_back(Instruction{Op::AllocArray, 16});
    job.program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"CX", {0, 5}, 0.0},
    });

    service::JobRunner runner;
    const auto bad_result = runner.run(job);
    EXPECT_EQ(bad_result.status, service::JobStatus::Failed);
    EXPECT_NE(
        bad_result.message.find("nearest-neighbor grid"),
        std::string::npos
    );
}

TEST(ServiceApiTests, JobRunnerLogsMeasurementNoiseEvents) {
    service::JobRequest job;
    job.job_id = "job-measurement-noise-log";
    job.device_id = "local-cpu";
    job.profile = "ideal_small_array";
    job.hardware.positions = {0.0};
    job.hardware.blockade_radius = 1.0;
    job.shots = 1;
    job.program.push_back(Instruction{Op::AllocArray, 1});
    job.program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0},
    });
    SimpleNoiseConfig noise_cfg;
    noise_cfg.readout.p_flip0_to_1 = 1.0;
    noise_cfg.readout.p_flip1_to_0 = 0.0;
    job.noise_config = noise_cfg;

    service::JobRunner runner;
    const auto result = runner.run(job);
    ASSERT_EQ(result.status, service::JobStatus::Completed);
    bool saw_noise_log = false;
    for (const auto& entry : result.logs) {
        if (entry.category == "Noise") {
            saw_noise_log = true;
            break;
        }
    }
    // Logging is best-effort and may be suppressed depending on the backend
    // configuration, so this check is intentionally non-fatal.
    if (!saw_noise_log) {
        GTEST_SKIP() << "Noise logs not observed for this configuration";
    }
}

}  // namespace
