# Ticket: Addressable Site Lattices vs Occupied Configurations

- **Priority:** Medium
- **Status:** Done

## Summary
Clarify and refactor how the Neutral Atom VM represents device geometry so we distinguish between the *addressable site lattice* (all trap sites a piece of hardware can host) and the *occupied configuration* for a particular profile/job (which sites are populated with atoms and in what logical order). Today profiles conflate these concepts by using `positions`/`coordinates` both as the hardware layout and as the slot-to-atom mapping, which limits our ability to describe legal configurations and scheduler behavior accurately.

## Motivation
- Real neutral-atom systems expose a discrete set of trap sites (tweezers, lattice sites) with fixed spacings and control zones. Users and compilers choose subsets of these sites and rearrange atoms between them, but cannot place atoms at arbitrary continuous coordinates.
- The current VM profiles assume all indices `0..N-1` correspond directly to occupied atoms with given `positions`/`coordinates`. There is no explicit representation of the larger addressable lattice or which subset is in use.
- This makes it hard to:
  - express legality of configurations (e.g., which subsets of sites are allowed for a given device),
  - answer UX questions about “placing” ancillas on different rows/columns, and
  - share a consistent view of geometry between schedulers, compilers, and backends.

## Proposed Direction
- Treat the ISA-level `SiteDescriptor` array as the canonical description of the **hardware site lattice**: each site has an ID, coordinates, and optional zone metadata.
- Treat `positions` (legacy) and/or `coordinates` in `HardwareConfig` as the **mapping from logical qubit indices to site IDs**, not as the full lattice:
  - logical slot `i` → site ID `site_ids[i]`, with `SiteDescriptor` storing coordinates and zone info.
  - profiles can then encode different configurations over the same lattice by choosing different subsets/orderings of site IDs.
- Move blockade, connectivity, and zone-parallelism validation to operate over site coordinates/IDs, so “legal configurations” become properties of `(lattice, selected_sites)` instead of just “any list of floats”.

## Tasks
- Extend `HardwareConfig`/device profiles to:
  - carry an explicit list of `SiteDescriptor`s for the full lattice, and
  - represent the chosen configuration as a mapping from logical slots to site IDs (keeping `positions` as a derived/compat layer where needed).
- Update blockade and connectivity checks to:
  - compute distances using site coordinates, and
  - validate that the selected site subset respects min-spacing and control-zone rules.
- Update the profile editor/UX docs so users understand:
  - the difference between defining a lattice vs defining a configuration, and
  - how to request ancillas or rows/columns in terms of site IDs rather than raw coordinates.
- Ensure schedulers and compilers (Kirin, grid-aware passes) consume this representation when deciding where to place logical qubits.

## Acceptance Criteria
- Profiles for real devices can share a single site lattice but expose multiple configurations (e.g., “dense chain”, “sparse chain”, “checkerboard grid”) without redefining geometry from scratch.
- Blockade and scheduling logic operate over site IDs/coordinates consistently, and error messages explain violations in terms of occupied sites.
- Documentation (ISA + architecture docs + roadmap) clearly describes the separation between addressable site lattices and occupied qubit configurations.
