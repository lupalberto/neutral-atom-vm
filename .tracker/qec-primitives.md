# Ticket: Quantum Error Correction Primitives

- **Priority:** High
- **Status:** Backlog

## Summary
Add support for simulating basic quantum error correction primitives and workflows on top of the Neutral Atom VM, leveraging the new noise models.

## Notes
- Define interfaces for stabilizer-style circuits (syndrome extraction, ancilla preparation, multi-round parity checks) that can be expressed as Squin kernels and lowered to the VM instruction set.
- Extend the service/job API to compute and return logical error metrics (e.g., logical X/Z error rates over many shots) for selected circuits.
- Provide reference examples (repetition code, small surface/color code patches) and regression tests tying QEC behavior to specific noise configurations.

