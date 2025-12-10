#pragma once

namespace neutral_atom_vm {

struct InstructionTiming {
    double start_time = 0.0;
    double duration = 0.0;
    bool valid = false;
};

}  // namespace neutral_atom_vm
