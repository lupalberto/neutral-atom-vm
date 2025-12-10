# Stim Integration in the Neutral Atom VM Stack

Stim is an important part of our stack, but it plays a different role than the Neutral Atom VM. This document outlines
how Stim fits into the broader architecture, how it relates to the VM, and how we intend to keep their roles cleanly
separated while sharing a common noise model.

---

## 1. Stim's Role

Stim is a **specialized stabilizer / Pauli-frame simulator**:

- Extremely fast simulation of Clifford circuits with Pauli noise and measurements.
- Native support for:
    - Pauli channels (X/Y/Z errors with configured probabilities).
    - CZ-specific Pauli noise.
    - Repeated stabilizer measurements and QEC workflows.

In our stack (Bloqade/Kirin repos), Stim is integrated via:

- A Stim dialect and wrappers (e.g. `bloqade.stim`).
- Lowering passes such as `squin_noise_to_stim` that transform Squin/Bloqade programs plus noise annotations into Stim
  circuits.

**Stim is the Pauli/QEC back-end**, not the neutral-atom hardware emulator.

---

## 2. Two Compilation Paths

The same high-level Bloqade/Squin program can be compiled down two main paths:

1. **Hardware path → Neutral Atom VM**

    - Lower to a **neutral-atom VM dialect**:
        - Instructions like `AllocArray`, `ApplyGate`, `MoveAtom`, `Wait`, `Pulse`, `Measure`.
        - Geometry and timing information appropriate for neutral-atom hardware.
    - Attach a **device configuration**:
        - Geometry (1D/2D arrays, blockade radius).
        - Operation set (allowed gates, pulses).
        - Noise profile (effective Pauli + loss parameters, derived from calibration and/or modeling).
    - Run on:
        - C++ VM engines (ideal, Pauli, eventual Rydberg physics).
        - Real hardware, when available.

   Use cases:
    - Simulate hardware-like behavior (geometry, blockade).
    - Provide an ISA and job API for client SDKs and services.

2. **Noise/QEC path → Stim**

    - Lower to a **Stim circuit** in the Stim dialect:
        - Clifford gates and measurements.
        - Pauli channels and CZ-specific noise.
    - Run on the Stim engine to:
        - Estimate logical error rates.
        - Prototype and validate decoders.
        - Perform large Monte Carlo sweeps of Pauli noise.

   Use cases:
    - High-throughput QEC and noise analysis.
    - Validating effective noise models and decoder performance.

Both paths share a **noise IR** (Bloqade's Pauli and loss channels), but they use it differently.

---

## 3. Noise IR and Its Two Consumers

Bloqade/Kirin expose a noise dialect with operations like:

- `single_qubit_pauli_channel(px, py, pz, qubits)`
- `two_qubit_pauli_channel(probabilities, controls, targets)`
- `atom_loss_channel(qargs, prob=...)`
- `qubit_loss`, `correlated_qubit_loss`, etc.

This **noise IR** is:

- For Stim:
    - Lowered directly to Stim operations (`pauli_channel1`, `pauli_channel2`, etc.).
    - Stim becomes the *reference implementation* of what these channels mean.

- For the Neutral Atom VM:
    - Used to derive or override an effective **device noise configuration** (e.g. `SimpleNoiseConfig` and successors).
    - The VM's `NoiseEngine` then injects this noise below the VM ISA during simulation.
    - The noise IR does *not* become explicit instructions in the VM program.

In other words, Pauli/loss ops in the compiler are **modeling constructs**, not direct hardware control instructions.

---

## 4. VM ISA vs Noise

The Neutral Atom VM’s instruction set (ISA) is intended to be **noise-blind**:

- Contains only ideal hardware operations and scheduling:
    - Allocate arrays, move atoms, apply pulses and gates, wait, measure.
- Defines the **semantics** of ideal execution:
    - What applying a gate or measurement does to logical state.
    - How geometry and blockade are enforced at the logical level.

Noise and imperfections belong **below** this ISA:

- Device implementations (engines) use a configuration to decide:
    - How gates are realized (ideal vs noisy).
    - How idles and waits contribute to dephasing or decay.
    - How measurements are corrupted (SPAM, loss).
- The ISA does not include operations like "apply a pauli_channel(px, py, pz)"—that is an engine concern, not a
  programmatic command.

This mirrors the classical analogy: a CPU ISA does not have an "inject cosmic ray" instruction; cosmic rays are modeled
in reliability analyses, not in the instruction stream.

---

## 5. Keeping Roles Clean and Aligned

To avoid confusion and conflating roles:

- **Neutral Atom VM**:
    - Virtual hardware ISA for neutral-atom devices.
    - Stable service/job API.
    - Backed by one or more engines (ideal/Pauli/physics/hardware).
    - Noise configured through device profiles and injected by engines, not requested by the program.

- **Stim**:
    - High-throughput Pauli + stabilizer simulator.
    - Canonical implementation of our Pauli/loss noise IR.
    - Used for QEC, decoder validation, and noise analytics.

- **Noise IR (Pauli channels, loss)**:
    - Shared *description* of noise between the two paths.
    - Interpreted as:
        - Concrete operations in Stim.
        - Configuration for VM engines.
    - Never required to appear as instructions in the VM ISA.

By keeping these roles distinct, we ensure:

- The VM feels like a proper *hardware* abstraction for neutral-atom devices.
- Stim remains the *Pauli/QEC branch* of the stack, optimized for error analysis.
- The two stay aligned via a shared noise model, but neither is forced into the other's abstraction.

In particular:

- **Kernel-level Pauli channels live above the VM ISA.** Bloqade's squin + noise dialect (and helpers such as `single_qubit_pauli_channel` / `depolarize2`) are used to describe *where* Pauli/loss noise should appear in a circuit.
- **`SquinNoiseToStim` is the canonical squin → Stim lowering.** When targeting Stim directly, Bloqade rewrites those noise ops into Stim's `PAULI_CHANNEL_*` / erasure primitives at precise locations in the circuit.
- The Neutral Atom VM's ISA remains gate-only; for squin + Stim workflows we treat the resulting Stim circuit as a simulator-specific artifact, attached alongside the VM program rather than encoded as new ISA instructions.

---

## 5.5 Selecting the stabilizer backend today

The current repo now ships a Stim-backed backend behind the existing device abstraction:

- Builds default to `-DNA_VM_WITH_STIM=ON` (pass `-DNA_VM_WITH_STIM=OFF` if Stim is not available in your toolchain). The Python wheel build exposes the same toggle.
- Presets can be targeted by selecting the `stabilizer` device alias. It reuses the geometry/timing metadata from `local-cpu` but automatically sanitizes preset noise down to Pauli/readout/loss terms before emitting the job.
- CLI / SDK usage:

  ```
  quera-vm run \
    --device stabilizer \
    --profile ideal_small_array \
    --shots 1000 \
    tests.squin_programs:bell_pair
  ```

  or `connect_device("stabilizer", profile="ideal_small_array").submit(bell_pair, shots=...)`.

The backend currently accepts the Clifford subset of the ISA plus Pauli/readout/loss noise; unsupported operations raise clear errors so callers can fall back to `local-cpu`. Future work focuses on feeding the shared noise IR directly, expanding the supported gates/noise, and clarifying how non-Clifford segments route between backends.

---

## 6. Open Questions and Architectural Concerns (Notes from Discussion)

The prototype work and subsequent review surfaced several important questions about how Stim fits alongside the VM and other backends.

### 6.1 What exactly do we offload to Stim?

Stim is best used as a specialized backend for:

- Clifford circuits (or circuits where the Clifford part is dominant), and
- Pauli / erasure / readout noise.

In the context of the Neutral Atom VM:

- The VM and service layer still own:
  - The ISA, device profiles, and geometry (blockade, cooldowns, etc.).
  - Job handling (`JobRequest`/`JobResult`) and backend selection.
  - Noise IR / profile selection (which Pauli/loss model applies to this job).
- A Stim-backed `StabilizerBackend` would receive:
  - A validated VM program using a supported subset of the ISA (Clifford gates + measurements).
  - An effective Pauli/loss noise description derived from profiles/IR.
  - It would then:
    - build a Stim circuit (gates + `pauli_channel`/erasure ops),
    - run the circuit for the requested number of shots,
    - and return measurement bitstrings (and erasures) wrapped as VM `JobResult` records.

Stim does **not** enforce neutral-atom geometry or timing rules; those remain VM responsibilities.

### 6.2 Does Stim have a notion of time?

No. Stim operates on an ordered list of operations; it does not assign durations or enforce cooldowns between them.

This means:

- The VM must maintain its own `logical_time` (advancing on gates/measurements/waits) and enforce timing constraints (e.g., measurement cooldown) before handing the sequence to any backend, including Stim.
- For the Stim backend, `logical_time` is a **VM-side concept**:
  - Used to decide if a schedule is valid with respect to device profiles.
  - Used to derive idle noise / cooldown observance when building the Stim circuit.
  - Stim itself simply executes the resulting sequence; it does not know about time or enforce timing constraints.

### 6.3 Why not just lower kernels directly to Stim?

Lowering directly to Stim would conflate:

- the **hardware abstraction** (VM ISA + device profiles), and
- a particular **simulator backend** (Stim).

This causes multiple problems:

- Stim lacks geometry, detailed timing, non-Pauli noise, and pulse-level behavior that real hardware and physics engines care about.
- Stim only covers a subset of workloads (Clifford + Pauli/loss), whereas the VM must also support non-Clifford gates and richer noise via other backends.
- Tying compilers directly to Stim semantics would make it harder to evolve hardware simulators or integrate real devices.

The VM remains the stable **target** for compilers; Stim is one among several **backends** that implement that contract for a particular regime.

### 6.4 Orthogonal vs composable backends

Today, backends are effectively orthogonal:

- `StatevectorBackend` for general gates + richer noise.
- `StabilizerBackend` (planned) for Clifford + Pauli/loss.
- Future physics/hardware backends.

A fair concern is that this fragments capabilities: each backend is its own "island."

In the current prototype, this is acknowledged as a deliberate simplification:

- For a given job, you pick a single backend (statevector, stabilizer, hardware).
- That backend either runs the full program or rejects unsupported features.

Longer-term, we may want a **composable** design:

- A routing/scheduling layer that splits one VM program into segments and:
  - runs Clifford/Pauli segments on Stim,
  - runs non-Clifford or non-Pauli segments on statevector or physics engines,
  - and passes state between engines in a controlled way.

This is a significantly more complex hybrid-simulation architecture and is captured as a future architectural question, not something this PoC solves.

### 6.5 Stim and non-Clifford gates

Stim is fundamentally a stabilizer simulator:

- It is exact and very fast for Clifford + Pauli/loss circuits.
- Non-Clifford gates require approximations or are outright unsupported.

Within the VM:

- A Stim backend will:
  - support only the Clifford subset of the ISA plus Pauli/loss noise,
  - reject (or explicitly approximate) non-Clifford gates and non-Pauli noise when selected.
- Non-Clifford workloads are handled by:
  - the statevector backend (today), and
  - future physics or approximate simulators (later).

The open architectural question is where and how to combine these capabilities without confusing users or violating the VM abstraction.
