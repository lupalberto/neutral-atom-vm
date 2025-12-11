#include "service/job.hpp"
#include "hardware_vm.hpp"
#include "noise/device_noise_builder.hpp"
#include "service/scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
constexpr double kNanosecondsPerMicrosecond = 1000.0;
constexpr double kMicrosecondsPerNanosecond = 1.0 / kNanosecondsPerMicrosecond;
constexpr const char* kDisplayTimeUnit = "us";
constexpr double kDefaultSingleQubitDurationNs = 500.0;
constexpr double kDefaultTwoQubitDurationNs = 1000.0;
constexpr double kDefaultMeasurementDurationNs = 50000.0;

std::string escape_json(const std::string& str) {
    std::ostringstream out;
    for (const char ch : str) {
        switch (ch) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << ch;
        }
    }
    return out.str();
}

std::vector<ExecutionLog> build_timeline_logs(const std::vector<service::TimelineEntry>& timeline) {
    std::vector<ExecutionLog> entries;
    entries.reserve(timeline.size());
    for (const auto& event : timeline) {
        ExecutionLog log;
        log.shot = 0;
        log.logical_time = event.start_time;
        log.category = "Timeline";
        std::ostringstream oss;
        oss << event.op;
        if (!event.detail.empty()) {
            oss << " " << event.detail;
        }
        oss << " duration_us=" << event.duration;
        log.message = oss.str();
        entries.push_back(std::move(log));
    }
    return entries;
}

void convert_timeline_to_microseconds(std::vector<service::TimelineEntry>& timeline) {
    for (auto& entry : timeline) {
        entry.start_time *= kMicrosecondsPerNanosecond;
        entry.duration *= kMicrosecondsPerNanosecond;
    }
}

void convert_logs_to_microseconds(std::vector<ExecutionLog>& logs) {
    for (auto& entry : logs) {
        entry.logical_time *= kMicrosecondsPerNanosecond;
    }
}

void append_int_array(const std::vector<int>& values, std::ostringstream& out) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << values[i];
    }
    out << ']';
}

void append_double_matrix(const std::vector<std::vector<double>>& values, std::ostringstream& out) {
    out << '[';
    for (std::size_t row = 0; row < values.size(); ++row) {
        if (row > 0) {
            out << ',';
        }
        out << '[';
        for (std::size_t col = 0; col < values[row].size(); ++col) {
            if (col > 0) {
                out << ',';
            }
            out << values[row][col];
        }
        out << ']';
    }
    out << ']';
}

std::string connectivity_to_string(ConnectivityKind kind) {
    switch (kind) {
        case ConnectivityKind::AllToAll:
            return "AllToAll";
        case ConnectivityKind::NearestNeighborChain:
            return "NearestNeighborChain";
        case ConnectivityKind::NearestNeighborGrid:
            return "NearestNeighborGrid";
    }
    return "AllToAll";
}

void append_site_descriptor(const SiteDescriptor& site, std::ostringstream& out) {
    out << "{\"id\":" << site.id
        << ",\"x\":" << site.x
        << ",\"y\":" << site.y
        << ",\"z\":" << site.z
        << ",\"zone_id\":" << site.zone_id << '}';
}

void append_sites_json(const std::vector<SiteDescriptor>& sites, std::ostringstream& out) {
    out << '[';
    for (std::size_t idx = 0; idx < sites.size(); ++idx) {
        if (idx > 0) {
            out << ',';
        }
        append_site_descriptor(sites[idx], out);
    }
    out << ']';
}

void append_native_gate(const NativeGate& gate, std::ostringstream& out) {
    out << "{\"name\":\"" << escape_json(gate.name)
        << "\",\"arity\":" << gate.arity
        << ",\"duration_ns\":" << gate.duration_ns
        << ",\"angle_min\":" << gate.angle_min
        << ",\"angle_max\":" << gate.angle_max
        << ",\"connectivity\":\"" << connectivity_to_string(gate.connectivity)
        << "\"}";
}

void append_native_gates_json(const std::vector<NativeGate>& gates, std::ostringstream& out) {
    out << '[';
    for (std::size_t idx = 0; idx < gates.size(); ++idx) {
        if (idx > 0) {
            out << ',';
        }
        append_native_gate(gates[idx], out);
    }
    out << ']';
}

void append_timing_limits_json(const TimingLimits& limits, std::ostringstream& out) {
    out << '{'
        << "\"min_wait_ns\":" << limits.min_wait_ns << ','
        << "\"max_wait_ns\":" << limits.max_wait_ns << ','
        << "\"max_parallel_single_qubit\":" << limits.max_parallel_single_qubit << ','
        << "\"max_parallel_two_qubit\":" << limits.max_parallel_two_qubit << ','
        << "\"max_parallel_per_zone\":" << limits.max_parallel_per_zone << ','
        << "\"measurement_cooldown_ns\":" << limits.measurement_cooldown_ns << ','
        << "\"measurement_duration_ns\":" << limits.measurement_duration_ns
        << '}';
}

void append_pulse_limits_json(const PulseLimits& limits, std::ostringstream& out) {
    out << '{'
        << "\"detuning_min\":" << limits.detuning_min << ','
        << "\"detuning_max\":" << limits.detuning_max << ','
        << "\"duration_min_ns\":" << limits.duration_min_ns << ','
        << "\"duration_max_ns\":" << limits.duration_max_ns << ','
        << "\"max_overlapping_pulses\":" << limits.max_overlapping_pulses
        << '}';
}

void populate_sites_from_coordinates(HardwareConfig& hw) {
    if (!hw.sites.empty() || hw.coordinates.empty()) {
        return;
    }
    hw.sites.reserve(hw.coordinates.size());
    for (std::size_t idx = 0; idx < hw.coordinates.size(); ++idx) {
        const auto& coord = hw.coordinates[idx];
        SiteDescriptor site;
        site.id = static_cast<int>(idx);
        if (!coord.empty()) {
            site.x = coord[0];
        }
        if (coord.size() > 1) {
            site.y = coord[1];
        }
        if (coord.size() > 2) {
            site.z = coord[2];
        }
        site.zone_id = 0;
        hw.sites.push_back(site);
    }
}

void ensure_site_ids(HardwareConfig& hw) {
    std::size_t expected = hw.positions.size();
    expected = std::max(expected, hw.coordinates.size());
    expected = std::max(expected, hw.site_ids.size());
    if (hw.site_ids.size() >= expected) {
        return;
    }
    const std::size_t current = hw.site_ids.size();
    hw.site_ids.resize(expected);
    for (std::size_t idx = current; idx < expected; ++idx) {
        hw.site_ids[idx] = static_cast<int>(idx);
    }
}

void ensure_positions_from_sites(HardwareConfig& hw) {
    if (hw.site_ids.empty()) {
        return;
    }
    if (hw.positions.size() >= hw.site_ids.size()) {
        return;
    }
    const auto site_index = build_site_index(hw);
    const std::size_t current = hw.positions.size();
    hw.positions.resize(hw.site_ids.size(), 0.0);
    for (std::size_t slot = current; slot < hw.site_ids.size(); ++slot) {
        const SiteDescriptor* site = site_descriptor_for_slot(hw, site_index, static_cast<int>(slot));
        if (site) {
            hw.positions[slot] = site->x;
        }
    }
}

void ensure_coordinates_from_sites(HardwareConfig& hw) {
    if (!hw.coordinates.empty() || hw.site_ids.empty()) {
        return;
    }
    const auto site_index = build_site_index(hw);
    hw.coordinates.resize(hw.site_ids.size());
    for (std::size_t slot = 0; slot < hw.site_ids.size(); ++slot) {
        const SiteDescriptor* site = site_descriptor_for_slot(hw, site_index, static_cast<int>(slot));
        if (site) {
            hw.coordinates[slot] = {site->x, site->y};
        } else if (slot < hw.positions.size()) {
            hw.coordinates[slot] = {hw.positions[slot]};
        }
    }
}

void append_instruction_json(const Instruction& instr, std::ostringstream& out) {
    out << "{\"op\":\"";
    switch (instr.op) {
        case Op::AllocArray: {
            out << "AllocArray\",\"n_qubits\":" << std::get<int>(instr.payload);
            break;
        }
        case Op::ApplyGate: {
            const auto& gate = std::get<Gate>(instr.payload);
            out << "ApplyGate\",\"gate\":{\"name\":\"" << escape_json(gate.name)
                << "\",\"targets\":";
            append_int_array(gate.targets, out);
            out << ",\"param\":" << gate.param << '}';
            break;
        }
        case Op::Measure: {
            out << "Measure\",\"targets\":";
            append_int_array(std::get<std::vector<int>>(instr.payload), out);
            break;
        }
        case Op::MoveAtom: {
            const auto& move = std::get<MoveAtomInstruction>(instr.payload);
            out << "MoveAtom\",\"atom\":" << move.atom << ",\"position\":"
                << move.position;
            break;
        }
        case Op::Wait: {
            const auto& wait_instr = std::get<WaitInstruction>(instr.payload);
            out << "Wait\",\"duration\":" << wait_instr.duration;
            break;
        }
        case Op::Pulse: {
            const auto& pulse = std::get<PulseInstruction>(instr.payload);
            out << "Pulse\",\"target\":" << pulse.target
                << ",\"detuning\":" << pulse.detuning
                << ",\"duration\":" << pulse.duration;
            break;
        }
        default:
            throw std::runtime_error("Unsupported instruction for serialization");
    }
    out << '}';
}
}

namespace service {

BackendKind backend_for_device(const std::string& device_id) {
    if (device_id == "local-arc") {
        return BackendKind::kOneApi;
    }
    if (device_id == "stabilizer") {
        return BackendKind::kStabilizer;
    }
    return BackendKind::kCpu;
}

namespace {

void enrich_hardware_with_profile_constraints(
    const JobRequest& job,
    HardwareConfig& hw
) {
    const bool is_sim_device =
        job.device_id == "local-cpu" ||
        job.device_id == "local-arc" ||
        job.device_id == "stabilizer";
    if (!is_sim_device) {
        return;
    }

    auto ensure_native_gates = [](HardwareConfig& cfg, ConnectivityKind two_qubit_connectivity) {
        if (!cfg.native_gates.empty()) {
            return;
        }
        NativeGate x;
        x.name = "X";
        x.arity = 1;
        x.duration_ns = kDefaultSingleQubitDurationNs;
        NativeGate h;
        h.name = "H";
        h.arity = 1;
        h.duration_ns = kDefaultSingleQubitDurationNs;
        NativeGate z;
        z.name = "Z";
        z.arity = 1;
        z.duration_ns = kDefaultSingleQubitDurationNs;
        NativeGate cx;
        cx.name = "CX";
        cx.arity = 2;
        cx.duration_ns = kDefaultTwoQubitDurationNs;
        cx.connectivity = two_qubit_connectivity;
        cfg.native_gates = {x, h, z, cx};
    };

    auto ensure_measurement_defaults = [](HardwareConfig& cfg) {
        if (cfg.timing_limits.measurement_duration_ns <= 0.0) {
            cfg.timing_limits.measurement_duration_ns = kDefaultMeasurementDurationNs;
        }
        if (cfg.timing_limits.measurement_cooldown_ns <= 0.0) {
            cfg.timing_limits.measurement_cooldown_ns = kDefaultMeasurementDurationNs;
        }
    };

    auto apply_defaults = [&](ConnectivityKind gate_connectivity) {
        ensure_native_gates(hw, gate_connectivity);
        ensure_measurement_defaults(hw);
    };

    if (job.profile == "benchmark_chain" ||
        job.profile == "ideal_small_array" ||
        job.profile == "lossy_chain" ||
        job.profile == "readout_stress") {
        apply_defaults(ConnectivityKind::NearestNeighborChain);
    } else if (job.profile == "lossy_block") {
        apply_defaults(ConnectivityKind::AllToAll);
    } else if (job.profile == "noisy_square_array") {
        apply_defaults(ConnectivityKind::NearestNeighborGrid);
        // Populate a simple 4x4 grid of site coordinates matching the
        // conceptual geometry of the noisy_square_array preset.
        if (hw.coordinates.empty()) {
            const std::size_t n_sites = hw.positions.size();
            hw.sites.clear();
            if (n_sites > 0) {
                const int side = static_cast<int>(std::sqrt(static_cast<double>(n_sites)) + 0.5);
                if (side > 0 && static_cast<std::size_t>(side * side) == n_sites) {
                    hw.sites.reserve(n_sites);
                    for (int idx = 0; idx < side * side; ++idx) {
                        SiteDescriptor site;
                        site.id = idx;
                        site.x = static_cast<double>(idx % side);
                        site.y = static_cast<double>(idx / side);
                        site.zone_id = 0;
                        hw.sites.push_back(site);
                    }
                }
            }
        }
    } else {
        // Fallback for legacy/custom profiles that still need durations.
        ensure_measurement_defaults(hw);
    }
}

}  // namespace

std::string to_json(const JobRequest& job) {
    std::ostringstream out;
    out << std::setprecision(15);
    out << '{';
    out << "\"job_id\":\"" << escape_json(job.job_id) << "\",";
    out << "\"device_id\":\"" << escape_json(job.device_id) << "\",";
    out << "\"profile\":\"" << escape_json(job.profile) << "\",";
    out << "\"shots\":" << job.shots << ',';
    out << "\"isa_version\":{\"major\":" << job.isa_version.major
        << ",\"minor\":" << job.isa_version.minor << "},";
    out << "\"hardware\":{\"positions\":";
    out << '[';
    for (std::size_t i = 0; i < job.hardware.positions.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << job.hardware.positions[i];
    }
    out << ']';
    if (!job.hardware.site_ids.empty()) {
        out << ",\"site_ids\":";
        append_int_array(job.hardware.site_ids, out);
    }
    if (!job.hardware.coordinates.empty()) {
        out << ",\"coordinates\":";
        append_double_matrix(job.hardware.coordinates, out);
    }
    out << ",\"blockade_radius\":" << job.hardware.blockade_radius;
    if (!job.hardware.sites.empty()) {
        out << ",\"sites\":";
        append_sites_json(job.hardware.sites, out);
    }
    if (!job.hardware.native_gates.empty()) {
        out << ",\"native_gates\":";
        append_native_gates_json(job.hardware.native_gates, out);
    }
    out << ",\"timing_limits\":";
    append_timing_limits_json(job.hardware.timing_limits, out);
    out << ",\"pulse_limits\":";
    append_pulse_limits_json(job.hardware.pulse_limits, out);
    out << "},";
    out << "\"program\":";
    out << '[';
    for (std::size_t i = 0; i < job.program.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        append_instruction_json(job.program[i], out);
    }
    out << "],";
    if (job.max_threads > 0) {
        out << "\"max_threads\":" << job.max_threads << ",";
    }
    out << "\"metadata\":{";
    bool first_entry = true;
    for (const auto& [key, value] : job.metadata) {
        if (!first_entry) {
            out << ',';
        }
        first_entry = false;
        out << "\"" << escape_json(key) << "\":\"" << escape_json(value) << "\"";
    }
    out << "}";
    if (job.stim_circuit) {
        out << ",\"stim_circuit\":\"" << escape_json(*job.stim_circuit) << "\"";
    }
    out << '}';
    return out.str();
}

std::string status_to_string(JobStatus status) {
    switch (status) {
        case JobStatus::Pending:
            return "pending";
        case JobStatus::Running:
            return "running";
        case JobStatus::Completed:
            return "completed";
        case JobStatus::Failed:
            return "failed";
    }
    return "unknown";
}

JobResult JobRunner::run(
    const JobRequest& job,
    std::size_t max_threads,
    neutral_atom_vm::ProgressReporter* reporter
) {
    auto start = std::chrono::steady_clock::now();
    JobResult result;
    result.job_id = job.job_id;
    try {
        if (!is_supported_isa_version(job.isa_version)) {
            throw std::runtime_error(
                "Unsupported ISA version " + to_string(job.isa_version) +
                " (supported: " + supported_versions_to_string() + ")");
        }
        const int shots = std::max(1, job.shots);

        // Hardware VM façade: select a concrete device profile and execute
        // the ISA program on a backend engine. For now all devices share the
        // same statevector backend, but the profile struct gives us a place
        // to hang future differences (noise, capabilities, backend kind).
        DeviceProfile profile;
        profile.id = job.device_id;
        profile.isa_version = job.isa_version;
        HardwareConfig hw = job.hardware;
        populate_sites_from_coordinates(hw);
        enrich_hardware_with_profile_constraints(job, hw);
        ensure_site_ids(hw);
        ensure_positions_from_sites(hw);
        ensure_coordinates_from_sites(hw);
        profile.hardware = std::move(hw);
        profile.backend = backend_for_device(job.device_id);
        if (job.noise_config) {
            profile.noise_config = job.noise_config;
            profile.noise_engine = std::make_shared<SimpleNoiseEngine>(*job.noise_config);
            profile.device_noise_engine =
                neutral_atom_vm::noise::build_device_noise_engine(*job.noise_config);
        }
        if (job.stim_circuit) {
            profile.stim_circuit_text = job.stim_circuit;
        }

        HardwareVM vm(profile);
        if (reporter) {
            vm.set_progress_reporter(reporter);
        }
        const std::size_t threads = max_threads > 0 ? max_threads : job.max_threads;
        const SchedulerResult scheduled = schedule_program(job.program, profile.hardware);

        std::vector<service::TimelineEntry> scheduler_timeline;
        scheduler_timeline.reserve(scheduled.timeline.size());
        double step = 0.0;
        for (const auto& event : scheduled.timeline) {
            service::TimelineEntry entry;
            entry.start_time = step;
            entry.duration = 1.0;
            entry.op = event.op;
            entry.detail = event.detail;
            scheduler_timeline.push_back(std::move(entry));
            step += 1.0;
        }
        result.scheduler_timeline = std::move(scheduler_timeline);
        result.scheduler_timeline_units = "steps";
        auto run_result = vm.run(scheduled.program, shots, {}, nullptr, threads);
        std::vector<service::TimelineEntry> timeline_entries;
        if (!run_result.backend_timeline.empty()) {
            timeline_entries.reserve(run_result.backend_timeline.size());
            for (const auto& event : run_result.backend_timeline) {
                service::TimelineEntry entry;
                entry.start_time = event.start_time;
                entry.duration = event.duration;
                entry.op = event.op;
                entry.detail = event.detail;
                timeline_entries.push_back(std::move(entry));
            }
        } else {
            timeline_entries = scheduled.timeline;
        }
        convert_timeline_to_microseconds(timeline_entries);
        result.timeline = timeline_entries;
        result.timeline_units = kDisplayTimeUnit;
        result.logs = build_timeline_logs(result.timeline);
        convert_logs_to_microseconds(run_result.logs);
        result.log_time_units = kDisplayTimeUnit;
        result.logs.insert(result.logs.end(), run_result.logs.begin(), run_result.logs.end());
        result.measurements = std::move(run_result.measurements);
        result.status = JobStatus::Completed;
    } catch (const std::exception& ex) {
        result.status = JobStatus::Failed;
        result.message = ex.what();
    }
    auto end = std::chrono::steady_clock::now();
    result.elapsed_time = std::chrono::duration<double>(end - start).count();
    return result;
}

}  // namespace service
