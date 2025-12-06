#include "service/job.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

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

std::string to_json(const JobRequest& job) {
    std::ostringstream out;
    out << std::setprecision(15);
    out << '{';
    out << "\"job_id\":\"" << escape_json(job.job_id) << "\",";
    out << "\"shots\":" << job.shots << ',';
    out << "\"hardware\":{\"positions\":";
    out << '[';
    for (std::size_t i = 0; i < job.hardware.positions.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << job.hardware.positions[i];
    }
    out << "],\"blockade_radius\":" << job.hardware.blockade_radius << "},";
    out << "\"program\":";
    out << '[';
    for (std::size_t i = 0; i < job.program.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        append_instruction_json(job.program[i], out);
    }
    out << "],";
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

JobResult JobRunner::run(const JobRequest& job) {
    auto start = std::chrono::steady_clock::now();
    JobResult result;
    result.job_id = job.job_id;
    try {
        const int shots = std::max(1, job.shots);
        for (int shot = 0; shot < shots; ++shot) {
            VM vm(job.hardware);
            vm.run(job.program);
            const auto& measurements = vm.state().measurements;
            result.measurements.insert(
                result.measurements.end(), measurements.begin(), measurements.end());
        }
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
