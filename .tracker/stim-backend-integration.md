# Ticket: Stim / Stabilizer Backend Integration

- **Priority:** High
- **Status:** Backlog

## Summary
Introduce a Stim-backed (or compatible stabilizer) backend behind the Neutral Atom VM contract so that effective Pauli + loss noise models can be simulated efficiently on Clifford/QEC workloads. The goal is to share a noise IR between the VM and Stim, and expose a `BackendKind` (e.g., `stabilizer`) that users and compilers can select without changing VM-level programs.

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

