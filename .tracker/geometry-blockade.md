# Ticket: Geometry-Aware Gates

- **Priority:** Medium
- **Status:** Done

## Summary
Add geometry and Rydberg-blockade checks inside the VM so multi-qubit gates are only applied when atom distances respect the configured blockade radius.

## Notes
- Consume `HardwareConfig.positions`/`blockade_radius` in `apply_gate`.
- Decide whether to reject invalid gates or auto-insert swaps/moves.

## Resolution
- Added blockade enforcement in `apply_gate` for two-qubit gates, using configured positions and radius.
- Added regression tests covering both allowed and forbidden inter-atom distances.
