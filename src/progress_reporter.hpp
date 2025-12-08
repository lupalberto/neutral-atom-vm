#pragma once

#include "vm/measurement_record.types.hpp"

#include <cstddef>

namespace neutral_atom_vm {

class ProgressReporter {
  public:
    virtual ~ProgressReporter() = default;

    virtual void set_total_steps(std::size_t total_steps) = 0;
    virtual void increment_completed_steps(std::size_t delta = 1) = 0;
    virtual void record_log(const ExecutionLog& log) = 0;
};

}  // namespace neutral_atom_vm
