# Ticket: Noise Modeling for Neutral Atom VM

- **Priority:** High
- **Status:** Backlog

## Summary
Introduce configurable noise models for the Neutral Atom VM so simulations can capture decoherence, gate imperfections, SPAM errors, and atom loss.

## Notes
- Add parameterized single- and two-qubit noise channels (dephasing, amplitude/phase damping, depolarizing) that can be attached to gates, waits, and idles.
- Model SPAM (state preparation and measurement) errors and atom loss, and surface them clearly in measurement records.
- Plumb noise configuration through the job/request layer so clients (Kirin/Bloqade) can select standard noise profiles or supply custom parameters.

## 2025-12-06
- Refactored the C++ `NoiseEngine` into a composable framework backed by a new `RandomStream` abstraction so new noise sources can be added without touching the VM hooks. The legacy `SimpleNoiseEngine` now builds on a `CompositeNoiseEngine` plus dedicated measurement, gate, and idle components, and the statevector engine simply supplies a `StdRandomStream` adapter.
