#include "service/scheduler.hpp"

#include "vm/isa.hpp"

#include <gtest/gtest.h>

TEST(SchedulerTests, InsertsWaitAfterMeasurementCooldown) {
    HardwareConfig hw;
    hw.positions = {0.0};
    hw.timing_limits.measurement_cooldown_ns = 5.0;
    hw.native_gates.clear();
    NativeGate x;
    x.name = "X";
    x.arity = 1;
    x.duration_ns = 10.0;
    hw.native_gates.push_back(x);

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 1});
    program.push_back(Instruction{Op::Measure, std::vector<int>{0}});
    program.push_back(Instruction{Op::ApplyGate, Gate{"X", {0}}});

    const service::SchedulerResult scheduled = service::schedule_program(program, hw);
    const auto& program_out = scheduled.program;
    ASSERT_EQ(program_out.size(), 4u);
    EXPECT_EQ(program_out[0].op, Op::AllocArray);
    EXPECT_EQ(program_out[1].op, Op::Measure);
    EXPECT_EQ(program_out[2].op, Op::Wait);
    EXPECT_EQ(program_out[3].op, Op::ApplyGate);

    const WaitInstruction& wait_instr = std::get<WaitInstruction>(program_out[2].payload);
    EXPECT_GE(wait_instr.duration, hw.timing_limits.measurement_cooldown_ns);
}

TEST(SchedulerTests, AllowsParallelSingleQubitGates) {
    HardwareConfig hw;
    hw.positions = {0.0, 1.0};
    NativeGate x;
    x.name = "X";
    x.arity = 1;
    x.duration_ns = 500.0;
    hw.native_gates = {x};
    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 2});
    program.push_back(Instruction{Op::ApplyGate, Gate{"X", {0}, 0.0}});
    program.push_back(Instruction{Op::ApplyGate, Gate{"X", {1}, 0.0}});

    const service::SchedulerResult scheduled = service::schedule_program(program, hw);
    ASSERT_EQ(scheduled.program.size(), 3u);
    ASSERT_GE(scheduled.timeline.size(), 2u);
    double first_start = -1.0;
    double second_start = -1.0;
    int seen = 0;
    for (const auto& entry : scheduled.timeline) {
        if (entry.op == "ApplyGate") {
            if (seen == 0) {
                first_start = entry.start_time;
            } else if (seen == 1) {
                second_start = entry.start_time;
            }
            ++seen;
        }
    }
    ASSERT_EQ(seen, 2);
    EXPECT_DOUBLE_EQ(first_start, 0.0);
    EXPECT_DOUBLE_EQ(second_start, 0.0);
}

TEST(SchedulerTests, RespectsSingleQubitParallelLimit) {
    HardwareConfig hw;
    hw.positions = {0.0, 1.0};
    NativeGate x;
    x.name = "X";
    x.arity = 1;
    x.duration_ns = 500.0;
    hw.native_gates = {x};
    hw.timing_limits.max_parallel_single_qubit = 1;
    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 2});
    program.push_back(Instruction{Op::ApplyGate, Gate{"X", {0}, 0.0}});
    program.push_back(Instruction{Op::ApplyGate, Gate{"X", {1}, 0.0}});

    const service::SchedulerResult scheduled = service::schedule_program(program, hw);
    ASSERT_GE(scheduled.timeline.size(), 2u);
    std::vector<double> starts;
    for (const auto& entry : scheduled.timeline) {
        if (entry.op == "ApplyGate") {
            starts.push_back(entry.start_time);
        }
    }
    ASSERT_EQ(starts.size(), 2u);
    EXPECT_DOUBLE_EQ(starts[0], 0.0);
    EXPECT_GE(starts[1], starts[0] + 500.0);
}

TEST(SchedulerTests, RespectsTwoQubitParallelLimit) {
    HardwareConfig hw;
    hw.positions = {0.0, 1.0, 2.0, 3.0};
    NativeGate cx;
    cx.name = "CX";
    cx.arity = 2;
    cx.duration_ns = 1000.0;
    cx.connectivity = ConnectivityKind::AllToAll;
    hw.native_gates = {cx};
    hw.timing_limits.max_parallel_two_qubit = 1;
    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 4});
    program.push_back(Instruction{Op::ApplyGate, Gate{"CX", {0, 1}, 0.0}});
    program.push_back(Instruction{Op::ApplyGate, Gate{"CX", {2, 3}, 0.0}});

    const service::SchedulerResult scheduled = service::schedule_program(program, hw);
    ASSERT_GE(scheduled.timeline.size(), 2u);
    std::vector<double> starts;
    for (const auto& entry : scheduled.timeline) {
        if (entry.op == "ApplyGate") {
            starts.push_back(entry.start_time);
        }
    }
    ASSERT_EQ(starts.size(), 2u);
    EXPECT_GE(starts[1], starts[0] + 1000.0);
}
