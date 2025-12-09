# Ticket: Backend & Noise Architecture Separation

- **Priority:** High
- **Status:** Done

## Summary
Tighten the Neutral Atom VM prototype to mirror the canonical quantum stack architecture: a clean separation between the VM contract, backend kinds (statevector, stabilizer/Pauli, hardware), and orthogonal noise models (shared noise IR consumed by both VM engines and Stim). The goal is to turn the current “statevector + SimpleNoiseEngine” prototype into a structure where backends and noise are distinct, swappable concerns.

## Notes
- Make the target model explicit in `docs/vm-architecture.md`:
  - Document the canonical layering: frontend/compilers → VM (ISA + job/service + device profiles) → backends (statevector, stabilizer/Pauli, hardware).
  - Describe noise as a separate IR that both VM engines and Stim-based tools consume.
  - Call out that the current prototype intentionally collapses some layers (statevector + SimpleNoiseEngine) and that this ticket is about teasing those apart.

- Separate backend kind from noise model:
  - Introduce a `BackendKind` enum at the VM level in C++ (and mirror it in Python), with values like `Statevector`, `PauliStatevector`, and `HardwareStub` (plus a future `Stabilizer`).
  - Ensure `JobRequest` (C++ and Python) carries backend selection and noise configuration as distinct fields, not just as an opaque device/profile.
  - In Python, treat each preset as a pairing of geometry/timing profile and noise preset, with backend kind as a separate dimension rather than encoding everything into the device ID.

- Define a true backend interface:
  - Add an abstract `IBackend` (or equivalent) in C++ with methods like `prepare(const HardwareConfig&)` and `run(const std::vector<Instruction>&, NoiseEngine*, RNG&, Logs&)`.
  - Refactor `StatevectorEngine` to implement this interface, isolating statevector-specific details behind a consistent contract.
  - Keep `NoiseEngine` as a separate interface; backends should consume noise only through well-defined hooks, not by reaching into noise implementations.

- Make noise orthogonal configuration:
  - Keep `SimpleNoiseConfig` and `SimpleNoiseEngine`, but treat them purely as an independent configuration and engine:
    - `JobRequest` (or a `DeviceConfig` it references) should carry a `NoiseConfig` field distinct from hardware geometry/ISA.
    - Backends receive an optional `NoiseEngine*` or `NoiseContext` and decide how to apply it through their hooks.
  - In Python, clearly distinguish geometry/timing profiles from noise presets so callers can combine them without fabricating extra device IDs.

- Clarify the “Pauli backend” story:
  - Introduce a logical `PauliStatevectorBackend` implementing the backend interface, which wraps the existing statevector engine plus Pauli/loss `NoiseEngine` hooks, and document it as such.
  - In `docs/vm-architecture.md` and the Astro site, explicitly say that in this prototype “Pauli backend” means “Pauli-style noise on top of statevector”, and outline how a true stabilizer/Stim backend could be swapped in behind the same interface later.

- Integrate Stim / stabilizer path cleanly:
  - Use `docs/stim-integration.md` to define the shared noise IR that feeds:
    - the hardware path (VM + `NoiseEngine`), and
    - the QEC path (Stim / stabilizer simulators).
  - Add a `StabilizerBackend` placeholder implementing `IBackend` that will eventually call into Stim, even if it initially just throws “not implemented” or proxies to the Pauli statevector backend.
  - Ensure `JobRunner` dispatches on backend kind, so adding a real stabilizer backend only requires wiring the factory and not touching the VM contract.

- Tighten job/service layer and device profiles:
  - Update `service::JobRunner` and `HardwareVM` so they work with `(BackendKind, HardwareConfig, NoiseConfig)` explicitly, and factor backend creation into a dedicated factory (`create_backend(BackendKind, HardwareConfig)`).
  - Do similarly for noise (`create_noise_engine(NoiseConfig)`), keeping construction and validation in one place.
  - Adjust device/profile definitions (C++ and/or Python) so each profile clearly declares:
    - geometry + timing + native gate set,
    - default backend kind,
    - default noise preset,
    and supports overrides like “same device, new noise” or “same profile, new backend kind”.

- Surface the separation in SDK and CLI:
  - Extend `neutral_atom_vm.connect_device` to accept optional `backend` and `noise` parameters (with backwards-compatible defaults).
  - Extend `quera-vm run` with flags such as `--backend {statevector,pauli,stabilizer}` and `--noise-preset NAME` / `--noise-config PATH`, and explain in the help text that “pauli” may currently be implemented as noise-augmented statevector but is architected to map to a true stabilizer backend later.

- Align documentation and Astro site:
  - Update `docs/vm-architecture.md` and the Astro “Architecture” / “Noise & QEC” pages so they consistently describe:
    - backend kinds (statevector, noise-augmented statevector, stabilizer, hardware),
    - noise models as orthogonal, configured via the shared noise IR.
  - Make the prototype nature explicit: highlight where we intentionally shortcut the canonical architecture today and how this ticket will resolve those shortcuts.

- Testing and migration:
  - Add focused tests that run the same job under different `(backend_kind, noise_config)` combinations and assert the plumbing (correct selection, no crashes, expected error paths), even if distributions differ between backends.
  - Preserve existing interfaces for profiles, SDK, and CLI by mapping old names to new `(backend_kind, noise_preset)` defaults under the hood so current examples and notebooks keep working while the architecture is tightened.

## Resolution
- Introduced a first-class `BackendKind` enum across C++ and Python, updated `JobRequest` to carry backend/noise selections explicitly, and taught device presets plus CLI flags to surface those options.
- Refactored the runtime into an `IBackend` interface implemented by the statevector and Pauli-statevector backends, while `NoiseEngine` creation is now orthogonal and injected via `NoiseConfig`.
- Documentation (architecture pages, Astro site) now reflects the layered model and clarifies how Stim/stabilizer backends will plug into the same factory, with regression tests covering backend/noise selection plumbing.
