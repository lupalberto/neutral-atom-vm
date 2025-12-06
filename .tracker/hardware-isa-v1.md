# Ticket: Hardware ISA v1

- **Priority:** High
- **Status:** Done

## Summary
Define a simple, first-order hardware instruction set architecture (ISA) for the neutral-atom VM that captures the essential low-level operations (e.g., atom placement/motion, pulses, waits, and measurements) while staying independent of any particular execution engine implementation.

## Notes
- Consolidate the current prototype instruction types into a single, well-documented ISA header (e.g., `src/vm/isa.hpp`) that is the source of truth for `Op`, `Instruction`, and `HardwareConfig`.
- Keep the ISA “hardware-facing”: no engine details (statevector, noise engines, RNGs) should leak into this layer.
- Document ISA v1 semantics in `docs/vm-architecture.md` (opcodes, operands, timing/geometry semantics) and keep the docs in sync with the header.
- Ensure existing tests are updated or extended so that the ISA is exercised via the VM/emulator, not just via internal helpers.

## Resolution
- Extracted the ISA definitions into `src/vm/isa.hpp` and removed duplicates from `src/vm.hpp` so the ISA header is the canonical contract.
- Documented the ISA semantics plus versioning strategy in `docs/vm-architecture.md` and noted it in the roadmap for Phase 0.
- Conveyed the ISA version in each `JobRequest` and JSON serialization, keeping the regression test aligned with the field addition.
