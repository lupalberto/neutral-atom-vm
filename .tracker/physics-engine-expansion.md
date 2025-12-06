# Ticket: Physics Engine Expansion

- **Priority:** Medium
- **Status:** Backlog

## Summary
Grow the current ideal-qubit, hard-blockade VM into a more realistic neutral-atom simulator with continuous-time evolution and geometry-aware interactions.

## Notes
- Introduce a Hamiltonian-based evolution model (including Rydberg interaction terms and laser driving) that underlies high-level gate and pulse operations.
- Generalize geometry from 1D positions to 2D/3D layouts and support time-dependent atom motion with simple constraints.
- Connect `Pulse`, `MoveAtom`, and `Wait` to actual state evolution (rather than bookkeeping only), while keeping a configurable "ideal mode" for fast testing.

