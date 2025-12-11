#include "hardware_vm.hpp"

#ifdef NA_VM_WITH_STIM

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <stim/circuit/circuit.h>
#include <stim/mem/simd_bits.h>
#include <stim/simulators/tableau_simulator.h>

namespace {

constexpr double kNoiseEpsilon = 1e-12;
constexpr double kDefaultSingleQubitDurationNs = 500.0;
constexpr double kDefaultTwoQubitDurationNs = 1000.0;
constexpr double kDefaultMeasurementDurationNs = 50'000.0;

bool has_single_qubit_noise(const SingleQubitPauliConfig& cfg) {
    return std::abs(cfg.px) > kNoiseEpsilon ||
           std::abs(cfg.py) > kNoiseEpsilon ||
           std::abs(cfg.pz) > kNoiseEpsilon;
}

bool has_correlated_noise(const TwoQubitCorrelatedPauliConfig& cfg) {
    for (double value : cfg.matrix) {
        if (std::abs(value) > kNoiseEpsilon) {
            return true;
        }
    }
    return false;
}

struct StimMeasurementGroup {
    std::vector<int> targets;
    std::vector<std::size_t> indices;
};

std::string to_upper(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return text;
}

class StimCircuitBuilder {
  public:
    explicit StimCircuitBuilder(const DeviceProfile& profile) {
        if (profile.noise_config) {
            noise_holder_ = *profile.noise_config;
            noise_ = &(*noise_holder_);
            validate_noise_support();
        }
        initialize_gate_durations(profile.hardware.native_gates);
        const double measurement_duration = profile.hardware.timing_limits.measurement_duration_ns;
        if (measurement_duration > 0.0) {
            measurement_duration_ns_ = measurement_duration;
        }
        const double cooldown = profile.hardware.timing_limits.measurement_cooldown_ns;
        if (cooldown > 0.0) {
            measurement_cooldown_ns_ = cooldown;
        }
    }

    void translate(const std::vector<Instruction>& program) {
        for (const auto& instr : program) {
            switch (instr.op) {
                case Op::AllocArray:
                    {
                        const int n_qubits = std::get<int>(instr.payload);
                        if (n_qubits < 0) {
                            throw std::runtime_error("Stim backend received negative qubit allocation");
                        }
                        allocated_qubits_ += n_qubits;
                        has_allocation_ = true;
                        logical_time_ = 0.0;
                        qubit_ready_time_.assign(
                            static_cast<std::size_t>(allocated_qubits_), 0.0);
                        last_measure_time_.assign(
                            static_cast<std::size_t>(allocated_qubits_),
                            -std::numeric_limits<double>::infinity());
                    }
                    break;
                case Op::ApplyGate:
                    apply_gate(std::get<Gate>(instr.payload));
                    break;
                case Op::Measure:
                    append_measure(std::get<std::vector<int>>(instr.payload));
                    break;
                case Op::Wait:
                    append_wait(std::get<WaitInstruction>(instr.payload).duration);
                    break;
                case Op::MoveAtom:
                case Op::Pulse:
                    throw std::runtime_error("Stim backend does not support move/pulse instructions");
            }
        }
        if (allocated_qubits_ < 0) {
            throw std::runtime_error("Stim backend requires AllocArray before other ops");
        }
    }

    stim::Circuit&& finish() {
        if (!has_allocation_ || allocated_qubits_ <= 0) {
            throw std::runtime_error("Stim backend cannot finalize circuit without allocation");
        }
        return std::move(circuit_);
    }

    const std::vector<StimMeasurementGroup>& groups() const { return measurement_groups_; }
    std::size_t measurement_count() const { return measurement_cursor_; }
    const SimpleNoiseConfig* noise() const { return noise_; }
    const std::vector<BackendTimelineEvent>& timeline() const { return timeline_; }

  private:
    void validate_noise_support() const {
        if (!noise_) {
            return;
        }
        const auto& cfg = *noise_;
        if (cfg.idle_rate > 0.0 ||
            cfg.phase.single_qubit > 0.0 ||
            cfg.phase.two_qubit_control > 0.0 ||
            cfg.phase.two_qubit_target > 0.0 ||
            cfg.phase.idle > 0.0 ||
            cfg.amplitude_damping.per_gate > 0.0 ||
            cfg.amplitude_damping.idle_rate > 0.0 ||
            cfg.loss_runtime.per_gate > 0.0 ||
            cfg.loss_runtime.idle_rate > 0.0) {
            throw std::runtime_error(
                "Stim backend supports only Pauli/loss measurement noise; idle/phase/amplitude/loss_runtime terms "
                "are unsupported");
        }
        if (has_correlated_noise(cfg.correlated_gate)) {
            throw std::runtime_error("Stim backend does not yet support correlated two-qubit noise");
        }
    }

    void require_allocation() const {
        if (!has_allocation_ || allocated_qubits_ <= 0) {
            throw std::runtime_error("Stim backend received gate before AllocArray");
        }
    }

    void ensure_target_range(int target) const {
        if (target < 0 || target >= allocated_qubits_) {
            throw std::runtime_error("Stim backend target index is out of range");
        }
    }

    void apply_gate(const Gate& gate) {
        require_allocation();
        if (gate.targets.empty()) {
            throw std::runtime_error("Stim backend encountered gate with no targets");
        }
        if (std::abs(gate.param) > kNoiseEpsilon) {
            throw std::runtime_error("Stim backend does not support parameterized gates");
        }
        const std::string name = to_upper(gate.name);
        if (gate.targets.size() == 1) {
            const int target = gate.targets[0];
            ensure_target_range(target);
            if (name == "X") {
                emit_single_qubit_gate("X", target);
            } else if (name == "Y") {
                emit_single_qubit_gate("Y", target);
            } else if (name == "Z") {
                emit_single_qubit_gate("Z", target);
            } else if (name == "H") {
                emit_single_qubit_gate("H", target);
            } else if (name == "S") {
                emit_single_qubit_gate("S", target);
            } else if (name == "SDG" || name == "S_DAG") {
                emit_single_qubit_gate("S_DAG", target);
            } else {
                throw std::runtime_error("Stim backend does not support gate '" + gate.name + "'");
            }
            append_single_qubit_noise(target);
            return;
        }
        if (gate.targets.size() == 2) {
            const int q0 = gate.targets[0];
            const int q1 = gate.targets[1];
            ensure_target_range(q0);
            ensure_target_range(q1);
            if (name == "CX" || name == "CNOT") {
                emit_two_qubit_gate("CX", q0, q1);
            } else if (name == "CZ") {
                emit_two_qubit_gate("CZ", q0, q1);
            } else {
                throw std::runtime_error("Stim backend does not support two-qubit gate '" + gate.name + "'");
            }
            append_two_qubit_noise(q0, q1);
            return;
        }
        throw std::runtime_error("Stim backend only supports 1Q/2Q gates");
    }

    static std::string format_targets(const std::vector<uint32_t>& targets) {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < targets.size(); ++i) {
            if (i > 0) {
                oss << ",";
            }
            oss << targets[i];
        }
        oss << "]";
        return oss.str();
    }

    void record_timeline_event(
        const std::string& op,
        const std::string& detail,
        double start_time,
        double duration
    ) {
        const double safe_duration = std::max(0.0, duration);
        timeline_.push_back(BackendTimelineEvent{start_time, safe_duration, op, detail});
        last_event_start_time_ = start_time;
    }

    double gate_duration_ns(const std::string& gate, int arity) const {
        const auto key = std::make_pair(gate, arity);
        auto it = gate_durations_.find(key);
        if (it != gate_durations_.end() && it->second > 0.0) {
            return it->second;
        }
        return arity == 1 ? kDefaultSingleQubitDurationNs : kDefaultTwoQubitDurationNs;
    }

    double earliest_start_for_targets(const std::vector<int>& targets) const {
        double start = logical_time_;
        for (int target : targets) {
            if (target < 0 || target >= allocated_qubits_) {
                continue;
            }
            const std::size_t idx = static_cast<std::size_t>(target);
            if (idx < qubit_ready_time_.size()) {
                start = std::max(start, qubit_ready_time_[idx]);
            }
            if (idx < last_measure_time_.size()) {
                start = std::max(start, last_measure_time_[idx] + measurement_cooldown_ns_);
            }
        }
        // Ensure we don't go backwards relative to the last emitted event.
        start = std::max(start, last_event_start_time_);
        return start;
    }

    void emit_single_qubit_gate(
        const std::string& gate,
        int target
    ) {
        std::vector<uint32_t> targets{static_cast<uint32_t>(target)};
        circuit_.safe_append_u(gate, targets);
        const double duration = gate_duration_ns(gate, 1);
        std::vector<int> logical_targets{target};
        const double start_time = earliest_start_for_targets(logical_targets);
        record_timeline_event(
            gate,
            "targets=" + format_targets(targets),
            start_time,
            duration);
        const double end_time = start_time + duration;
        if (static_cast<std::size_t>(target) < qubit_ready_time_.size()) {
            qubit_ready_time_[static_cast<std::size_t>(target)] = end_time;
        }
        logical_time_ = std::max(logical_time_, start_time);
    }

    void emit_two_qubit_gate(
        const std::string& gate,
        int q0,
        int q1
    ) {
        std::vector<uint32_t> targets{
            static_cast<uint32_t>(q0),
            static_cast<uint32_t>(q1)
        };
        circuit_.safe_append_u(gate, targets);
        const double duration = gate_duration_ns(gate, 2);
        std::vector<int> logical_targets{q0, q1};
        const double start_time = earliest_start_for_targets(logical_targets);
        record_timeline_event(
            gate,
            "targets=" + format_targets(targets),
            start_time,
            duration);
        const double end_time = start_time + duration;
        if (static_cast<std::size_t>(q0) < qubit_ready_time_.size()) {
            qubit_ready_time_[static_cast<std::size_t>(q0)] = end_time;
        }
        if (static_cast<std::size_t>(q1) < qubit_ready_time_.size()) {
            qubit_ready_time_[static_cast<std::size_t>(q1)] = end_time;
        }
        logical_time_ = std::max(logical_time_, start_time);
    }

    void append_single_qubit_noise(int target) {
        if (!noise_) {
            return;
        }
        const auto& cfg = noise_->gate.single_qubit;
        if (!has_single_qubit_noise(cfg)) {
            return;
        }
        std::vector<uint32_t> targets{static_cast<uint32_t>(target)};
        std::vector<double> args{cfg.px, cfg.py, cfg.pz};
        circuit_.safe_append_u("PAULI_CHANNEL_1", targets, args);
        const double start_time = earliest_start_for_targets(std::vector<int>{target});
        record_timeline_event(
            "PAULI_CHANNEL_1",
            "targets=" + format_targets(targets) +
                " px=" + std::to_string(cfg.px) +
                " py=" + std::to_string(cfg.py) +
                " pz=" + std::to_string(cfg.pz),
            start_time,
            0.0);
    }

    void append_two_qubit_noise(int control, int target) {
        if (!noise_) {
            return;
        }
        const auto& ctrl_cfg = noise_->gate.two_qubit_control;
        if (has_single_qubit_noise(ctrl_cfg)) {
            std::vector<uint32_t> targets{static_cast<uint32_t>(control)};
            std::vector<double> args{ctrl_cfg.px, ctrl_cfg.py, ctrl_cfg.pz};
            circuit_.safe_append_u("PAULI_CHANNEL_1", targets, args);
            record_timeline_event(
                "PAULI_CHANNEL_1",
                "targets=" + format_targets(targets) +
                    " px=" + std::to_string(ctrl_cfg.px) +
                    " py=" + std::to_string(ctrl_cfg.py) +
                    " pz=" + std::to_string(ctrl_cfg.pz),
                earliest_start_for_targets(std::vector<int>{control}),
                0.0);
        }
        const auto& tgt_cfg = noise_->gate.two_qubit_target;
        if (has_single_qubit_noise(tgt_cfg)) {
            std::vector<uint32_t> targets{static_cast<uint32_t>(target)};
            std::vector<double> args{tgt_cfg.px, tgt_cfg.py, tgt_cfg.pz};
            circuit_.safe_append_u("PAULI_CHANNEL_1", targets, args);
            record_timeline_event(
                "PAULI_CHANNEL_1",
                "targets=" + format_targets(targets) +
                    " px=" + std::to_string(tgt_cfg.px) +
                    " py=" + std::to_string(tgt_cfg.py) +
                    " pz=" + std::to_string(tgt_cfg.pz),
                earliest_start_for_targets(std::vector<int>{target}),
                0.0);
        }
    }

    void append_measure(
        const std::vector<int>& targets
    ) {
        if (targets.empty()) {
            return;
        }
        require_allocation();
        StimMeasurementGroup group;
        group.targets = targets;
        group.indices.reserve(targets.size());
        for (int target : targets) {
            ensure_target_range(target);
            group.indices.push_back(measurement_cursor_);
            std::vector<uint32_t> stim_targets{static_cast<uint32_t>(target)};
            circuit_.safe_append_u("M", stim_targets);
            ++measurement_cursor_;
        }
        measurement_groups_.push_back(std::move(group));
        std::vector<int> logical_targets = targets;
        const double start_time = earliest_start_for_targets(logical_targets);
        record_timeline_event(
            "M",
            "targets=" + format_targets(std::vector<uint32_t>(targets.begin(), targets.end())),
            start_time,
            measurement_duration_ns_);
        const double end_time = start_time + measurement_duration_ns_;
        for (int target : targets) {
            if (target < 0 || target >= allocated_qubits_) {
                continue;
            }
            const std::size_t idx = static_cast<std::size_t>(target);
            if (idx < last_measure_time_.size()) {
                last_measure_time_[idx] = end_time;
            }
            if (idx < qubit_ready_time_.size()) {
                qubit_ready_time_[idx] = end_time;
            }
        }
        logical_time_ = std::max(logical_time_, end_time);
    }

    void append_wait(
        double duration
    ) {
        circuit_.safe_append_u("TICK", std::vector<uint32_t>{});
        const double safe_duration = std::max(0.0, duration);
        const double start_time = logical_time_;
        record_timeline_event(
            "Wait",
            "duration_ns=" + std::to_string(duration),
            start_time,
            safe_duration);
        const double end_time = start_time + safe_duration;
        logical_time_ = end_time;
        for (double& ready : qubit_ready_time_) {
            ready = std::max(ready, end_time);
        }
    }

    void initialize_gate_durations(const std::vector<NativeGate>& gates) {
        for (const auto& gate : gates) {
            gate_durations_[std::make_pair(to_upper(gate.name), gate.arity)] = gate.duration_ns;
        }
    }

    stim::Circuit circuit_;
    int allocated_qubits_ = 0;
    std::optional<SimpleNoiseConfig> noise_holder_;
    const SimpleNoiseConfig* noise_ = nullptr;
    std::vector<StimMeasurementGroup> measurement_groups_;
    std::size_t measurement_cursor_ = 0;
    bool has_allocation_ = false;
    std::vector<BackendTimelineEvent> timeline_;
    double logical_time_ = 0.0;
    std::vector<double> qubit_ready_time_;
    std::vector<double> last_measure_time_;
    double last_event_start_time_ = 0.0;
    std::map<std::pair<std::string, int>, double> gate_durations_;
    double measurement_duration_ns_ = kDefaultMeasurementDurationNs;
    double measurement_cooldown_ns_ = kDefaultMeasurementDurationNs;
};

void push_noise_log(
    std::vector<ExecutionLog>& logs,
    int shot_index,
    const std::string& message
) {
    ExecutionLog log;
    log.shot = shot_index;
    log.logical_time = 0.0;
    log.category = "Noise";
    log.message = message;
    logs.push_back(std::move(log));
}

void apply_measurement_noise(
    std::vector<MeasurementRecord>& records,
    const SimpleNoiseConfig& cfg,
    int shot_index,
    std::mt19937_64& rng,
    std::vector<ExecutionLog>& logs
) {
    const bool has_loss = cfg.p_loss > 0.0;
    const bool has_quantum = cfg.p_quantum_flip > 0.0;
    const bool has_readout = cfg.readout.p_flip0_to_1 > 0.0 || cfg.readout.p_flip1_to_0 > 0.0;
    if (!has_loss && !has_quantum && !has_readout) {
        return;
    }
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (auto& record : records) {
        for (std::size_t idx = 0; idx < record.bits.size(); ++idx) {
            int& bit = record.bits[idx];
            const int target = record.targets[idx];
            if (has_loss && dist(rng) < cfg.p_loss) {
                bit = -1;
                push_noise_log(
                    logs,
                    shot_index,
                    "type=measure_loss qubit=" + std::to_string(target) +
                        " p_loss=" + std::to_string(cfg.p_loss)
                );
                continue;
            }
            if (bit == -1) {
                continue;
            }
            if (has_quantum && dist(rng) < cfg.p_quantum_flip) {
                const int before = bit;
                bit = bit == 0 ? 1 : 0;
                push_noise_log(
                    logs,
                    shot_index,
                    "type=measure_quantum_flip qubit=" + std::to_string(target) +
                        " before=" + std::to_string(before) +
                        " after=" + std::to_string(bit) +
                        " p_quantum_flip=" + std::to_string(cfg.p_quantum_flip)
                );
            }
            if (!has_readout || bit == -1) {
                continue;
            }
            const double r = dist(rng);
            if (bit == 0 && r < cfg.readout.p_flip0_to_1) {
                bit = 1;
                push_noise_log(
                    logs,
                    shot_index,
                    "type=measure_readout_flip qubit=" + std::to_string(target) +
                        " before=0 after=1 p01=" + std::to_string(cfg.readout.p_flip0_to_1)
                );
            } else if (bit == 1 && r < cfg.readout.p_flip1_to_0) {
                bit = 0;
                push_noise_log(
                    logs,
                    shot_index,
                    "type=measure_readout_flip qubit=" + std::to_string(target) +
                        " before=1 after=0 p10=" + std::to_string(cfg.readout.p_flip1_to_0)
                );
            }
        }
    }
}

}  // namespace

HardwareVM::RunResult HardwareVM::run_stabilizer(
    const std::vector<Instruction>& program,
    int shots,
    const std::vector<std::uint64_t>& shot_seeds
) {
    StimCircuitBuilder builder(profile_);
    builder.translate(program);
    stim::Circuit circuit;
    const std::size_t measurement_count = builder.measurement_count();
    if (profile_.stim_circuit_text) {
        circuit = stim::Circuit(*profile_.stim_circuit_text);
        if (static_cast<std::size_t>(circuit.count_measurements()) != measurement_count) {
            throw std::runtime_error("Stim backend measurement mismatch for provided circuit");
        }
    } else {
        circuit = builder.finish();
        if (static_cast<std::size_t>(circuit.count_measurements()) != measurement_count) {
            throw std::runtime_error("Stim backend measurement bookkeeping mismatch");
        }
    }

    RunResult result;
    result.measurements.reserve(static_cast<std::size_t>(shots) * builder.groups().size());
    std::vector<MeasurementRecord> shot_records;
    std::vector<ExecutionLog> shot_logs;
    const std::size_t program_steps = program.size();
    const bool has_progress = (progress_reporter_ != nullptr);

    for (int shot = 0; shot < shots; ++shot) {
        std::mt19937_64 stim_rng(shot_seeds[shot]);
        stim::simd_bits<64> sample = stim::TableauSimulator<64>::sample_circuit(circuit, stim_rng);

        shot_records.clear();
        shot_records.reserve(builder.groups().size());
        for (const auto& group : builder.groups()) {
            MeasurementRecord record;
            record.targets = group.targets;
            record.bits.resize(group.indices.size());
            for (std::size_t idx = 0; idx < group.indices.size(); ++idx) {
                const std::size_t bit_index = group.indices[idx];
                if (bit_index >= measurement_count) {
                    throw std::runtime_error("Stim backend sampled fewer bits than expected");
                }
                record.bits[idx] = static_cast<bool>(sample[bit_index]) ? 1 : 0;
            }
            shot_records.push_back(std::move(record));
        }

        shot_logs.clear();
        if (const auto* noise_cfg = builder.noise()) {
            std::mt19937_64 noise_rng(shot_seeds[shot] ^ 0x9e3779b97f4a7c15ULL);
            apply_measurement_noise(shot_records, *noise_cfg, shot, noise_rng, shot_logs);
        }

        for (auto& record : shot_records) {
            result.measurements.push_back(std::move(record));
        }
        result.logs.insert(result.logs.end(), shot_logs.begin(), shot_logs.end());
        if (has_progress) {
            for (std::size_t step = 0; step < program_steps; ++step) {
                progress_reporter_->increment_completed_steps();
            }
        }
    }

    result.backend_timeline = builder.timeline();
    return result;
}

#endif  // NA_VM_WITH_STIM
