# Ticket: Measurement Outputs

- **Priority:** Medium
- **Status:** Done

## Summary
Extend `VM::measure` to respect the requested target qubits and return classical bitstrings or shot data to callers instead of silently collapsing the whole state.

## Notes
- Keep the ability to collapse the measured subset only.
- Expose results through `VMState` or a return value so higher layers can consume them.

## Resolution
- Added `MeasurementRecord` tracking to `VMState`.
- Updated `VM::measure` to sample only the requested targets, collapse amplitudes conditionally, and store the resulting classical bits for later retrieval.
