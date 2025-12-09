#include "service/scheduler.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <string>

namespace service {

namespace {

const NativeGate* find_native_gate(const HardwareConfig& hw, const Gate& gate) {
    for (const auto& candidate : hw.native_gates) {
        if (candidate.name == gate.name && candidate.arity == static_cast<int>(gate.targets.size())) {
            return &candidate;
        }
    }
    return nullptr;
}

std::string format_targets(const std::vector<int>& targets) {
    std::ostringstream oss;
    oss << "[";
    for (std::size_t idx = 0; idx < targets.size(); ++idx) {
        if (idx > 0) {
            oss << ",";
        }
        oss << targets[idx];
    }
    oss << "]";
    return oss.str();
}

std::string describe_gate(const Gate& gate) {
    std::ostringstream oss;
    oss << gate.name << " targets=" << format_targets(gate.targets);
    oss << " param=" << gate.param;
    return oss.str();
}

std::string describe_measure(const std::vector<int>& targets) {
    std::ostringstream oss;
    oss << "targets=" << format_targets(targets);
    return oss.str();
}

std::string describe_wait(double duration) {
    std::ostringstream oss;
    oss << "duration_ns=" << duration;
    return oss.str();
}

std::string describe_pulse(const PulseInstruction& pulse) {
    std::ostringstream oss;
    oss << "target=" << pulse.target;
    oss << " detuning=" << pulse.detuning;
    oss << " duration_ns=" << pulse.duration;
    return oss.str();
}

struct SchedulingState {
    double logical_time = 0.0;
    std::vector<double> last_measurement_time;
    std::vector<TimelineEntry>* timeline = nullptr;
};

void record_timeline(
    SchedulingState& state,
    double start_time,
    double duration,
    const std::string& op,
    const std::string& detail
) {
    if (!state.timeline) {
        return;
    }
    state.timeline->push_back(TimelineEntry{start_time, duration, op, detail});
}

void append_wait_instruction(
    std::vector<Instruction>& out,
    SchedulingState& state,
    double duration,
    const TimingLimits& limits,
    const std::string& detail
) {
    if (duration <= 0.0) {
        return;
    }
    double remaining = duration;
    const double min_wait = limits.min_wait_ns;
    const double max_wait = limits.max_wait_ns;

    while (remaining > 0.0) {
        double chunk = remaining;
        if (max_wait > 0.0 && chunk > max_wait) {
            chunk = max_wait;
        }
        if (min_wait > 0.0 && chunk < min_wait) {
            chunk = min_wait;
        }
        if (chunk <= 0.0) {
            chunk = min_wait > 0.0 ? min_wait : remaining;
        }
        Instruction wait_instr;
        wait_instr.op = Op::Wait;
        WaitInstruction payload;
        payload.duration = chunk;
        wait_instr.payload = payload;
        const double start_time = state.logical_time;
        out.push_back(wait_instr);
        state.logical_time += chunk;
        const std::string detail_with_duration =
            detail.empty() ? describe_wait(chunk) : detail + " " + describe_wait(chunk);
        record_timeline(state, start_time, chunk, "Wait", detail_with_duration);
        remaining -= chunk;
        if (remaining <= 0.0) {
            break;
        }
    }
}

void enforce_measurement_cooldown(
    std::vector<Instruction>& out,
    SchedulingState& state,
    const HardwareConfig& hw,
    const Gate& gate
) {
    const double cooldown = hw.timing_limits.measurement_cooldown_ns;
    if (cooldown <= 0.0) {
        return;
    }
    double target_time = state.logical_time;
    for (int target : gate.targets) {
        if (target < 0 || target >= static_cast<int>(state.last_measurement_time.size())) {
            continue;
        }
        const double last_time = state.last_measurement_time[static_cast<std::size_t>(target)];
        target_time = std::max(target_time, last_time + cooldown);
    }
    if (target_time > state.logical_time) {
        append_wait_instruction(
            out,
            state,
            target_time - state.logical_time,
            hw.timing_limits,
            "Inserted for measurement cooldown"
        );
    }
}

}  // namespace

SchedulerResult schedule_program(
    const std::vector<Instruction>& program,
    const HardwareConfig& hardware_config
) {
    SchedulerResult result;
    std::vector<Instruction>& scheduled = result.program;
    scheduled.reserve(program.size());

    SchedulingState state;
    state.timeline = &result.timeline;

    for (const auto& instr : program) {
        switch (instr.op) {
            case Op::AllocArray: {
                scheduled.push_back(instr);
                const int n = std::get<int>(instr.payload);
                state.logical_time = 0.0;
                state.last_measurement_time.assign(
                    static_cast<std::size_t>(std::max(0, n)),
                    -std::numeric_limits<double>::infinity()
                );
                break;
            }
            case Op::ApplyGate: {
                const Gate& gate = std::get<Gate>(instr.payload);
                enforce_measurement_cooldown(scheduled, state, hardware_config, gate);
                scheduled.push_back(instr);
                const double start_time = state.logical_time;
                double duration = 0.0;
                if (const NativeGate* native = find_native_gate(hardware_config, gate)) {
                    duration = native->duration_ns;
                }
                state.logical_time += duration;
                record_timeline(state, start_time, duration, "ApplyGate", describe_gate(gate));
                break;
            }
            case Op::Measure: {
                scheduled.push_back(instr);
                const double start_time = state.logical_time;
                const double duration = hardware_config.timing_limits.measurement_duration_ns;
                state.logical_time += duration;
                const auto& targets = std::get<std::vector<int>>(instr.payload);
                for (int target : targets) {
                    if (target < 0 || target >= static_cast<int>(state.last_measurement_time.size())) {
                        continue;
                    }
                    state.last_measurement_time[static_cast<std::size_t>(target)] = state.logical_time;
                }
                record_timeline(state, start_time, duration, "Measure", describe_measure(targets));
                break;
            }
            case Op::Wait: {
                scheduled.push_back(instr);
                const double start_time = state.logical_time;
                const double duration = std::get<WaitInstruction>(instr.payload).duration;
                state.logical_time += duration;
                record_timeline(state, start_time, duration, "Wait", describe_wait(duration));
                break;
            }
            case Op::Pulse: {
                scheduled.push_back(instr);
                const double start_time = state.logical_time;
                const auto& pulse = std::get<PulseInstruction>(instr.payload);
                const double duration = pulse.duration;
                state.logical_time += duration;
                record_timeline(state, start_time, duration, "Pulse", describe_pulse(pulse));
                break;
            }
            default:
                scheduled.push_back(instr);
                break;
        }
    }

    return result;
}

}  // namespace service
