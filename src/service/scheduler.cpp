#include "service/scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>

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
    std::vector<double> qubit_ready_time;
    std::vector<int> qubit_zones;
    std::vector<TimelineEntry>* timeline = nullptr;
    struct ActiveOp {
        double end_time = 0.0;
        int arity = 1;
        std::vector<int> zones;
    };
    std::vector<ActiveOp> active_ops;
    int active_single_qubit = 0;
    int active_multi_qubit = 0;
    std::unordered_map<int, int> active_zone_counts;
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

void sync_all_qubits_to_time(SchedulingState& state) {
    for (auto& ready : state.qubit_ready_time) {
        ready = std::max(ready, state.logical_time);
    }
}

void prune_active_ops(SchedulingState& state, double current_time) {
    auto& ops = state.active_ops;
    auto it = ops.begin();
    while (it != ops.end()) {
        if (it->end_time <= current_time) {
            if (it->arity <= 1) {
                state.active_single_qubit = std::max(0, state.active_single_qubit - 1);
            } else {
                state.active_multi_qubit = std::max(0, state.active_multi_qubit - 1);
            }
            for (int zone : it->zones) {
                auto zone_it = state.active_zone_counts.find(zone);
                if (zone_it != state.active_zone_counts.end()) {
                    if (--zone_it->second <= 0) {
                        state.active_zone_counts.erase(zone_it);
                    }
                }
            }
            it = ops.erase(it);
        } else {
            ++it;
        }
    }
}

double next_active_completion(const SchedulingState& state) {
    double next_time = std::numeric_limits<double>::infinity();
    for (const auto& op : state.active_ops) {
        next_time = std::min(next_time, op.end_time);
    }
    return next_time;
}

std::vector<int> zones_for_targets(const SchedulingState& state, const std::vector<int>& targets) {
    std::vector<int> zones;
    zones.reserve(targets.size());
    for (int target : targets) {
        if (target < 0 || target >= static_cast<int>(state.qubit_zones.size())) {
            if (std::find(zones.begin(), zones.end(), 0) == zones.end()) {
                zones.push_back(0);
            }
            continue;
        }
        int zone = state.qubit_zones[static_cast<std::size_t>(target)];
        if (std::find(zones.begin(), zones.end(), zone) == zones.end()) {
            zones.push_back(zone);
        }
    }
    if (zones.empty()) {
        zones.push_back(0);
    }
    return zones;
}

bool parallel_limits_satisfied(
    const SchedulingState& state,
    const TimingLimits& limits,
    int arity,
    const std::vector<int>& zones
) {
    if (arity <= 1) {
        if (limits.max_parallel_single_qubit > 0 &&
            state.active_single_qubit + 1 > limits.max_parallel_single_qubit) {
            return false;
        }
    } else {
        if (limits.max_parallel_two_qubit > 0 &&
            state.active_multi_qubit + 1 > limits.max_parallel_two_qubit) {
            return false;
        }
    }
    if (limits.max_parallel_per_zone > 0) {
        for (int zone : zones) {
            auto it = state.active_zone_counts.find(zone);
            const int current = it == state.active_zone_counts.end() ? 0 : it->second;
            if (current + 1 > limits.max_parallel_per_zone) {
                return false;
            }
        }
    }
    return true;
}

double enforce_parallel_limits(
    SchedulingState& state,
    const TimingLimits& limits,
    int arity,
    const std::vector<int>& zones,
    double start_time
) {
    if (limits.max_parallel_single_qubit <= 0 &&
        limits.max_parallel_two_qubit <= 0 &&
        limits.max_parallel_per_zone <= 0) {
        return start_time;
    }
    double candidate = start_time;
    while (true) {
        prune_active_ops(state, candidate);
        if (parallel_limits_satisfied(state, limits, arity, zones)) {
            return candidate;
        }
        const double next_time = next_active_completion(state);
        if (!std::isfinite(next_time)) {
            return candidate;
        }
        candidate = std::max(candidate, next_time);
    }
}

void track_active_gate(
    SchedulingState& state,
    int arity,
    const std::vector<int>& zones,
    double end_time
) {
    SchedulingState::ActiveOp op;
    op.end_time = end_time;
    op.arity = arity;
    op.zones = zones;
    state.active_ops.push_back(op);
    if (arity <= 1) {
        state.active_single_qubit += 1;
    } else {
        state.active_multi_qubit += 1;
    }
    for (int zone : zones) {
        state.active_zone_counts[zone] += 1;
    }
}

double align_with_idle_window(SchedulingState& state, double candidate_start) {
    double start = candidate_start;
    while (true) {
        prune_active_ops(state, start);
        if (state.active_ops.empty()) {
            return start;
        }
        const double next_time = next_active_completion(state);
        if (!std::isfinite(next_time)) {
            return start;
        }
        start = std::max(start, next_time);
    }
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
        sync_all_qubits_to_time(state);
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
    const SiteIndexMap site_lookup = build_site_index(hardware_config);

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
                state.qubit_ready_time.assign(
                    static_cast<std::size_t>(std::max(0, n)),
                    0.0
                );
                state.qubit_zones.assign(static_cast<std::size_t>(std::max(0, n)), 0);
                for (std::size_t idx = 0; idx < state.qubit_zones.size(); ++idx) {
                    state.qubit_zones[idx] = zone_for_slot(
                        hardware_config, site_lookup, static_cast<int>(idx)
                    );
                }
                state.active_ops.clear();
                state.active_single_qubit = 0;
                state.active_multi_qubit = 0;
                state.active_zone_counts.clear();
                break;
            }
            case Op::ApplyGate: {
                const Gate& gate = std::get<Gate>(instr.payload);
                enforce_measurement_cooldown(scheduled, state, hardware_config, gate);
                double duration = 0.0;
                if (const NativeGate* native = find_native_gate(hardware_config, gate)) {
                    duration = native->duration_ns;
                }
                double start_time = 0.0;
                for (int target : gate.targets) {
                    if (target < 0 || target >= static_cast<int>(state.qubit_ready_time.size())) {
                        continue;
                    }
                    start_time = std::max(start_time, state.qubit_ready_time[static_cast<std::size_t>(target)]);
                }
                const std::vector<int> zones = zones_for_targets(state, gate.targets);
                start_time = enforce_parallel_limits(
                    state,
                    hardware_config.timing_limits,
                    static_cast<int>(gate.targets.size()),
                    zones,
                    start_time
                );
                if (start_time > state.logical_time) {
                    append_wait_instruction(
                        scheduled,
                        state,
                        start_time - state.logical_time,
                        hardware_config.timing_limits,
                        "Inserted for scheduling gap"
                    );
                }
                scheduled.push_back(instr);
                const double end_time = start_time + duration;
                record_timeline(state, start_time, duration, "ApplyGate", describe_gate(gate));
                if (duration > 0.0) {
                    track_active_gate(state, static_cast<int>(gate.targets.size()), zones, end_time);
                }
                for (int target : gate.targets) {
                    if (target < 0 || target >= static_cast<int>(state.qubit_ready_time.size())) {
                        continue;
                    }
                    state.qubit_ready_time[static_cast<std::size_t>(target)] = end_time;
                }
                state.logical_time = std::max(state.logical_time, start_time) + duration;
                break;
            }
            case Op::Measure: {
                const auto& targets = std::get<std::vector<int>>(instr.payload);
                double start_time = state.logical_time;
                for (int target : targets) {
                    if (target < 0 || target >= static_cast<int>(state.qubit_ready_time.size())) {
                        continue;
                    }
                    start_time = std::max(start_time, state.qubit_ready_time[static_cast<std::size_t>(target)]);
                }
                start_time = align_with_idle_window(state, start_time);
                if (start_time > state.logical_time) {
                    append_wait_instruction(
                        scheduled,
                        state,
                        start_time - state.logical_time,
                        hardware_config.timing_limits,
                        "Inserted before measurement"
                    );
                }
                scheduled.push_back(instr);
                const double duration = hardware_config.timing_limits.measurement_duration_ns;
                state.logical_time = std::max(state.logical_time, start_time) + duration;
                for (int target : targets) {
                    if (target < 0 || target >= static_cast<int>(state.last_measurement_time.size())) {
                        continue;
                    }
                    const std::size_t idx = static_cast<std::size_t>(target);
                    state.last_measurement_time[idx] = state.logical_time;
                    if (idx < state.qubit_ready_time.size()) {
                        state.qubit_ready_time[idx] = state.logical_time;
                    }
                }
                sync_all_qubits_to_time(state);
                record_timeline(state, start_time, duration, "Measure", describe_measure(targets));
                break;
            }
            case Op::Wait: {
                scheduled.push_back(instr);
                const double start_time = state.logical_time;
                const double duration = std::get<WaitInstruction>(instr.payload).duration;
                state.logical_time += duration;
                sync_all_qubits_to_time(state);
                record_timeline(state, start_time, duration, "Wait", describe_wait(duration));
                break;
            }
            case Op::Pulse: {
                scheduled.push_back(instr);
                const double start_time = state.logical_time;
                const auto& pulse = std::get<PulseInstruction>(instr.payload);
                const double duration = pulse.duration;
                state.logical_time += duration;
                sync_all_qubits_to_time(state);
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
