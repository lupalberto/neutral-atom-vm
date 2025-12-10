# Ticket: squin Pauli Channels → VM / Stim

- **Priority:** Medium
- **Status:** Done

## Summary
Schedular/Stim integration now surfaces pauli-channel noise operations in the execution timeline and allows the backend to report both the scheduler “plan” and the actual Stim events, ensuring program-specified noise (PAULI_CHANNEL) is logged, serialized, and playable through the stabilizer backend.

## Motivation
- Bloqade already has a noise dialect (`bloqade.noise.native`) and squin stdlib shorthands (e.g. `squin.single_qubit_pauli_channel`, `squin.two_qubit_pauli_channel`, depolarizing helpers), plus a `SquinNoiseToStim` rewrite that lowers those ops into Stim `PAULI_CHANNEL_*` instructions.
- Our neutral-atom VM stack currently:
  - uses `SimpleNoiseConfig` / device profiles to describe Pauli + loss noise, and
  - sanitizes that configuration for the Stim backend,
  but does not preserve or interpret squin-level noise ops in the VM ISA.
- For QEC and noise-analysis workflows we want compilers and users to be able to place Pauli channels at specific program points, with Stim as the high-throughput execution engine.

## Design Sketch

This section is meant to be concrete enough that a future agent can implement it without redesigning from scratch. The key constraints are:
- the **VM ISA remains hardware-facing** (gates, waits, measurements only); and
- we **reuse Bloqade’s `SquinNoiseToStim` rewrite** as the source of truth for how squin noise maps into Stim circuits.

### 1. Reuse `SquinNoiseToStim` for squin + stabilizer

Squin kernels already lower to an IR that includes noise ops and can be rewritten to Stim via `SquinNoiseToStim`. For the `stabilizer` backend we should:

- In the Python SDK, recognize when:
  - the kernel is a squin kernel (i.e. produced by `@squin.kernel`), and
  - the selected device/backend is Stim-backed (`device_id == "stabilizer"` or profile metadata says `backend=stabilizer`).
- For that combination:
  1. Lower the squin kernel to its IR as today.
  2. Run Bloqade’s `SquinNoiseToStim` pass to produce a Stim circuit that already includes:
     - all Clifford gates,
     - all Pauli/loss channels, and
     - any supported measurement noise.
  3. Attach this Stim circuit to the `JobRequest` as a dedicated field (e.g. `stim_circuit`), serialized in a way the C++ side can consume (string or structured JSON, depending on what the Stim bindings expect).
- Still build the usual VM program from the kernel:
  - for timelines, geometry validation, and cross-backend consistency, but
  - make clear that for Stim we **do not reconstruct the circuit from the VM ISA**; we trust `SquinNoiseToStim` as the canonical lowering.

This keeps all of Bloqade’s noise semantics and Stim-specific corner cases in one place.

### 2. squin → VM lowering (for non-Stim backends)

For other backends (statevector, hardware, future physics engines) we keep the existing squin → VM lowering behavior:

- `python/src/neutral_atom_vm/squin_lowering.py` continues to emit only ISA-level ops (`AllocArray`, `ApplyGate`, `Measure`, etc.).
- Squin noise helpers are either:
  - rejected when targeting hardware backends (clear error that explicit noise is simulation-only), or
  - approximated via `SimpleNoiseConfig` / device-level noise when targeting the noisy statevector backend (future work).

No ISA changes are required for this; the VM program remains independent of the Stim-specific squin rewrite.

Optional phase 2:
- Once the basic mapping works, add a capability check so lowering can emit a clear error if a kernel uses noise helpers but the selected device/backend does not support `ApplyNoise` (e.g. a real hardware backend).

### 3. Backend behavior

Stim backend (`src/stabilizer_backend.cpp`):
Teach the backend to prefer a prebuilt Stim circuit when it is available:

- Extend the C++ job representation (`JobRequest` / `DeviceProfile` / `HardwareVM`) to optionally carry a Stim circuit payload (e.g. `std::optional<std::string> stim_circuit_text`).
- In `HardwareVM::run` when `profile_.backend == BackendKind::kStabilizer`:
  - If a Stim circuit is present on the job/profile:
    - Parse that circuit (using Stim’s C++ API) and sample it directly.
    - Populate `RunResult.measurements` / `RunResult.logs` to match the existing schema.
  - If no Stim circuit is present (e.g. non-squin clients using only VM JSON):
    - Fall back to the existing `StimCircuitBuilder` that translates the VM program + `SimpleNoiseConfig` into a Stim circuit.
- Device/profile noise:
  - For the “prebuilt Stim circuit” path, assume `SquinNoiseToStim` has already encoded all explicit Pauli channels and that only device/profile measurement noise (e.g. loss/readout) needs to be applied afterwards in C++ (via `apply_measurement_noise`).
  - For the fallback path, keep using `SimpleNoiseConfig` as we do today.

Statevector backend:
- Unchanged by this ticket. It continues to rely on `SimpleNoiseConfig` and does not see the Stim circuit payload.

Hardware / future physics backends:
- For now, reject squin kernels that use explicit noise helpers when targeting real hardware (or document that such kernels are simulation-only and must be compiled without the noise passes for hardware runs).

### 4. Capability and UX

- Device profiles (`python/src/neutral_atom_vm/device.py`) should advertise backend capabilities via metadata:
  - e.g. `"supports_kernel_noise": true` for `stabilizer`, false for real hardware.
- The Python SDK and CLI can:
  - warn or error if a kernel containing `ApplyNoise` is submitted to a device that doesn’t support it.
  - suggest `--device stabilizer` / `connect_device("stabilizer", ...)` when such kernels are detected.

### 5. Testing

Minimum tests once implemented:
- Python-level:
  - A `@squin.kernel` that calls `squin.single_qubit_pauli_channel` lowers to a VM program containing an `ApplyNoise` dict with the correct `kind`, `targets`, and `params`.
  - Submitting that kernel to `--device stabilizer` runs without error and produces a distribution consistent with the configured Pauli channel (e.g. simple 1-qubit depolarizing test compared against Bloqade’s Stim path).
  - Submitting the same kernel to a hardware-style device results in a clear error message.
- C++:
  - Directly build a program with `Op::Noise` instructions and feed it to `HardwareVM` with `BackendKind::kStabilizer`, validating that Stim receives the expected `PAULI_CHANNEL_*` instructions (via measurement statistics).

## Tasks
- **Front-end / squin integration**
  - Confirm and document the squin stdlib noise API we support (e.g. `squin.single_qubit_pauli_channel`, `squin.two_qubit_pauli_channel`, `squin.depolarize*`, loss channels).
  - Decide whether we:
    - run Bloqade's `SquinNoiseToStim` rewrite in the squin → VM path when targeting `stabilizer`, or
    - extend our squin → VM lowering to emit explicit VM-level noise ops that backends can interpret.

- **VM ISA / Job representation**
  - Introduce a way for the VM IR / `JobRequest` to carry per-op Pauli channels:
    - either as new instructions (e.g. `ApplyNoise` with Pauli-channel payload), or
    - as annotations that the backend translates into local noise applications.
  - Ensure this representation composes cleanly with device/profile `SimpleNoiseConfig` (e.g. kernel-level channels override or layer on top of the profile noise, with documented semantics).

- **Stim backend mapping**
  - Extend `StimCircuitBuilder` so it can:
    - read kernel-level Pauli/loss annotations/ops and emit corresponding Stim `PAULI_CHANNEL_*` and erasure instructions at the correct points, and
    - continue to honor sanitized profile noise for background/device-level effects.
  - Add tests where a squin kernel explicitly uses Pauli-channel helpers and the stabilizer backend reproduces the expected statistics (compared against Bloqade's own Stim path where possible).

- **Docs / UX**
  - Update `docs/stim-integration.md`, `docs/noise.md`, and the Astro site to:
    - distinguish profile-driven noise from kernel-driven noise,
    - document that kernels using explicit Pauli channels require simulator backends (statevector+noise, Stim) and are not directly executable on hardware backends.

## Acceptance Criteria
- A `@squin.kernel` that uses the documented Pauli-channel helpers can be lowered to the VM, executed on `--device stabilizer`, and produces Stim-backed results consistent with Bloqade's `SquinNoiseToStim` path.
- The same kernel, when compiled for a real device backend, is either rejected with a clear error or treated as a simulation-only artifact per the docs (no silent mis-execution).
- Noise-related documentation clearly explains how to configure device/profile noise vs how to express program-specific Pauli channels in squin.
