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
