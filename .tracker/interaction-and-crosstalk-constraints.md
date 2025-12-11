# Ticket: Interaction Graphs & Crosstalk-Aware Constraints

- **Priority:** Medium
- **Status:** Backlog

## Summary
Upgrade blockade and two-qubit gate validation from a purely geometric radius check to a **hardware-shaped interaction model** that reflects:
- which site pairs actually support high-fidelity 2Q gates,
- how anisotropic blockade/crosstalk behaves across the lattice, and
- per-zone resource constraints tied to real control hardware,
while keeping the user/program-facing instruction set unchanged.

## Motivation
- Real neutral-atom platforms have:
  - anisotropic blockade (x vs y vs z may behave differently),
  - “good” vs “bad” interaction pairs (e.g., within a beam waist vs far outside it),
  - crosstalk and control limits per beam/zone.
- Current VM behavior:
  - uses Euclidean distances from `coordinates`/`sites` + a global `blockade_radius`,
  - has `ConnectivityKind` to indicate chain vs grid vs all-to-all, but no per-pair fidelity or exclusion,
  - uses `zone_id` and `max_parallel_per_zone` without describing what zones mean physically.
- For realistic scheduling, diagnostics, and error messages, we want:
  - explicit statements like “CZ only between these pairs,”
  - crosstalk-aware parallelism limits,
  - explanations that mention sites/regions/zones, not just vague violations.

## Proposed Direction
- Introduce an optional **interaction graph**:
  - per 2Q gate family, an optional list or rule set describing allowed pairs (e.g., nearest neighbors in x within a zone, or a sparse set of site ID pairs).
  - optional per-pair quality hints (weights or tags) for future calibration/compilation work.
- Extend blockade semantics:
  - allow anisotropic parameters (different effective radii along x/y/z or per-region overrides),
  - still keep the simple global `blockade_radius` for legacy clients.
- Make zones concrete:
  - define in docs that `zone_id` corresponds to a physical control resource group (beam / AOM channel / detector region),
  - tie `max_parallel_per_zone` to that description so parallelism limits read like “max 4 CZs per beam per timestep” instead of an abstract integer.
- Keep the ISA stable:
  - continue to use `ApplyGate` with `Gate{name, targets, param}`,
  - treat interaction/crosstalk constraints as hardware validation, not new opcodes.

## Tasks
- Schema / runtime:
  - Extend `NativeGate` or a nearby structure to encode optional per-gate interaction rules (graph or rule descriptor).
  - Update the statevector engine and/or scheduler:
    - to enforce that 2Q gates only act on allowed pairs,
    - to still fall back to geometric checks when interaction data is absent.
  - Refine blockade checks to optionally use anisotropic or per-region parameters when present.
- Presets:
  - For built-in profiles, define simple but realistic interaction models:
    - chain devices: nearest neighbors along x,
    - square grids: 4-neighbor interactions in-plane,
    - 3D block: limit interactions to same plane or near planes.
- Error reporting:
  - Update error messages to mention:
    - lattice site IDs and/or coordinates,
    - regions/zones (e.g., “zone 1 / row 0”) when constraints are violated.
- Docs:
  - Add a section describing interaction graphs, blockade models, and zones with examples from real hardware scenarios (e.g., laser addressing footprints).

## Acceptance Criteria
- Profiles can express per-gate interaction constraints beyond a simple connectivity enum.
- The runtime rejects 2Q gates that violate declared interaction or blockade constraints, with errors phrased in terms of lattice sites and zones.
- Zone-based parallelism limits are explained and enforced as properties of physical control resources, not just abstract counters.
- Documentation gives at least one concrete, hardware-inspired example of how interaction graphs and crosstalk constraints are specified and enforced.

