# Ticket: Quantum Error Correction Primitives

- **Priority:** High
- **Status:** Done

## Summary
Add support for simulating basic quantum error correction primitives and workflows on top of the Neutral Atom VM, leveraging the new noise models.

## Notes
- Define interfaces for stabilizer-style circuits (syndrome extraction, ancilla preparation, multi-round parity checks) that can be expressed as Squin kernels and lowered to the VM instruction set.
- Extend the service/job API to compute and return logical error metrics (e.g., logical X/Z error rates over many shots) for selected circuits.
- Provide reference examples (repetition code, small surface/color code patches) and regression tests tying QEC behavior to specific noise configurations.

## Design Note (2025-12-10)

This note outlines the concrete design for introducing QEC primitives on top of the existing VM + Stim integration, with minimal new surface area and clear separation of responsibilities.

### 1. Scope and Goals

- Target **small stabilizer-style QEC demos** first:
  - Repetition codes (distance 3, 5) with multi-round parity checks.
  - Tiny surface-code–like patches (optional, later).
- Keep the **VM ISA unchanged** (still gates, waits, measurements).
- Use the **stabilizer backend + Pauli/loss noise** as the primary engine.
- Add **QEC-aware post-processing** in the service/Python layers:
  - Logical X/Z error rates.
  - Optional detection-event summaries.

Non-goals (for now):
- No new ISA ops for “syndrome” or “logical qubit” – this stays at the library/protocol level.
- No full-blown decoder implementations (minimum viable post-processing only).

### 2. QEC Circuit Construction (Squin Layer)

We introduce small, self-contained Squin kernels that build QEC circuits using existing primitives:

- New Squin helpers (in `python/src/neutral_atom_vm/squin_qec.py` or similar):
  - `repetition_code_round(data, ancilla, noise=None)`:
    - Prepares ancilla qubits, applies CX chains implementing a parity check across data qubits, measures ancilla.
    - Optionally applies explicit Pauli channels if we want protocol-local noise beyond device profile noise.
  - `repetition_code_circuit(distance: int, rounds: int, noise_config: SimpleNoiseConfig | None)`:
    - Allocates `distance` data qubits and 1 or 2 ancilla qubits.
    - Runs a configurable number of rounds via `repetition_code_round`.
    - Measures data at the end.
  - Later: similar helpers for “mini surface code” (2×2 or distance-3 patch).

These kernels:
- Are pure Squin (Python) and lower to standard VM ISA (`AllocArray`, `ApplyGate`, `Measure`, `Wait`).
- Are explicitly documented as “QEC demo circuits” and live under a `qec` or `examples` namespace.

### 3. Backend and Noise Model Use

Backend choice:
- Primary target: `--device stabilizer` / `connect_device("stabilizer", profile=...)`.
  - Stim backend handles Clifford gates and Pauli/loss noise efficiently.
- Secondary target: `local-cpu` with Pauli-only `SimpleNoiseConfig` for cross-checks.

Noise configuration:
- Use **device/profile noise** for background Pauli + loss + readout.
- For protocol-specific noise (e.g., an extra depolarizing channel after each parity check), we:
  - Either express it via Squin Pauli-channel helpers (already mapped to Stim) when targeting `stabilizer`, or
  - Approximate via profile-level noise when targeting `local-cpu`.

No changes are needed in `SimpleNoiseConfig` or Stim mapping beyond what we already have for Pauli/loss/readout.

### 4. Service / JobResult Extensions for QEC Metrics

We keep the core C++ `JobResult` schema as-is and compute QEC metrics in the Python layer, to avoid over-specializing the service API:

- In Python (`python/src/neutral_atom_vm/qec_metrics.py`):
  - Add a function:
    - `compute_repetition_code_metrics(result: Mapping[str, Any], distance: int, rounds: int) -> dict[str, float]`
  - Inputs:
    - A `JobResult`-like mapping with `measurements` and (optionally) `timeline`/`logs`.
    - The code parameters (`distance`, `rounds`).
  - Behavior for repetition code:
    - Identify which measurement groups correspond to:
      - Data-qubit final measurements.
      - Ancilla measurements per round (syndrome).
    - For each shot:
      - Reconstruct the data bitstring.
      - Compare against the expected logical value (e.g., all-0 or all-1 encoded state).
      - Count logical X errors (wrong majority) as `logical_error = 1`, else `0`.
    - Aggregate over shots:
      - `logical_x_error_rate = (# logical X errors) / shots`.
      - Optionally `logical_z_error_rate` (if we include phase-flip codes later).
  - Return a `dict` with:
    - `logical_x_error_rate`
    - `shots`
    - Possibly `distance`, `rounds`, and any other relevant metadata.

- For now we **do not** extend the C++ `JobResult` struct. Instead, Python convenience wrappers (examples/notebooks) can attach these metrics in derived views or in the notebook output.

### 5. Public Python API and Examples

- New Python helper API:
  - `neutral_atom_vm.qec.repetition_code_job(distance: int, rounds: int, device, profile, noise=None) -> JobResult`
    - Builds the QEC kernel using Squin.
    - Submits it to the chosen device/profile (defaults to `stabilizer`).
    - Returns a `JobResult` (Python wrapper).
  - `neutral_atom_vm.qec.analyze_repetition_code(result, distance, rounds) -> dict[str, float]`
    - Thin wrapper around `compute_repetition_code_metrics`.

- Examples:
  - `python/examples/repetition_code_qec.py`:
    - Runs a distance-3 repetition code for a configurable number of rounds.
    - Prints:
      - Physical error parameters (from `SimpleNoiseConfig`).
      - Estimated logical X error rate.
  - A small notebook in `notebooks/`:
    - Varies `distance` and/or `rounds`, plotting logical error vs physical error.

### 6. Testing Plan

Unit tests:
- New Python tests in `python/tests/test_qec_primitives.py`:
  - Construct a repetition-code kernel with zero noise:
    - Verify that `logical_x_error_rate` is 0 (within sampling noise).
  - Construct with a small symmetric bit-flip Pauli noise on data qubits:
    - Verify that the measured logical error rate is:
      - Greater than physical error with distance 1.
      - Lower for distance 3 than distance 1 (qualitative trend).
  - Ensure compatibility with both backends:
    - `stabilizer` device when Stim is available.
    - `local-cpu` with equivalent Pauli noise, within looser tolerances.

Integration / regression:
- A CLI-level smoke test in `python/tests/test_cli.py` (optional):
  - A script or entry point that runs a small repetition-code example via `quera-vm run`, confirming that:
    - The job completes on `--device stabilizer`.
    - The JSON output includes measurements that can be passed to `compute_repetition_code_metrics` without errors.

### 7. Incremental Implementation Plan

1. Implement Squin QEC building blocks and a repetition-code kernel (Python only).
2. Add Python-side `compute_repetition_code_metrics` and QEC helper API.
3. Add tests for QEC metrics using the existing stabilizer backend (and `local-cpu` as a cross-check when useful).
4. Add example script + notebook.
5. Revisit whether any C++-side hooks (e.g., tagged QEC jobs) are helpful for future decoders/metrics; keep the initial version Python-only. 

# Implementation status (2025-12-10)

- Added `neutral_atom_vm.qec` helpers that build repetition-code kernels (with tuple-based target lists for Kirin compatibility), submit them to the VM, and derive logical error metrics from `JobResult.measurements`.
- Added `python/examples/repetition_code_qec.py` to demonstrate running the distance-3 repetition code and printing logical error metrics.
- Added regression coverage in `python/tests/test_qec_primitives.py` to verify logical error statistics both with and without additional Pauli noise on the data register.
