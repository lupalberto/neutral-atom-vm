# Ticket: Scheduling & Logical Time Semantics

- **Priority:** High
- **Status:** Backlog

## Summary
Introduce a proper scheduling layer and complete logical-time semantics for the Neutral Atom VM so that ISA-level timing and resource constraints (e.g., measurement cooldown, max parallel two-qubit gates) are enforced by construction, not only as runtime errors. The current prototype exposes timing limits in the ISA and engine but lacks a scheduler and does not advance `logical_time` using gate/measurement durations, which makes examples like `benchmark_chain`'s cooldown look like user-facing quirks instead of a coherent hardware contract.

## Notes
- Clarify the intent in `docs/vm-architecture.md`:
  - Distinguish clearly between:
    - the ISA/device profile declaring durations, cooldowns, and parallelism limits, and
    - a scheduling/compiler layer that produces valid schedules, and
    - backends that execute those schedules.
  - Document that the current implementation is prototype-level: constraints are present and partially enforced, but scheduling/time semantics are not fully realized.

- Complete the logical-time model:
  - Ensure each gate and measurement has a duration (already present in `NativeGate.duration_ns`).
  - Make `StatevectorEngine` (and future backends) automatically advance `logical_time` by the appropriate duration when executing `ApplyGate` and `Measure`.
  - Keep `Wait` as an explicit “extra idle” primitive, not a mandatory tool to satisfy basic cooldown; it should add additional time on top of gate/measurement durations.

- Introduce a minimal scheduling layer:
  - Add a scheduling pass (either in Python or C++) that can:
    - detect trivial timing violations (e.g., back-to-back uses of a measured qubit) and insert `Wait` or reorder gates to fix them, or
    - at least flag them before the program reaches the backend.
  - Start simple: treat the schedule as a sequence of time steps, enforce `measurement_cooldown_ns` and `max_parallel_*` per step, and adjust or reject the program accordingly.
  - In the longer term, connect this to Kirin/Squin passes so that timing-aware scheduling happens at the compiler level rather than inside ad-hoc runtime code.

- Align validation with the new semantics:
  - Keep the ISA/device profile constraints as the single source of truth, but shift some checks earlier:
    - The scheduler/compilers should aim to produce programs that already respect cooldowns and parallelism.
    - The VM/backends should still validate schedules and reject bad ones, but validation should be a safety net, not the primary scheduling mechanism.
  - Update timing-related error messages and logs to reflect the new model (e.g., include gate durations and the offending timestamps).

- Update examples and documentation:
  - Revisit the `benchmark_chain` cooldown example in `docs/ux.md` and the Astro site:
    - Make it clear that the bug in the prototype is the missing scheduler/logical-time semantics, not the presence of cooldown in the ISA.
    - Once the new semantics are in place, update the example to show how a proper schedule avoids the violation without requiring hand-written `Wait` for basic cooldown.
  - Add at least one example of a more complex schedule (with parallel gates and waits) that demonstrates how timing/resource constraints are enforced in a realistic way.
  - UI/logs should annotate time units explicitly. Internal logical-time math stays in nanoseconds, but CLI/SDK outputs now convert to microseconds and include `timeline_units` / `log_time_units` metadata so notebooks and services can render timelines without guessing units.

- Testing:
  - Add tests that:
    - execute simple programs with and without required cooldown and confirm `logical_time` and error paths behave as expected.
    - exercise `max_parallel_two_qubit` and related limits with explicit time steps and waits.
  - Ensure existing examples (e.g., GHZ, maxcut_ring) still run correctly under the new semantics or are updated to be valid schedules.

## Timing references (2025-12-09)

- **Gate durations:** Neutral-atom Rydberg platforms routinely achieve single-qubit Raman pulses and two-qubit CZ/CX operations in the 0.1–1 µs window with >99% fidelity when using optimized Rydberg waveforms (Nature 2023, “High-fidelity parallel entangling gates on a neutral-atom quantum computer”).
- **Measurement durations:** EIT-based readout is quoted at roughly 0.05 ms (50 µs) with >99% fidelity, while ensemble-assisted schemes can complete in <10 µs; these are reasonable ranges for the VM’s logical-time measurement budget (EmergentMind topic “Two-dimensional neutral-atom quantum computing”, accessed 2025-12-09).
- **Lower-bound experiments:** Research demonstrations have reported 6.5 ns Rydberg gates (IEEE Spectrum, “Neutral Atom Qubit … 6.5 ns gate”), but we stick with the conservative 0.5 µs (single-qubit), 1 µs (two-qubit), and 50 µs (measurement) values for default presets.
