# Stim Integration in the Neutral Atom VM Stack

Stim is an important part of our stack, but it plays a different role than the Neutral Atom VM. This document outlines how Stim fits into the broader architecture, how it relates to the VM, and how we intend to keep their roles cleanly separated while sharing a common noise model.

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
- Lowering passes such as `squin_noise_to_stim` that transform Squin/Bloqade programs plus noise annotations into Stim circuits.

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
- The ISA does not include operations like "apply a pauli_channel(px, py, pz)"—that is an engine concern, not a programmatic command.

This mirrors the classical analogy: a CPU ISA does not have an "inject cosmic ray" instruction; cosmic rays are modeled in reliability analyses, not in the instruction stream.

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

