#include <iostream>

#include "noise.hpp"
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

void fill_measurement_noise_config(
    const py::dict& src,
    MeasurementNoiseConfig& dst
) {
    if (src.contains("p_flip0_to_1")) {
        dst.p_flip0_to_1 = py::cast<double>(src["p_flip0_to_1"]);
    }
    if (src.contains("p_flip1_to_0")) {
        dst.p_flip1_to_0 = py::cast<double>(src["p_flip1_to_0"]);
    }
}

void fill_pauli_config(
    const py::dict& src,
    SingleQubitPauliConfig& dst
) {
    if (src.contains("px")) {
        dst.px = py::cast<double>(src["px"]);
    }
    if (src.contains("py")) {
        dst.py = py::cast<double>(src["py"]);
    }
    if (src.contains("pz")) {
        dst.pz = py::cast<double>(src["pz"]);
    }
}

void fill_gate_noise_config(
    const py::dict& src,
    GateNoiseConfig& dst
) {
    if (src.contains("single_qubit")) {
        const auto single = py::cast<py::dict>(src["single_qubit"]);
        fill_pauli_config(single, dst.single_qubit);
    }
    if (src.contains("two_qubit_control")) {
        const auto ctrl = py::cast<py::dict>(src["two_qubit_control"]);
        fill_pauli_config(ctrl, dst.two_qubit_control);
    }
    if (src.contains("two_qubit_target")) {
        const auto tgt = py::cast<py::dict>(src["two_qubit_target"]);
        fill_pauli_config(tgt, dst.two_qubit_target);
    }
}

py::dict submit_job(const py::dict& job_obj) {
    service::JobRequest job;

    // High-level identifiers.
    if (job_obj.contains("job_id")) {
        job.job_id = py::cast<std::string>(job_obj["job_id"]);
    } else {
        job.job_id = "python-client";
    }

    if (job_obj.contains("device_id")) {
        job.device_id = py::cast<std::string>(job_obj["device_id"]);
    } else {
        job.device_id = "runtime";
    }

    if (job_obj.contains("profile") && !job_obj["profile"].is_none()) {
        job.profile = py::cast<std::string>(job_obj["profile"]);
    }

    // Program.
    const auto program = py::cast<py::list>(job_obj["program"]);
    job.program = instructions_from_list(program);

    // Hardware configuration.
    if (job_obj.contains("hardware")) {
        const auto hardware = py::cast<py::dict>(job_obj["hardware"]);
        if (hardware.contains("positions")) {
            job.hardware.positions = py::cast<std::vector<double>>(hardware["positions"]);
        }
        if (hardware.contains("blockade_radius")) {
            job.hardware.blockade_radius = py::cast<double>(hardware["blockade_radius"]);
        }
    } else {
        // Fallback to legacy top-level fields if present.
        if (job_obj.contains("positions")) {
            job.hardware.positions = py::cast<std::vector<double>>(job_obj["positions"]);
        }
        if (job_obj.contains("blockade_radius")) {
            job.hardware.blockade_radius = py::cast<double>(job_obj["blockade_radius"]);
        }
    }

    // Shots.
    if (job_obj.contains("shots")) {
        job.shots = py::cast<int>(job_obj["shots"]);
    }

    // Optional metadata: accept a mapping of str->str.
    if (job_obj.contains("metadata")) {
        job.metadata = py::cast<std::map<std::string, std::string>>(job_obj["metadata"]);
    }

    if (job_obj.contains("noise")) {
        const auto noise = py::cast<py::dict>(job_obj["noise"]);
        SimpleNoiseConfig cfg;
        if (noise.contains("p_quantum_flip")) {
            cfg.p_quantum_flip = py::cast<double>(noise["p_quantum_flip"]);
        }
        if (noise.contains("p_loss")) {
            cfg.p_loss = py::cast<double>(noise["p_loss"]);
        }
        if (noise.contains("readout")) {
            fill_measurement_noise_config(py::cast<py::dict>(noise["readout"]), cfg.readout);
        }
        if (noise.contains("gate")) {
            fill_gate_noise_config(py::cast<py::dict>(noise["gate"]), cfg.gate);
        }
        if (noise.contains("idle_rate")) {
            cfg.idle_rate = py::cast<double>(noise["idle_rate"]);
        }
        job.noise_config = cfg;
    }

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
        py::arg("job"),
        "Submit a VM job using the builtin JobRunner. The job dict mirrors service::JobRequest."
    );
}
