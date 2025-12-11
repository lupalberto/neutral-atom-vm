#include <iostream>

#include "noise.hpp"
#include "service/job.hpp"
#include "service/job_service.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <stdexcept>
#include <utility>

namespace py = pybind11;

ConnectivityKind parse_connectivity(py::handle value) {
    if (!value || value.is_none()) {
        return ConnectivityKind::AllToAll;
    }
    const std::string text = py::cast<std::string>(value);
    if (text == "AllToAll") {
        return ConnectivityKind::AllToAll;
    }
    if (text == "NearestNeighborChain") {
        return ConnectivityKind::NearestNeighborChain;
    }
    if (text == "NearestNeighborGrid") {
        return ConnectivityKind::NearestNeighborGrid;
    }
    throw std::invalid_argument("Unknown connectivity: " + text);
}

void fill_site_descriptor(const py::dict& src, SiteDescriptor& dst) {
    if (src.contains("id")) {
        dst.id = py::cast<int>(src["id"]);
    }
    if (src.contains("x")) {
        dst.x = py::cast<double>(src["x"]);
    }
    if (src.contains("y")) {
        dst.y = py::cast<double>(src["y"]);
    }
    if (src.contains("zone_id")) {
        dst.zone_id = py::cast<int>(src["zone_id"]);
    }
}

void fill_native_gate(const py::dict& src, NativeGate& dst) {
    if (src.contains("name")) {
        dst.name = py::cast<std::string>(src["name"]);
    }
    if (src.contains("arity")) {
        dst.arity = py::cast<int>(src["arity"]);
    }
    if (src.contains("duration_ns")) {
        dst.duration_ns = py::cast<double>(src["duration_ns"]);
    }
    if (src.contains("angle_min")) {
        dst.angle_min = py::cast<double>(src["angle_min"]);
    }
    if (src.contains("angle_max")) {
        dst.angle_max = py::cast<double>(src["angle_max"]);
    }
    if (src.contains("connectivity")) {
        dst.connectivity = parse_connectivity(src["connectivity"]);
    }
}

void fill_timing_limits(const py::dict& src, TimingLimits& dst) {
    if (src.contains("min_wait_ns")) {
        dst.min_wait_ns = py::cast<double>(src["min_wait_ns"]);
    }
    if (src.contains("max_wait_ns")) {
        dst.max_wait_ns = py::cast<double>(src["max_wait_ns"]);
    }
    if (src.contains("max_parallel_single_qubit")) {
        dst.max_parallel_single_qubit = py::cast<int>(src["max_parallel_single_qubit"]);
    }
    if (src.contains("max_parallel_two_qubit")) {
        dst.max_parallel_two_qubit = py::cast<int>(src["max_parallel_two_qubit"]);
    }
    if (src.contains("max_parallel_per_zone")) {
        dst.max_parallel_per_zone = py::cast<int>(src["max_parallel_per_zone"]);
    }
    if (src.contains("measurement_cooldown_ns")) {
        dst.measurement_cooldown_ns = py::cast<double>(src["measurement_cooldown_ns"]);
    }
    if (src.contains("measurement_duration_ns")) {
        dst.measurement_duration_ns = py::cast<double>(src["measurement_duration_ns"]);
    }
}

void fill_pulse_limits(const py::dict& src, PulseLimits& dst) {
    if (src.contains("detuning_min")) {
        dst.detuning_min = py::cast<double>(src["detuning_min"]);
    }
    if (src.contains("detuning_max")) {
        dst.detuning_max = py::cast<double>(src["detuning_max"]);
    }
    if (src.contains("duration_min_ns")) {
        dst.duration_min_ns = py::cast<double>(src["duration_min_ns"]);
    }
    if (src.contains("duration_max_ns")) {
        dst.duration_max_ns = py::cast<double>(src["duration_max_ns"]);
    }
    if (src.contains("max_overlapping_pulses")) {
        dst.max_overlapping_pulses = py::cast<int>(src["max_overlapping_pulses"]);
    }
}
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
    if (!result.log_time_units.empty()) {
        out["log_time_units"] = result.log_time_units;
    }
    py::list log_list;
    for (const auto& entry : result.logs) {
        py::dict log;
        log["shot"] = entry.shot;
        log["time"] = entry.logical_time;
        log["category"] = entry.category;
        log["message"] = entry.message;
        log_list.append(log);
    }
    out["logs"] = log_list;
    if (!result.timeline_units.empty()) {
        out["timeline_units"] = result.timeline_units;
    }
    py::list timeline_list;
    for (const auto& entry : result.timeline) {
        py::dict item;
        item["start_time"] = entry.start_time;
        item["duration"] = entry.duration;
        item["op"] = entry.op;
        item["detail"] = entry.detail;
        timeline_list.append(item);
    }
    out["timeline"] = timeline_list;
    if (!result.scheduler_timeline_units.empty()) {
        out["scheduler_timeline_units"] = result.scheduler_timeline_units;
    }
    py::list scheduler_timeline_list;
    for (const auto& entry : result.scheduler_timeline) {
        py::dict item;
        item["start_time"] = entry.start_time;
        item["duration"] = entry.duration;
        item["op"] = entry.op;
        item["detail"] = entry.detail;
        scheduler_timeline_list.append(item);
    }
    out["scheduler_timeline"] = scheduler_timeline_list;
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

void fill_correlated_gate_config(
    const py::dict& src,
    TwoQubitCorrelatedPauliConfig& dst
) {
    if (!src.contains("matrix")) {
        return;
    }
    const auto matrix = py::cast<py::list>(src["matrix"]);
    std::size_t idx = 0;
    for (const auto& row_obj : matrix) {
        if (idx >= 4) {
            break;
        }
        const auto row = py::cast<py::list>(row_obj);
        for (std::size_t j = 0; j < 4 && j < row.size(); ++j) {
            dst.matrix[4 * idx + j] = py::cast<double>(row[j]);
        }
        ++idx;
    }
}

void fill_loss_runtime_config(
    const py::dict& src,
    LossRuntimeConfig& dst
) {
    if (src.contains("per_gate")) {
        dst.per_gate = py::cast<double>(src["per_gate"]);
    }
    if (src.contains("idle_rate")) {
        dst.idle_rate = py::cast<double>(src["idle_rate"]);
    }
}

void fill_phase_noise_config(
    const py::dict& src,
    PhaseNoiseConfig& dst
) {
    if (src.contains("single_qubit")) {
        dst.single_qubit = py::cast<double>(src["single_qubit"]);
    }
    if (src.contains("two_qubit_control")) {
        dst.two_qubit_control = py::cast<double>(src["two_qubit_control"]);
    }
    if (src.contains("two_qubit_target")) {
        dst.two_qubit_target = py::cast<double>(src["two_qubit_target"]);
    }
    if (src.contains("idle")) {
        dst.idle = py::cast<double>(src["idle"]);
    }
}

void fill_amplitude_damping_config(
    const py::dict& src,
    AmplitudeDampingConfig& dst
) {
    if (src.contains("per_gate")) {
        dst.per_gate = py::cast<double>(src["per_gate"]);
    }
    if (src.contains("idle_rate")) {
        dst.idle_rate = py::cast<double>(src["idle_rate"]);
    }
}

service::JobRequest build_job_request(const py::dict& job_obj) {
    service::JobRequest job;

    if (job_obj.contains("job_id")) {
        job.job_id = py::cast<std::string>(job_obj["job_id"]);
    } else {
        job.job_id = "python-client";
    }

    if (job_obj.contains("device_id")) {
        job.device_id = py::cast<std::string>(job_obj["device_id"]);
    } else {
        job.device_id = "local-cpu";
    }

    if (job_obj.contains("profile") && !job_obj["profile"].is_none()) {
        job.profile = py::cast<std::string>(job_obj["profile"]);
    }

    const auto program = py::cast<py::list>(job_obj["program"]);
    job.program = instructions_from_list(program);

    if (job_obj.contains("hardware")) {
        const auto hardware = py::cast<py::dict>(job_obj["hardware"]);
        if (hardware.contains("positions")) {
            job.hardware.positions = py::cast<std::vector<double>>(hardware["positions"]);
        }
        if (hardware.contains("coordinates")) {
            job.hardware.coordinates =
                py::cast<std::vector<std::vector<double>>>(hardware["coordinates"]);
        }
        if (hardware.contains("blockade_radius")) {
            job.hardware.blockade_radius = py::cast<double>(hardware["blockade_radius"]);
        }
        if (hardware.contains("sites")) {
            const auto site_list = py::cast<py::list>(hardware["sites"]);
            job.hardware.sites.clear();
            job.hardware.sites.reserve(site_list.size());
            for (const auto& site_obj : site_list) {
                SiteDescriptor descriptor;
                fill_site_descriptor(py::cast<py::dict>(site_obj), descriptor);
                job.hardware.sites.push_back(descriptor);
            }
        }
        if (hardware.contains("site_ids")) {
            job.hardware.site_ids = py::cast<std::vector<int>>(hardware["site_ids"]);
        }
        if (hardware.contains("native_gates")) {
            const auto gate_list = py::cast<py::list>(hardware["native_gates"]);
            job.hardware.native_gates.clear();
            job.hardware.native_gates.reserve(gate_list.size());
            for (const auto& gate_obj : gate_list) {
                NativeGate gate;
                fill_native_gate(py::cast<py::dict>(gate_obj), gate);
                job.hardware.native_gates.push_back(std::move(gate));
            }
        }
        if (hardware.contains("timing_limits")) {
            fill_timing_limits(py::cast<py::dict>(hardware["timing_limits"]), job.hardware.timing_limits);
        }
        if (hardware.contains("pulse_limits")) {
            fill_pulse_limits(py::cast<py::dict>(hardware["pulse_limits"]), job.hardware.pulse_limits);
        }
    } else {
        if (job_obj.contains("positions")) {
            job.hardware.positions = py::cast<std::vector<double>>(job_obj["positions"]);
        }
        if (job_obj.contains("blockade_radius")) {
            job.hardware.blockade_radius = py::cast<double>(job_obj["blockade_radius"]);
        }
    }

    if (job_obj.contains("shots")) {
        job.shots = py::cast<int>(job_obj["shots"]);
    }

    if (job_obj.contains("max_threads")) {
        job.max_threads = py::cast<std::size_t>(job_obj["max_threads"]);
    }

    if (job_obj.contains("metadata")) {
        job.metadata = py::cast<std::map<std::string, std::string>>(job_obj["metadata"]);
    }

    if (job_obj.contains("stim_circuit") && !job_obj["stim_circuit"].is_none()) {
        job.stim_circuit = py::cast<std::string>(job_obj["stim_circuit"]);
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
        if (noise.contains("correlated_gate")) {
            fill_correlated_gate_config(
                py::cast<py::dict>(noise["correlated_gate"]),
                cfg.correlated_gate
            );
        }
        if (noise.contains("idle_rate")) {
            cfg.idle_rate = py::cast<double>(noise["idle_rate"]);
        }
        if (noise.contains("phase")) {
            fill_phase_noise_config(py::cast<py::dict>(noise["phase"]), cfg.phase);
        }
        if (noise.contains("amplitude_damping")) {
            fill_amplitude_damping_config(
                py::cast<py::dict>(noise["amplitude_damping"]),
                cfg.amplitude_damping
            );
        }
        if (noise.contains("loss_runtime")) {
            fill_loss_runtime_config(
                py::cast<py::dict>(noise["loss_runtime"]),
                cfg.loss_runtime
            );
        }
        job.noise_config = cfg;
    }

    return job;
}

service::JobService job_service;

py::dict execution_log_to_dict(const ExecutionLog& entry) {
    py::dict log;
    log["shot"] = entry.shot;
    log["time"] = entry.logical_time;
    log["category"] = entry.category;
    log["message"] = entry.message;
    return log;
}

py::dict submit_job(const py::dict& job_obj) {
    service::JobRequest job = build_job_request(job_obj);
    service::JobRunner runner;
    auto result = runner.run(job);
    return job_result_to_dict(result);
}

py::dict submit_job_async(const py::dict& job_obj) {
    service::JobRequest job = build_job_request(job_obj);
    const std::string job_id = job_service.submit(job, job.max_threads);
    py::dict out;
    out["job_id"] = job_id;
    return out;
}

py::dict job_status(const std::string& job_id) {
    const service::JobStatusSnapshot snapshot = job_service.status(job_id);
    py::dict out;
    out["job_id"] = job_id;
    out["status"] = service::status_to_string(snapshot.status);
    out["percent_complete"] = snapshot.percent_complete;
    out["message"] = snapshot.message;
    py::list logs;
    for (const auto& entry : snapshot.recent_logs) {
        logs.append(execution_log_to_dict(entry));
    }
    out["recent_logs"] = logs;
    return out;
}

py::dict job_result(const std::string& job_id) {
    const auto result = job_service.poll_result(job_id);
    if (!result) {
        throw std::runtime_error("job result not available yet");
    }
    return job_result_to_dict(*result);
}

bool has_oneapi_backend() {
#ifdef NA_VM_WITH_ONEAPI
    return true;
#else
    return false;
#endif
}

bool has_stabilizer_backend() {
#ifdef NA_VM_WITH_STIM
    return true;
#else
    return false;
#endif
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
    m.def(
        "submit_job_async",
        &submit_job_async,
        py::arg("job"),
        "Submit a VM job asynchronously and receive a job_id immediately."
    );
    m.def(
        "job_status",
        &job_status,
        py::arg("job_id"),
        "Query the current status snapshot for an async job."
    );
    m.def(
        "job_result",
        &job_result,
        py::arg("job_id"),
        "Fetch the final result for an async job (raises if not ready)."
    );
    m.def(
        "has_oneapi_backend",
        &has_oneapi_backend,
        "Return true when the bindings were built with the oneAPI backend."
    );
    m.def(
        "has_stabilizer_backend",
        &has_stabilizer_backend,
        "Return true when the stabilizer (Stim) backend is available."
    );
}
