#include "service/job.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace {

Instruction instruction_from_dict(const py::dict& obj) {
    const std::string op = py::cast<std::string>(obj["op"]);
    Instruction instr;
    if (op == "AllocArray") {
        instr.op = Op::AllocArray;
        instr.payload = py::cast<int>(obj["n_qubits"]);
    } else if (op == "ApplyGate") {
        instr.op = Op::ApplyGate;
        Gate gate;
        gate.name = py::cast<std::string>(obj["name"]);
        gate.targets = py::cast<std::vector<int>>(obj["targets"]);
        if (obj.contains("param")) {
            gate.param = py::cast<double>(obj["param"]);
        }
        instr.payload = gate;
    } else if (op == "Measure") {
        instr.op = Op::Measure;
        instr.payload = py::cast<std::vector<int>>(obj["targets"]);
    } else if (op == "MoveAtom") {
        instr.op = Op::MoveAtom;
        MoveAtomInstruction move;
        move.atom = py::cast<int>(obj["atom"]);
        move.position = py::cast<double>(obj["position"]);
        instr.payload = move;
    } else if (op == "Wait") {
        instr.op = Op::Wait;
        WaitInstruction wait;
        wait.duration = py::cast<double>(obj["duration"]);
        instr.payload = wait;
    } else if (op == "Pulse") {
        instr.op = Op::Pulse;
        PulseInstruction pulse;
        pulse.target = py::cast<int>(obj["target"]);
        pulse.detuning = py::cast<double>(obj["detuning"]);
        pulse.duration = py::cast<double>(obj["duration"]);
        instr.payload = pulse;
    } else {
        throw std::runtime_error("Unsupported op: " + op);
    }
    return instr;
}

std::vector<Instruction> instructions_from_list(const py::list& program) {
    std::vector<Instruction> out;
    out.reserve(py::len(program));
    for (const auto& item : program) {
        out.push_back(instruction_from_dict(py::cast<py::dict>(item)));
    }
    return out;
}

py::dict job_result_to_dict(const service::JobResult& result) {
    py::dict out;
    out["job_id"] = result.job_id;
    out["status"] = service::status_to_string(result.status);
    out["elapsed_time"] = result.elapsed_time;
    py::list measurements;
    for (const auto& record : result.measurements) {
        py::dict rec;
        rec["targets"] = record.targets;
        rec["bits"] = record.bits;
        measurements.append(rec);
    }
    out["measurements"] = measurements;
    out["message"] = result.message;
    return out;
}

py::dict submit_job(
    const py::list& program,
    const std::vector<double>& positions,
    double blockade_radius,
    int shots
) {
    service::JobRequest job;
    job.job_id = "python-client";
    job.hardware.positions = positions;
    job.hardware.blockade_radius = blockade_radius;
    job.program = instructions_from_list(program);
    job.shots = shots;
    service::JobRunner runner;
    auto result = runner.run(job);
    return job_result_to_dict(result);
}

}  // namespace

PYBIND11_MODULE(_neutral_atom_vm, m) {
    m.doc() = "Neutral Atom VM client bindings";
    m.def(
        "submit_job",
        &submit_job,
        py::arg("program"),
        py::arg("positions"),
        py::arg("blockade_radius") = 0.0,
        py::arg("shots") = 1,
        "Submit a VM job using the builtin JobRunner"
    );
}
