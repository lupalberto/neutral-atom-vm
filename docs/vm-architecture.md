# Neutral Atom VM Architecture

This document clarifies what we mean by a multi‑level *hardware virtual machine* for neutral‑atom devices, and how the current prototype fits into that picture.

The goal is to distinguish clearly between:

- The **hardware VM**: a stable abstraction of neutral‑atom hardware (ISA + device model + service API).
- The **execution engines**: simulators/physics engines that implement the VM semantics.
- The **compilation stack**: high‑level DSLs and passes that target the VM.

---

## 1. Layers at a Glance

From top to bottom:

1. **DSLs and Compilation Stack**
   - Languages: Bloqade, Squin, Kirin IR.
   - Passes: synthesis, scheduling, neutral‑atom specific rewrites, noise insertion (`pauli_channel`, loss).
   - Output: programs in a **VM dialect** (hardware‑oriented instructions + metadata).

2. **Hardware Virtual Machine (VM)**
   - Defines a **virtual instruction set** for neutral‑atom devices:
     - `AllocArray`, `ApplyGate`, `MoveAtom`, `Wait`, `Pulse`, `Measure`, etc.
     - Timing/ordering semantics, geometry references, and resource usage are part of the contract.
   - Exposes a **service / job API**:
     - Submit jobs (`JobRequest`), track status, stream measurements and logs (`JobResult`).
     - Select device profiles (geometry, capabilities, noise models).
   - Presents a **device‑like interface**:
     - Multiple engines and hardware backends sit behind a single VM abstraction.

3. **Execution Engines (Backends)**
   - Implement the VM semantics for different purposes:
     - Ideal statevector engine (what we have today).
     - Noisy Pauli engine (current `SimpleNoiseEngine` + VM core).
     - Full neutral‑atom physics engine (Rydberg Hamiltonian + pulse‑level dynamics, future work).
     - Real hardware driver(s) in the long term.
   - Each engine is responsible for:
     - Evolving the quantum state and hardware state (positions, time, pulses).
     - Applying noise according to a device configuration.
     - Producing measurements, logs, and diagnostics compatible with the VM API.

---

## 2. Current Prototype: What We Have

### 2.1 ISA and Statevector Runtime

In the current repository:

- The C++ `StatevectorEngine` class (`src/engine_statevector.hpp`, `src/engine_statevector.cpp`) implements:
  - A small instruction set:
    - `AllocArray` – allocate qubits/sites.
    - `ApplyGate` – apply a logical gate (`X`, `H`, `Z`, `CX`, `CZ`).
    - `Measure` – projectively measure a subset of qubits.
    - `MoveAtom` – update atomic positions (geometry).
    - `Wait` – advance logical time.
    - `Pulse` – log pulses applied to targets.
  - A `StatevectorState` with:
    - Statevector amplitudes.
    - Hardware configuration (`HardwareConfig`).
    - Logical time, pulse log, measurement records.

At this stage, timing and pulse semantics are deliberately minimal: the VM uses them mostly as bookkeeping, not as full continuous‑time dynamics.

### 2.2 Service/Job API

- `src/service/job.hpp`, `src/service/job.cpp` define:
  - `JobRequest`: job identifier, hardware config, program (vector of `Instruction`), `shots`, and metadata.
  - `JobResult`: status, elapsed time, measurements, message.
  - `JobRunner`: runs a job by constructing a fresh `StatevectorEngine` per shot and executing the program.
  - A JSON serialization of `JobRequest` used by tests.

- `src/bindings/python/module.cpp` exposes a low-level Python binding used by the
  higher-level SDK:
  - `neutral_atom_vm._neutral_atom_vm.submit_job(job: dict)`:
    - Accepts a dict mirroring `service::JobRequest` (job_id, device_id, profile,
      hardware config, program, shots, metadata).
    - Converts the instruction dictionaries into `Instruction` objects and uses
      `JobRunner` to execute the job.
  - The public Python API wraps this in `neutral_atom_vm.JobRequest` and
    `neutral_atom_vm.submit_job(job: JobRequest | Mapping[str, Any])`, so callers
    interact with a composable job description rather than passing geometry
    directly as top-level arguments.

From the client’s perspective, this is already a **device‑like** API: you submit a job and get measurements back, even though everything is currently in‑process.

### 2.3 Noise and Device Configuration

- `src/noise.hpp`, `src/noise.cpp` define:
  - `NoiseEngine` interface:
    - Measurement noise (`apply_measurement_noise`).
    - Optional per‑gate noise hooks (`apply_single_qubit_gate_noise`, `apply_two_qubit_gate_noise`).
  - `SimpleNoiseConfig` and `SimpleNoiseEngine`:
    - Measurement‑time combined noise: loss, quantum bit‑flip, and readout flips.
    - Per‑gate Pauli channels applied to the statevector after gates.

- `VM` holds a `std::shared_ptr<const NoiseEngine>` and an RNG, and calls the engine at well‑defined hook points.

This gives us a **first device model**: a configuration that determines how noisy the simulated hardware is using effective Pauli/loss channels.

### 2.4 DSL Integration

- `python/src/neutral_atom_vm/squin_lowering.py`:
  - `to_vm_program(kernel: Method) -> list[dict]` lowers a Squin kernel (Kirin IR) to the VM instruction schema.
  - Tests validate this for simple kernels (`python/tests/test_client.py`).

At the moment, only the gate‑level lowering from Squin → VM schema is implemented; richer pulse/schedule lowering and noise annotations from Bloqade are future work.

---

## 3. What “Hardware Virtual Machine” Adds Beyond the Simulator

The job description emphasizes a **multi‑level hardware virtual machine**. Relative to a standalone simulator, this implies:

1. **Stable VM ISA and semantics**
   - Clearly specified, versioned instruction set:
     - Meaning of `MoveAtom`, `Wait`, `Pulse`, blockade checks, etc.
     - Rules for timing, parallelism, and conflict (e.g., overlapping pulses).
   - Backward‑compatible evolution of the ISA as hardware features are added.

2. **Device profiles and capabilities**
   - Named device types/configurations:
     - Geometry (1D/2D arrays, zones).
     - Supported gate sets and pulse shapes.
     - Noise profiles and resource limits.
   - A way for clients to select profiles per job and for the VM to validate programs against them.

3. **Service / driver behavior**
   - Job queues, cancellation, status transitions (pending, running, completed, failed).
   - Streaming outputs: measurement batches, pulse logs, diagnostics.
   - Well‑defined error reporting (invalid programs, resource exhaustion, backend failures).

4. **Multiple execution engines behind the VM**
   - Ideal logical engine (current implementation).
   - Noisy Pauli engine (current `SimpleNoiseEngine` + VM).
   - Full neutral‑atom physics engine for pulse‑level programs.
   - Eventually, connectors to real hardware.

From the perspective of the compilation stack and client SDKs, these engines are interchangeable hardware instances, selected by configuration and capability flags.

---

## 4. Planned Evolution

The current codebase already provides:

- A minimal VM instruction set and state model.
- A job/service layer around the VM.
- A first noise engine prototype.
- A Squin → VM lowering pass.

To fully align with the “hardware VM” vision, the next steps are:

1. **Formalize the VM ISA**
   - Document the instruction set, timing, and geometry semantics.
   - Introduce versioning for the VM dialect consumed by the job API.

2. **Device and backend abstractions**
   - Define device profiles and capability descriptors.
   - Add a backend registry so the job runner can dispatch to different execution engines based on device selection.

3. **Pulse‑level VM dialect**
   - Extend the instruction set (or add a companion dialect) to model pulse schedules, shaped pulses, and zones explicitly.
   - Integrate with Bloqade/Squin passes that lower pulse‑level DSLs into this dialect.

4. **Noise model integration**
   - Connect `SimpleNoiseConfig` (and successors) to Bloqade’s Pauli and loss channels so device configurations can be derived from calibration/noise IR.
   - Add idle/time‑dependent noise (dephasing during `Wait`, pulse‑dependent errors, etc.).

5. **Service‑level robustness**
   - Enrich `JobRequest`/`JobResult` with device IDs, profile selection, richer status codes, and structured error messages.
   - Provide streaming interfaces and batching where appropriate.

These changes will turn the current prototype from “a simulator with a small API” into a proper **hardware virtual machine**: a stable target for the QuEra DSL and compilation stack, with multiple underlying engines and realistic device models.

## 5. ISA v1 Specification

The prototype ISA is captured in `src/vm/isa.hpp`, which must remain the definitive description for compilers, services, and backends. Key elements of ISA v1 are:

- **Instruction palette:**
  - `AllocArray` allocates `n` qubits/sites and zero-initializes the device state.
  - `ApplyGate` executes a logical gate (`Gate.name`) on the supplied `Gate.targets` and optional `Gate.param`.
  - `Measure` projects the specified targets and records outcomes in the measurement log.
  - `MoveAtom` updates an atom’s position for geometry-aware validation.
  - `Wait` advances logical time so engines can insert idle noise or monitor durations.
  - `Pulse` logs a classical pulse description (`target`, `detuning`, `duration`) without fixing the underlying physics.

- **Operand encoding:**
  - Each `Instruction` wraps an `Op` plus a `std::variant` payload that matches the opcode.
  - `Gate` carries easy-to-read metadata (`name`, `targets`, `param`) so a compiler can express both single- and two-qubit gates without engine-specific enums.

- **Hardware configuration:**
  - `HardwareConfig` exposes the atom `positions` (1D array for this version) and a `blockade_radius`.
  - Engines use the hardware configuration to enforce constraints before applying two-qubit operations.

Keeping this section synchronized with `src/vm/isa.hpp` ensures ISA v1 remains a clean, engine-agnostic contract—the exact behavior the new hardware ISA ticket requires.

## 6. ISA Versioning

`src/vm/isa.hpp` also defines `ISAVersion`, `kCurrentISAVersion`, and `kSupportedISAVersions`. Every `JobRequest` carries an `isa_version` with sensible defaults (`1.0` in this release), and the service serializes that field alongside programs and hardware configs.

This explicit version tag is how the VM can offer multiple ISA versions within the same release: downstream clients declare the dialect they are targeting, the service/engine inspects `isa_version`, and the runtime routes to the appropriate interpreter/emulator. Future ISA revisions can live beside the current one while we ship support for old programs, simply by adding new `ISAVersion` values and dispatch logic in `JobRunner`.

Currently the runtime enumerates the supported versions via `kSupportedISAVersions` (currently `[1.0]`), and `JobRunner` refuses to execute programs targeting anything else, returning a clear error that lists the acceptable versions.

Currently the `JobRunner` enforces this contract by rejecting jobs whose `isa_version` differs from `kCurrentISAVersion`, returning a failed status and explaining that the ISA is unsupported.
