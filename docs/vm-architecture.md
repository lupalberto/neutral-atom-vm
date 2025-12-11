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

Notably, the ISA is **noise-free**: it models idealized hardware operations. Noise is introduced either via device/profile configuration and `NoiseEngine` hooks, or via higher-level simulation IR (e.g. squin + Pauli/loss noise dialect lowered to Stim). This keeps the ISA suitable for both simulators and real devices while allowing simulators to support richer fault-injection workflows above the ISA.

At this stage, timing and pulse semantics are deliberately minimal: the VM uses them mostly as bookkeeping, not as full continuous‑time dynamics.

In the near term we will evolve this into a **constrained ISA** in the hardware sense:

- Every instruction will have an associated **duration** or timing semantics (either explicit on the op, or implied by the device profile).
- The ISA semantics will include **scheduling rules**: what can be parallelized, what must be serialized, and how `Wait` interacts with idle noise.
- Device profiles will include **connectivity graphs** and **native gate sets**, and the VM will reject programs that use non‑native gates or invalid couplings.
- Microarchitectural rules (cooldown times, pulse overlap limits, resource usage) will be surfaced at the ISA level as validation constraints, not hidden inside individual engines.

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
    - Optional per-gate noise hooks (`apply_single_qubit_gate_noise`, `apply_two_qubit_gate_noise`).
  - `SimpleNoiseConfig` and `SimpleNoiseEngine`:
    - Measurement-time combined noise: loss, quantum bit-flip, and readout flips.
    - Per-gate Pauli channels (single-qubit and correlated two-qubit tables) applied to the statevector after gates.
    - Runtime loss modeling (per-gate / idle) so erasures persist across the program.
    - Z-phase kick distributions after gates plus idle-phase drift to capture laser phase noise.

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
   - Extend the ISA to cover hardware constraints explicitly:
     - Gate durations and pulse timing.
     - Scheduling/parallelism rules.
     - Connectivity constraints and native gate sets.
     - Microarchitectural limits (cooldown times, maximum concurrent operations, etc.).

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
  - (New) `site_ids` map each logical slot to a `SiteDescriptor` ID, and `sites` list the full lattice with per-site coordinates and zone metadata, so clients can distinguish an addressable lattice from a chosen occupied configuration.

When the richer fields are populated, `positions`/`coordinates` become derived, backwards-compatible per-slot geometry, while `site_ids`/`sites` describe the actual layout and allow schedulers/compilers to reason about legal subsets, zone limits, and adjacency in terms of lattice identities.

Keeping this section synchronized with `src/vm/isa.hpp` ensures ISA v1 remains a clean, engine-agnostic contract—the exact behavior the new hardware ISA ticket requires.

## 6. ISA Versioning

`src/vm/isa.hpp` also defines `ISAVersion`, `kCurrentISAVersion`, and `kSupportedISAVersions`. Every `JobRequest` carries an `isa_version` with sensible defaults (`1.0` in this release), and the service serializes that field alongside programs and hardware configs.

This explicit version tag is how the VM can offer multiple ISA versions within the same release: downstream clients declare the dialect they are targeting, the service/engine inspects `isa_version`, and the runtime routes to the appropriate interpreter/emulator. Future ISA revisions can live beside the current one while we ship support for old programs, simply by adding new `ISAVersion` values and dispatch logic in `JobRunner`.

Currently the runtime enumerates the supported versions via `kSupportedISAVersions` (currently `[1.0]`), and `JobRunner` refuses to execute programs targeting anything else, returning a clear error that lists the acceptable versions.

Currently the `JobRunner` enforces this contract by rejecting jobs whose `isa_version` differs from `kCurrentISAVersion`, returning a failed status and explaining that the ISA is unsupported.

## 7. ISA v1.1 – Hardware‑Constrained ISA

ISA v1 deliberately models an “idealized logical device”: it exposes a small instruction palette and a minimal `HardwareConfig` so compilers can target a stable contract without committing to particular hardware limits. For realistic hardware work (cooldowns, limited parallelism, non‑uniform connectivity, per‑gate timings), we need a slightly richer ISA that still feels like a neutral‑atom VM and remains backward compatible with v1.

ISA v1.1 is a planned evolution of `src/vm/isa.hpp` that adds **explicit hardware constraints and timing metadata** while preserving the existing opcodes. Until the header is updated, v1.1 should be treated as a design target rather than an implemented wire format.

### 7.1 Goals

- Make hardware limits **first‑class** in the ISA, not an out‑of‑band comment in docs.
- Keep the **same instruction palette** (`AllocArray`, `ApplyGate`, `Measure`, `MoveAtom`, `Wait`, `Pulse`) so existing compilers mostly need to enrich metadata, not rewrite programs.
- Define a contract that can be used both by:
  - compilers (to statically validate programs against hardware), and
  - runtimes (to reject/diagnose illegal schedules at execution time).

### 7.2 New/extended data structures

Relative to v1, ISA v1.1 extends the hardware description rather than adding new opcodes.

- **`ISAVersion`**
  - v1.1 programs set `isa_version = {major = 1, minor = 1}`.
  - v1.0 programs remain valid; the service advertises support via `kSupportedISAVersions` once the implementation lands.

- **`HardwareConfig` (v1.1)**  
  The existing fields remain:
  - `positions: std::vector<double>` – 1D site positions (for legacy/v1 programs).
  - `blockade_radius: double`.

  v1.1 extends this with a richer, hardware‑oriented view (names illustrative – exact C++ layout will be decided when we update `isa.hpp`):

  - `sites` – an array of per‑site descriptors:
    - `id: int` – logical site index.
    - `x, y: double` – 2D coordinates (1D devices set `y = 0`).
  - `zone_id: int` – optional grouping label for per-zone limits.
  - `site_ids` – slot-to-site mapping for the currently selected configuration family (a sequence of `SiteDescriptor.id` values). When `site_ids` is populated, clients no longer have to infer geometry from the legacy 1D `positions` array.
  - `configuration_families` – optional dictionary keyed by human-readable family names. Each entry describes:
    - `site_ids` – which sites are occupied in that family.
    - `regions` – a list of region descriptors that classify occupied traps (data/ancilla/parking/calibration).
    - `description`/`intended_use` – textual hints that the ProfileConfigurator, SDK, and scheduler can show to users.
    - The SDK and ProfileConfigurator emit the chosen family’s name in job metadata (`metadata["configuration_family"]`), so schedulers/logs can explain violations in terms of that concrete configuration.
    - `default_configuration_family` (per profile) controls which family is chosen when a client does nothing special.
  - `regions` – a standalone list of `{ name, site_ids, role, zone_id? }` entries that describe data/ancilla/parking cohorts over the whole lattice. These are the canonical role annotations that schedulers, diagnostics, and the ProfileConfigurator consult when they need to show “this is the ancilla row” or “parking sites should stay empty.”
  - `native_gates` – list of gate families supported by the hardware:
    - `name: std::string` – `"X"`, `"RZ"`, `"CZ"`, `"MOVE"`, etc.
    - `arity: int` – number of targets (1, 2, …).
    - `duration_ns: double` – nominal duration of the gate in nanoseconds.
    - `angle_min/angle_max: double` – allowed range for `Gate.param` (if applicable).
    - `allowed_connectivity` – constraints on target combinations (nearest‑neighbor chain, full graph within a zone, etc.).
  - `timing_limits` – global timing/resource limits:
    - `min_wait_ns`, `max_wait_ns` – bounds for `Wait.duration`.
    - `max_parallel_single_qubit`, `max_parallel_two_qubit` – total concurrent gate count across the device.
    - `max_parallel_per_zone` – per‑`zone_id` concurrency cap.
    - `measurement_cooldown_ns` – minimum logical time between `Measure` on a site and the next gate on that site.
  - `pulse_limits` – optional constraints for `Pulse` instructions:
    - allowed ranges for `detuning`, `duration`, and target sets,
    - per‑zone or per‑site limits on overlapping pulses.

These structures let both compilers and engines reason about what the hardware can actually do without baking those rules into ad‑hoc code paths.

### 7.3 Instruction‑level semantics in v1.1

The instruction palette is unchanged but gains stricter semantics:

- **`AllocArray`**
  - Must not allocate more sites than declared as available in `HardwareConfig.sites`.
  - Engines may use a device‑specific mapping from logical indices to site descriptors; v1.1 requires that mapping to respect per‑site constraints (e.g., sites that are disabled or reserved must not be allocated).

- **`ApplyGate`**
  - `Gate.name` must reference a gate family listed in `HardwareConfig.native_gates`.
  - `Gate.targets` must:
    - be valid site indices, and
    - satisfy the `allowed_connectivity` constraints for that gate family (e.g., nearest‑neighbor pairs in a chain).
  - `Gate.param` must lie within `[angle_min, angle_max]` for parametric gates; static gates ignore the range.
  - Scheduling constraints:
    - Two `ApplyGate` instructions that operate on overlapping sites may not be scheduled at the same logical time.
    - The total number of gates acting in parallel at a given logical time must not exceed `max_parallel_*` limits.

- **`Measure`**
  - May impose a **cooldown** on each measured site:
    - If `measurement_cooldown_ns > 0`, then after measuring a site the engine must advance logical time by at least that amount before an `ApplyGate` involving that site is allowed.
  - Engines may record measurement duration explicitly and treat it similarly to a long gate under the same parallelism limits.

- **`MoveAtom`**
  - Subject to geometry and blockade constraints:
    - Moves must not place atoms closer than a hardware‑defined minimum spacing (derived from `blockade_radius` and the physical model).
    - Engines may enforce per‑move duration and velocity bounds (e.g., max distance per unit time); these are expressed as constraints in `timing_limits` or a future `move_limits` sub‑structure.

- **`Wait`**
  - `Wait.duration` must satisfy `min_wait_ns <= duration <= max_wait_ns`.
  - `Wait` is the primitive that advances logical time between operations; engines must treat it as the gap that enables cooldowns, pulse spacing, and idle noise.

- **`Pulse`**
  - Pulse parameters must fall within `pulse_limits` (if present).
  - Pulses contribute to logical time and respect the same parallelism/zone limits as gates; engines must reject programs that attempt to exceed per‑zone or global pulse counts.

### 7.4 Backward‑compatibility story

- Existing v1.0 programs become v1.1 programs by:
  - upgrading `isa_version` to `{1,1}`, and
  - providing an enriched `HardwareConfig` that is consistent with the earlier 1D `positions` view.
- Compilers that only know about v1 can continue to target v1.0; the service will keep accepting v1.0 jobs as long as `kSupportedISAVersions` includes `{1,0}`.
- New compilers can:
  - query device capabilities (via future service APIs), and
  - target v1.1 to get static guarantees that programs respect durations, connectivity, and parallelism limits.

Once we update `src/vm/isa.hpp` to encode these structures explicitly, this section and the header should again be treated as the **single source of truth** for the hardware‑constrained ISA. For now, this serves as the design document that guides that implementation work.
