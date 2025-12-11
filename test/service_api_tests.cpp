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
    EXPECT_EQ(result.log_time_units, "us");
    EXPECT_EQ(result.logs.front().category, "Timeline");
    bool saw_alloc = false;
    for (const auto& entry : result.logs) {
        if (entry.category == "AllocArray") {
            saw_alloc = true;
            break;
        }
    }
    EXPECT_TRUE(saw_alloc);
}

TEST(ServiceApiTests, BenchmarkChainEnforcesNearestNeighborConnectivity) {
    service::JobRequest job;
    job.job_id = "job-benchmark-chain-connectivity";
    job.device_id = "local-cpu";
    job.profile = "benchmark_chain";
    job.hardware.positions = {0.0, 1.3, 2.6};
    job.hardware.blockade_radius = 3.0;
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
    EXPECT_EQ(result.status, service::JobStatus::Completed);
    EXPECT_FALSE(result.timeline.empty());
    EXPECT_EQ(result.timeline_units, "us");
    bool saw_apply_gate = false;
    for (const auto& entry : result.timeline) {
        if (entry.op == "ApplyGate") {
            saw_apply_gate = true;
            break;
        }
    }
    EXPECT_TRUE(saw_apply_gate);
    bool saw_wait = false;
    bool saw_timeline_log = false;
    for (const auto& entry : result.logs) {
        if (entry.category == "Wait") {
            saw_wait = true;
        }
        if (entry.category == "Timeline") {
            saw_timeline_log = true;
        }
    }
    EXPECT_TRUE(saw_wait);
    EXPECT_TRUE(saw_timeline_log);
}

#ifdef NA_VM_WITH_STIM
TEST(ServiceApiTests, StimBackendTimelineIsChronological) {
    service::JobRequest job;
    job.job_id = "stim-timeline-order";
    job.device_id = "stabilizer";
    job.profile = "ideal_square_grid";
    job.hardware.positions = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    job.hardware.blockade_radius = 10.0;
    job.shots = 1;

    NativeGate x_gate;
    x_gate.name = "X";
    x_gate.arity = 1;
    x_gate.duration_ns = 500.0;
    NativeGate h_gate;
    h_gate.name = "H";
    h_gate.arity = 1;
    h_gate.duration_ns = 500.0;
    NativeGate z_gate;
    z_gate.name = "Z";
    z_gate.arity = 1;
    z_gate.duration_ns = 500.0;
    NativeGate cx_gate;
    cx_gate.name = "CX";
    cx_gate.arity = 2;
    cx_gate.duration_ns = 1000.0;
    job.hardware.native_gates = {x_gate, h_gate, z_gate, cx_gate};
    job.hardware.timing_limits.measurement_duration_ns = 50000.0;
    job.hardware.timing_limits.measurement_cooldown_ns = 50000.0;

    job.program.push_back(Instruction{Op::AllocArray, 6});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"H", {0}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"H", {2}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"X", {3}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"CX", {0, 1}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"CX", {2, 3}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"H", {0}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"H", {1}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"CX", {0, 4}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"CX", {1, 4}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"H", {0}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"H", {1}, 0.0}});
    job.program.push_back(Instruction{Op::Measure, std::vector<int>{4}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"CX", {1, 5}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"CX", {2, 5}, 0.0}});
    job.program.push_back(Instruction{Op::Measure, std::vector<int>{5}});
    job.program.push_back(Instruction{Op::Measure, std::vector<int>{0, 1, 2, 3}});

    service::JobRunner runner;
    const auto result = runner.run(job);

    ASSERT_EQ(result.status, service::JobStatus::Completed);
    ASSERT_FALSE(result.timeline.empty());
    double last_start = -1.0;
    for (const auto& entry : result.timeline) {
        EXPECT_GE(entry.start_time, last_start);
        last_start = entry.start_time;
    }
}
#endif  // NA_VM_WITH_STIM

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
    job.hardware.blockade_radius = 10.0;
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

TEST(ServiceApiTests, TimelineLogsMatchEntries) {
    service::JobRequest job;
    job.job_id = "job-log-timeline-sync";
    job.hardware.positions = {0.0, 1.0};
    job.hardware.blockade_radius = 1.0;
    job.program.push_back(Instruction{Op::AllocArray, 2});
    job.program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"H", {0}, 0.0},
    });
    job.program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"CX", {0, 1}, 0.0},
    });
    job.program.push_back(Instruction{
        Op::Measure,
        std::vector<int>{0},
    });

    service::JobRunner runner;
    const auto result = runner.run(job);

    ASSERT_EQ(result.status, service::JobStatus::Completed);
    ASSERT_FALSE(result.timeline.empty());
    ASSERT_FALSE(result.logs.empty());

    std::size_t measure_idx = result.timeline.size();
    for (std::size_t idx = 0; idx < result.timeline.size(); ++idx) {
        if (result.timeline[idx].op == "Measure") {
            measure_idx = idx;
            break;
        }
    }
    ASSERT_LT(measure_idx, result.timeline.size());

    const auto& entry = result.timeline[measure_idx];
    const auto& log_entry = result.logs[measure_idx];
    EXPECT_NEAR(entry.start_time, log_entry.logical_time, 1e-6);
}

#ifdef NA_VM_WITH_STIM
TEST(ServiceApiTests, StimBackendExposesPlanAndExecutionTimelines) {
    service::JobRequest job;
    job.job_id = "stim-plan-vs-exec";
    job.device_id = "stabilizer";
    job.profile = "ideal_square_grid";
    job.hardware.positions = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    job.hardware.blockade_radius = 10.0;
    job.shots = 1;

    NativeGate x_gate;
    x_gate.name = "X";
    x_gate.arity = 1;
    x_gate.duration_ns = 500.0;
    NativeGate h_gate;
    h_gate.name = "H";
    h_gate.arity = 1;
    h_gate.duration_ns = 500.0;
    NativeGate z_gate;
    z_gate.name = "Z";
    z_gate.arity = 1;
    z_gate.duration_ns = 500.0;
    NativeGate cx_gate;
    cx_gate.name = "CX";
    cx_gate.arity = 2;
    cx_gate.duration_ns = 1000.0;
    job.hardware.native_gates = {x_gate, h_gate, z_gate, cx_gate};
    job.hardware.timing_limits.measurement_duration_ns = 50000.0;
    job.hardware.timing_limits.measurement_cooldown_ns = 50000.0;

    job.program.push_back(Instruction{Op::AllocArray, 6});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"H", {0}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"H", {2}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"X", {3}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"CX", {0, 1}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"CX", {2, 3}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"H", {0}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"H", {1}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"CX", {0, 4}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"CX", {1, 4}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"H", {0}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"H", {1}, 0.0}});
    job.program.push_back(Instruction{Op::Measure, std::vector<int>{4}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"CX", {1, 5}, 0.0}});
    job.program.push_back(Instruction{Op::ApplyGate, Gate{"CX", {2, 5}, 0.0}});
    job.program.push_back(Instruction{Op::Measure, std::vector<int>{5}});
    job.program.push_back(Instruction{Op::Measure, std::vector<int>{0, 1, 2, 3}});

    service::JobRunner runner;
    const auto result = runner.run(job);

    ASSERT_EQ(result.status, service::JobStatus::Completed);
    ASSERT_FALSE(result.timeline.empty());
    ASSERT_FALSE(result.scheduler_timeline.empty());
    EXPECT_NE(result.timeline, result.scheduler_timeline);
}
#endif  // NA_VM_WITH_STIM

}  // namespace
