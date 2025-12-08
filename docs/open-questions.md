# Neutral Atom VM – Open Questions and Architectural Notes

This document captures open questions and unresolved design points that came up while building the prototype and
discussing its architecture. It is intentionally candid: some of these are gaps, not features.

## 1. Scheduling and Logical Time

**What we have now**

- The ISA and `HardwareConfig` include timing-related fields:
    - `NativeGate.duration_ns`
    - `TimingLimits` (including `measurement_cooldown_ns`, `min_wait_ns`, `max_wait_ns`, and parallelism limits).
- The engine tracks `logical_time` and stores `last_measurement_time` per qubit.
- `StatevectorEngine::apply_gate` enforces `measurement_cooldown_ns` by comparing `logical_time` with
  `last_measurement_time` and throwing on violations.
- `StatevectorEngine::wait` advances `logical_time` explicitly via `Wait` instructions.
- The Squin lowering (`to_vm_program`) produces a linear instruction stream with no scheduling / timing awareness and
  never inserts `Wait`.

**Open questions / gaps**

- We do not yet:
    - advance `logical_time` automatically based on gate/measurement durations, or
    - have a scheduler that reasons about parallelism and cooldown when constructing a program.
- As a result, examples like the `benchmark_chain` measurement cooldown appear to require manual `Wait` insertion just
  to respect basic hardware latency. That is more a symptom of missing scheduling / time semantics than an intentional
  user-facing requirement.

**Desired direction**

- In a more complete architecture:
    - Gates and measurements advance `logical_time` by their durations.
    - `Wait` represents *additional* idle time, not the only way to satisfy cooldown.
    - A scheduler/ compiler layer is responsible for producing schedules that respect `TimingLimits` and parallelism
      constraints.
    - The VM’s role is to validate those schedules against the ISA / device profile before dispatching to a backend.
- We need to decide:
    - How much scheduling logic lives in Kirin/Squin vs. in a VM-side pass.
    - How strict the VM should be in rejecting schedules vs. auto-filling idle when allowed by the spec.

## 2. “Pauli backend” vs. Noise-Augmented Statevector

**What we have now**

- Documentation and Astro copy refer to a “Pauli backend” alongside statevector and hardware.
- In code, there is no separate Pauli backend:
    - We have a statevector backend (`StatevectorEngine` + `StateBackend` implementations).
    - We have a composable noise framework (`SimpleNoiseConfig`, `NoiseEngine`, Pauli/loss sources).
    - Profiles select noise presets; engines apply noise hooks around gates/measurements.
- The phrase “Pauli backend” currently means “statevector + Pauli-style noise configuration,” not a distinct stabilizer
  engine.

**Open questions**

- Do we want a dedicated stabilizer/Stim-backed backend behind the same VM contract, or is “statevector + Pauli noise”
  sufficient for the foreseeable use cases?
- How should we represent backend kind vs. noise model:
    - `BackendKind` enum (statevector, stabilizer/Stim, hardware, etc.)?
    - Noise IR as a separate configuration that both VM engines and Stim consume?

**Desired direction**

- Architecturally, it is cleaner to:
    - treat backend kind (statevector, stabilizer, hardware) and noise model as orthogonal; and
    - define a shared noise IR that can configure both VM engines and Stim.
- The current design is a step in that direction but still bundles:
    - backend choice,
    - geometry,
    - and noise configuration
      inside profiles. We should unbundle these concerns.

## 3. Noise as an IR vs. Implementation Detail

**What we have now**

- Device-level noise is configured via `SimpleNoiseConfig` and profiles, exposed through:
    - Python presets and JSON `--profile-config` files.
    - C++ `NoiseEngine` implementations hooked into the VM.
- We talk about a shared noise IR in `docs/noise.md` and `docs/stim-integration.md`, but:
    - program-annotated noise (e.g., `pauli_channel` emitted by Bloqade/Squin) is not yet wired into the VM; and
    - there is no actual Stim backend consuming a concrete IR from this codebase.

**Open questions**

- Where exactly should the noise IR live and how should it flow?
    - Calibration / DSL → noise IR → (a) VM engines, (b) Stim?
    - Or calibration → device profiles → engine-specific configs only?
- How should program-level noise annotations interact with device-level noise?
    - Are they additive, overriding, or just a separate mode?
- How do we ensure reproducibility and clarity from the user’s perspective:
    - Noise that is purely “inside the backend” but not visible at the VM/IR level is hard to reason about and share.

**Desired direction**

- Noise should be:
    - described at a VM/IR level (configurable, serializable, versioned), and
    - implemented by backends in a way that preserves semantics across engines.
- Device profiles and JSON configs are acceptable as a first pass, but:
    - we eventually want a noise IR that is shared between the VM and Stim;
    - program-level noise annotations should be part of the IR/ISA story, not ad hoc backend hooks.

## 4. User-Facing Noise Control vs. Abstraction Boundaries

**What we have now**

- Users / DSLs can choose noise only via:
    - device profiles (e.g., `noisy_square_array`, `lossy_chain`), or
    - full profile JSON passed as `--profile-config` / `build_device_from_config`.
- There is no way for users to express program-specific noise scenarios at the VM ISA level yet (e.g., “apply this Pauli
  channel on this layer of the circuit only”).

**Open questions**

- How much noise control should users have directly at the VM level?
    - Only through device/profile selection?
    - Through program-level noise annotations / instructions?
- Where is the right boundary between “hardware-like abstraction” and “experiment control”?
    - Too little control: QEC and algorithm developers cannot express the scenarios they need.
    - Too much control: the VM leaks backend-specific details and loses its “hardware-like” abstraction property.

**Desired direction**

- Treat user/DSL-driven noise as part of an extended, well-defined contract:
    - e.g., an extended ISA or dialect with explicit noise instructions/annotations.
    - Engines and Stim implement that contract consistently.
- Keep backend internals hidden:
    - users should not have to know how `SimpleNoiseEngine` is implemented,
    - but they should be able to select and vary noise models in a reproducible, documented way.

## 5. How to Reflect This in Docs and Astro Site

**Agreement points from the conversation**

- We should not present current quirks (e.g., needing `Wait` to satisfy basic cooldown) as fully intentional “features.”
- The architecture is partially sound (ISA + device constraints + backend validation), but notably incomplete without:
    - a proper scheduling layer, and
    - a full logical-time model.
- The noise story is architecturally motivated (shared IR across VM and Stim), but today we only implement the
  device-level configuration path, not the complete IR flow.

**Actions taken so far**

- Added a “Known limitations of this prototype” section on the Astro Overview page that:
    - calls out the missing scheduler,
    - describes the incomplete time model,
    - and notes that backend vs. noise are not yet fully separated.
- Created tracker tickets:
    - `backend-noise-architecture-separation.md` – for unbundling backend kind and noise IR.
    - `scheduling-and-logical-time.md` – for fleshing out scheduling and logical-time semantics.

**Remaining documentation questions**

- How prominently should we surface these limitations in public-facing docs vs. internal docs?
- At what point (once a minimal scheduler or stabilizer backend exists) do we tighten the language on the Astro site and
  in high-level docs to reflect the more complete architecture?

