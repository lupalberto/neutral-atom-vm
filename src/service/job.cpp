#include "service/job.hpp"
#include "hardware_vm.hpp"
#include "noise/device_noise_builder.hpp"

#include <cmath>
#include <chrono>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
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
        site.zone_id = 0;
        hw.sites.push_back(site);
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
    return BackendKind::kCpu;
}

namespace {

void enrich_hardware_with_profile_constraints(
    const JobRequest& job,
    HardwareConfig& hw
) {
    const bool is_sim_device =
        job.device_id == "quera.na_vm.sim" ||
        job.device_id == "local-cpu" ||
        job.device_id == "local-arc";
    if (!is_sim_device) {
        return;
    }

    if (job.profile == "benchmark_chain") {
        NativeGate cx;
        cx.name = "CX";
        cx.arity = 2;
        cx.duration_ns = 200.0;
        cx.angle_min = 0.0;
        cx.angle_max = 0.0;
        cx.connectivity = ConnectivityKind::NearestNeighborChain;

        NativeGate x;
        x.name = "X";
        x.arity = 1;
        x.duration_ns = 50.0;

        NativeGate h;
        h.name = "H";
        h.arity = 1;
        h.duration_ns = 50.0;

        NativeGate z;
        z.name = "Z";
        z.arity = 1;
        z.duration_ns = 50.0;

        hw.native_gates.clear();
        hw.native_gates.push_back(x);
        hw.native_gates.push_back(h);
        hw.native_gates.push_back(z);
        hw.native_gates.push_back(cx);

        hw.pulse_limits.detuning_min = -10.0;
        hw.pulse_limits.detuning_max = 10.0;
        hw.pulse_limits.duration_min_ns = 0.0;
        hw.pulse_limits.duration_max_ns = 1e6;
        hw.pulse_limits.max_overlapping_pulses = 0;
        hw.timing_limits.measurement_cooldown_ns = 5.0;
    } else if (job.profile == "noisy_square_array") {
        NativeGate cx;
        cx.name = "CX";
        cx.arity = 2;
        cx.duration_ns = 200.0;
        cx.angle_min = 0.0;
        cx.angle_max = 0.0;
        cx.connectivity = ConnectivityKind::NearestNeighborGrid;

        NativeGate x;
        x.name = "X";
        x.arity = 1;
        x.duration_ns = 50.0;

        NativeGate h;
        h.name = "H";
        h.arity = 1;
        h.duration_ns = 50.0;

        NativeGate z;
        z.name = "Z";
        z.arity = 1;
        z.duration_ns = 50.0;

        hw.native_gates.clear();
        hw.native_gates.push_back(x);
        hw.native_gates.push_back(h);
        hw.native_gates.push_back(z);
        hw.native_gates.push_back(cx);

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
    if (!job.hardware.coordinates.empty()) {
        out << ",\"coordinates\":";
        append_double_matrix(job.hardware.coordinates, out);
    }
    out << ",\"blockade_radius\":" << job.hardware.blockade_radius << "},";
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
    out << "}}";
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
        profile.hardware = std::move(hw);
        profile.backend = backend_for_device(job.device_id);
        if (job.noise_config) {
            profile.noise_engine = std::make_shared<SimpleNoiseEngine>(*job.noise_config);
            profile.device_noise_engine =
                neutral_atom_vm::noise::build_device_noise_engine(*job.noise_config);
        }

        HardwareVM vm(profile);
        if (reporter) {
            vm.set_progress_reporter(reporter);
        }
        const std::size_t threads = max_threads > 0 ? max_threads : job.max_threads;
        const auto run_result = vm.run(job.program, shots, {}, threads);
        result.measurements = run_result.measurements;
        result.logs = run_result.logs;
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
