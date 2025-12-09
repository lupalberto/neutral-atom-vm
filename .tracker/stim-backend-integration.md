# Ticket: Stim / Stabilizer Backend Integration

- **Priority:** High
- **Status:** Done

## Summary
Introduce a Stim-backed (or compatible stabilizer) backend behind the Neutral Atom VM contract so that effective Pauli + loss noise models can be simulated efficiently on Clifford/QEC workloads. The goal is to share a noise IR between the VM and Stim, and expose a `BackendKind` (e.g., `stabilizer`) that users and compilers can select without changing VM-level programs.

## Implementation Notes (2025-12-09)
- Added `BackendKind::kStabilizer` plumbing and a concrete Stim-backed path in `HardwareVM::run` that materializes Stim circuits, samples via `run_stabilizer`, and reapplies Pauli/readout/loss noise so logs/timelines stay consistent (`src/stabilizer_backend.cpp`, `src/hardware_vm.cpp`, `src/service/job.cpp`).
- Enabled Stim builds by default (`NA_VM_WITH_STIM=ON`) for both the core library and Python bindings, reusing `CONDA_PREFIX` autodetection with override hooks when Stim headers/libs live elsewhere (`CMakeLists.txt`, `python/CMakeLists.txt`).
- Python device presets now register a `stabilizer` alias that mirrors each CPU profile but sanitizes noise down to Pauli/loss/readout terms before emitting jobs; CLI/SDK flows simply pick `--device stabilizer` / `connect_device("stabilizer", ...)` to route work to Stim (`python/src/neutral_atom_vm/device.py`, `python/src/neutral_atom_vm/cli.py`).
- Documentation and Astro site copy now explain how to select the stabilizer backend, its supported ISA/noise envelope, and fallback behavior when a build lacks Stim (`docs/stim-integration.md`, `docs/ux.md`, `astro-site/src/pages/ux.astro`).
- Regression coverage: C++ hardware VM test exercises the stabilizer dispatch, and Python device tests assert the presence/absence of the `stabilizer` presets plus the public `has_stabilizer_backend()` probe (`test/hardware_vm_tests.cpp`, `python/tests/test_device.py`).

## Notes
- Clarify the role of the Stim backend in the overall architecture:
  - VM ISA + device profiles remain the primary contract.
  - Backends implement that contract for different regimes:
    - statevector + noise (current `StatevectorEngine` + `SimpleNoiseEngine`),
    - stabilizer/Stim for Pauli + loss on Clifford-like circuits,
    - future physics / hardware engines.
  - Noise IR and device-level noise configuration should be shared between the VM and Stim where possible.

- Define a minimal noise IR for Stim:
  - Start from what we already encode in `SimpleNoiseConfig`:
    - single-qubit Pauli channels (X, Y, Z probabilities),
    - two-qubit independent Pauli channels (control/target),
    - correlated two-qubit Pauli channels (4×4 table over {I, X, Y, Z}⊗{I, X, Y, Z}),
    - atom loss / erasure probabilities,
    - measurement/readout bit-flip noise.
  - Map this into a Stim-compatible representation:
    - Stabilizer circuits with `pauli_channel`, erasure, and measurement noise primitives.
  - Keep the IR backend-agnostic: it should be possible to derive both Stim configs and `SimpleNoiseConfig` from it.

- Add a `StabilizerBackend` implementation:
  - Extend the VM backend abstraction to include a `StabilizerBackend` that:
    - accepts the same `JobRequest` (or a reduced form) as the statevector backend,
    - translates the VM program + noise IR into Stim operations,
    - executes the circuit in Stim, and
    - returns measurements/logs in the VM’s `JobResult` schema (including erasures if supported).
  - Limit scope initially:
    - Support a subset of the ISA (e.g., Clifford gates + measurement).
    - Defer full neutral-atom geometry semantics to later, or treat them as annotations that do not affect stabilizer evolution.

- Connect backend selection to existing APIs:
  - Introduce a `BackendKind` enum or equivalent field on device profiles to distinguish:
    - statevector,
    - stabilizer/Stim,
    - hardware/hardware-stub.
  - In Python:
    - extend `connect_device` and `build_device_from_config` to accept a `backend` argument (with defaults per profile),
    - expose a simple way to choose Stim (e.g., `backend="stabilizer"` or a dedicated device ID).
  - In the CLI:
    - add `--backend statevector|stabilizer` and wire it through to device/backends.

- Align Stim and VM semantics:
  - Ensure that, for circuits and noise models that lie within the stabilizer regime:
    - statevector + Pauli noise and Stim produce statistically consistent measurement distributions (within sampling error).
  - Document and test cases where:
    - VM noise models include non-Pauli effects (amplitude damping, continuous phase noise, etc.) that Stim cannot represent exactly,
    - and define how the Stim backend approximates or rejects those configurations.

- Testing and validation:
  - Add tests that:
    - run representative Clifford/QEC circuits under both the statevector+Pauli backend and the Stim backend, and compare measurement statistics,
    - verify that device profiles and noise presets can be used with either backend (or that unsupported combinations are rejected with clear errors).
  - Use these tests to keep the VM, noise IR, and Stim mappings in sync as the system evolves.

- Documentation:
  - Update `docs/stim-integration.md` to reflect the concrete backend implementation rather than just design intent.
  - Add a short section to the Astro site (Noise & QEC and/or Open Questions) explaining:
    - when to use the Stim backend,
    - what subset of the ISA/noise it supports,
    - and how it shares noise semantics with the statevector backend.

## Design Plan (2025-12-09)

- **Backend interface & kind wiring**
  - Reuse the existing `IBackend` abstraction and `BackendKind` enum: add a new `BackendKind::Stabilizer` value and extend the backend factory so `create_backend(BackendKind::Stabilizer, HardwareConfig, NoiseConfig)` returns a Stim-backed implementation.
  - Implement a `StabilizerBackend` class that:
    - owns a Stim circuit object and a mapping from VM slot indices to Stim qubit indices,
    - exposes `prepare(const HardwareConfig&)` to allocate qubits and attach geometry annotations (for logging/metadata only),
    - implements `run(const std::vector<Instruction>&, NoiseEngine*, RNG&, Logs&)` by translating each supported ISA instruction into Stim operations and then performing sampling.
  - Keep the backend stateless across shots at the VM level by having `StabilizerBackend` build a single Stim circuit and let Stim handle `sample(k)` internally.
- **Device selection path**
  - Keep backend choice tied to device IDs instead of adding a new override flag: introduce a `stabilizer` device alias that shares the standard presets (geometry, timing) but sanitizes noise configs down to Pauli/loss/readout terms so they can target Stim safely.
  - Update CLI/SDK docs to highlight that `--device stabilizer` (or `connect_device("stabilizer", profile=...)`) routes work to Stim whenever the backend is enabled, falling back with a clear error otherwise.

- **Stim translator layer**
  - Introduce a dedicated `StimCircuitBuilder` helper (or namespace) that converts `(Instruction sequence, NoiseConfig)` into a Stim circuit:
    - maps native gates (`H`, `S`, `CX`, `CZ`, `X`, `Y`, `Z`) to their Stim equivalents,
    - ignores non-stabilizer gates (e.g., arbitrary rotations) by either rejecting the job with a clear error or routing it to the statevector backend (configurable via profile).
  - Integrate the shared noise IR:
    - derive per-operation Pauli channels and erasure probabilities from `NoiseConfig`,
    - emit Stim `PAULI_CHANNEL`, `DEPOLARIZE`, `ERASE`, and measurement noise instructions where supported,
    - fall back to noiseless behavior or explicit “unsupported noise” errors when the configured noise includes non-Pauli components that Stim cannot represent.
  - Preserve VM logical-time and logging semantics by attaching timeline/log entries as we traverse the program, even though Stim itself is time-agnostic.

- **JobRequest, device profiles, and selection**
  - Ensure `JobRequest` already carries `backend_kind` and `noise_config`; extend device profiles so a subset of profiles advertise `backend=stabilizer` (or allow overrides via Python/CLI).
  - In Python:
    - extend `connect_device(..., backend=\"stabilizer\")` and `build_device_from_config` so users can explicitly request Stim-backed execution,
    - validate that the chosen profile + backend combination is compatible (e.g., only profiles with Clifford-native gate sets and Pauli-style noise can target Stim).
  - In the CLI:
    - extend `quera-vm run` with `--backend stabilizer` and plumb it through to the device builder so the service receives `BackendKind::Stabilizer` in the job.

- **Result schema & erasures**
  - Keep using the existing `JobResult` schema:
    - pack Stim measurement samples into the usual `measurements` list and `shots`-by-`slots` bit arrays,
    - represent erasures/atom loss using the same “loss” markers already used by the statevector+noise path (e.g., `-1` or `None` plus `is_loss` flags in derived views).
  - Ensure logs/timelines clearly indicate when a job ran under the Stim backend (backend tag + note in the summary) and when certain instructions or noise terms were dropped due to Stim limitations.

- **Testing strategy**
  - Add focused C++ tests that:
    - build small Clifford circuits (Bell pair, GHZ, simple parity checks) and compare measurement statistics between the Pauli-statevector backend and the Stim backend under identical Pauli noise parameters,
    - assert that unsupported gates or noise configurations are rejected with clear error messages rather than silently mis-simulated.
  - Add Python/CLI smoke tests to confirm:
    - `connect_device(..., backend=\"stabilizer\")` routes jobs to the Stabilizer backend,
    - `quera-vm run --backend stabilizer` produces consistent results and includes backend information in summaries.
