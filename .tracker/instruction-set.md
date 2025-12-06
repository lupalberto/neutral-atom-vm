# Ticket: Extended Instruction Set

- **Priority:** High
- **Status:** Done

## Summary
Introduce additional hardware-level instructions (e.g., `MoveAtom`, `Pulse`, `Wait`, configurable noise ops) so the VM can model neutral-atom control scenarios beyond simple gate application.

## Resolution
- Added `MoveAtom`, `Wait`, and `Pulse` instructions with associated state tracking and tests covering each behavior.

## Notes
- Update `Instruction`/`Op` enums and the interpreter loop.
- Provide placeholder physics so compilers can start targeting the new ISA pieces.
