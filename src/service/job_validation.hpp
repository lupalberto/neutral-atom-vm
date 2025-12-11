#pragma once

#include "vm/isa.hpp"

#include <vector>

namespace service {

void validate_blockade_constraints(
    const HardwareConfig& hardware,
    const std::vector<Instruction>& program
);

void validate_transport_constraints(
    const HardwareConfig& hardware,
    const std::vector<Instruction>& program
);

}  // namespace service
