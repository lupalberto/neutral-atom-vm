# Ticket: Physical Transport & Rearrangement Semantics

- **Priority:** Medium
- **Status:** Completed

## Summary
Model realistic **transport/rearrangement** on neutral-atom QPUs by:
- giving the VM a notion of which lattice sites support moves (edges),
- specifying distance/time constraints on rearrangements, and
- clarifying how `MoveAtom` and configuration switches relate to physically reachable states,
while keeping the core ISA simple.

## Motivation
- Actual neutral-atom devices rely on rearrangement:
  - load atoms into a “reservoir” pattern,
  - move them into an algorithmic layout (chains, blocks, ancilla bands),
  - swap, park, or remove atoms as needed.
- The current VM:
  - treats `MoveAtom` as a scalar position update with no reachability story,
  - has no notion of which site pairs can be connected by realistic transport,
  - doesn’t connect configuration families (from the lattice ticket) back to a plausible rearrangement path.
- For realistic workloads (compilers, calibration tooling, experimental planning), we need to express:
  - which configurations are reachable from which others,
  - rough move budgets (distance per move, number of moves per shot),
  - how transport interacts with blockade and timing.

## Proposed Direction
- Introduce a **transport graph** over sites:
  - optional `move_edges: list[(src_site_id, dst_site_id)]` on the lattice, or a parametric rule (e.g., nearest neighbors in x or y).
  - optional per-edge “cost” metadata (distance, nominal time).
- Add a `move_limits` sub-structure (as part of `HardwareConfig` or a `TimingLimits` extension) describing:
  - max total displacement per atom per shot,
  - max number of moves per atom or per configuration change,
  - optional “rearrangement window” before the main circuit.
- Clarify the ISA semantics:
  - a `MoveAtom` sequence corresponds to stepping along edges of the transport graph,
  - configurations may be tagged as “reachable from base load under move_limits” vs “purely conceptual”.
- Keep engines simple:
  - Statevector engine may continue to treat `MoveAtom` as a logical update plus geometric validation,
  - but the service layer should validate that requested moves respect the transport graph and limits.

## Tasks
- Schema:
  - Extend `HardwareConfig` with a transport capability (either explicit `move_edges` or a compact rule set).
  - Extend timing/limit structures with `move_limits` (budget for distance/moves).
- Runtime:
  - Add validation in the service layer:
    - reject `MoveAtom` sequences that step outside the transport graph,
    - enforce move limits (distance and count) per shot.
  - Optionally, add a configuration-level “is_reachable” flag based on a simple reachability check from a base configuration.
- SDK / presets:
  - For built-in profiles, sketch realistic-but-simple transport models:
    - chains with nearest-neighbor moves,
    - grids with row/column moves,
    - 3D block-like devices with limited inter-layer transport.
- Docs:
  - Document the difference between:
    - *legal configuration* (geometry + blockade + connectivity),
    - *reachable configuration* (legal + within move budget from a base layout).
  - Provide examples tying real device narratives (load → rearrange → compute) to VM constructs.

## Acceptance Criteria
- Profiles can express which site pairs support moves and under what rough constraints.
- The service rejects obviously unrealistic `MoveAtom` patterns (e.g., teleporting across the chip, unbounded repeated moves).
- Configuration families can be annotated as reachable/unreachable under a given transport model.
- Documentation explains transport semantics in terms that match real neutral-atom rearrangement workflows.

## Resolution
- Added `transport_edges` and `move_limits` to `HardwareConfig` plus ISA helpers so the service can understand lattice moves.
- The service validates transport paths and move budgets in `validate_transport_constraints`, and the profile serializer now emits the new fields for clients.
- The validator derives slot data from available geometry when `site_ids` are missing to keep enforcement active.
- Python bindings expose `TransportEdge`/`MoveLimits`, and regression tests cover rejected moves and per-atom budgets.
