#pragma once

#include <vector>
#include <unordered_map>

#include "service/timeline.hpp"
#include "vm/isa.hpp"

namespace service {

struct SchedulerResult {
    std::vector<Instruction> program;
    std::vector<TimelineEntry> timeline;
};

SchedulerResult schedule_program(
    const std::vector<Instruction>& program,
    const HardwareConfig& hardware_config
);

}  // namespace service
