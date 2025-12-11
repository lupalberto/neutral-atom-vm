# Epic: Neutral Atom Lattices, Configurations, and Geometry‑Aware UX

- **Priority:** High
- **Status:** Completed

## Summary
Deliver a cohesive, physically grounded geometry model for the Neutral Atom VM by:
- making **lattices** (`sites`) and **configurations** (`site_ids`) first-class capabilities,
- exposing **regions**, **interaction constraints**, and **transport limits** that match real neutral-atom QPUs, and
- redesigning the ProfileConfigurator UI so users interact with those concepts directly (sites, rows, regions, zones), not just raw position arrays.

This epic ties together multiple tickets so that compilers, schedulers, and users share a consistent view of geometry that is realistic for NA hardware.

## Roadmap & Ordering

### 1. Lattice Regions & Configuration Families
- **Ticket:** `lattice-regions-and-config-families.md`
- **Goal:** Define the canonical physical lattice (`sites` with x/y/z/zone_id) and express multiple **configuration families** and **regions** over that lattice using `site_ids`.
- **Why first:** This is the structural foundation; all later work (UI, interactions, transport) depends on a clear separation between:
  - addressable sites the hardware can host, and
  - which subset is populated for a given configuration/profile.

Key outcomes:
- Built‑in profiles define a single lattice but may expose several named configurations (dense chain, sparse chain, checkerboard, ancilla bands).
- Regions (data/ancilla/parking/disabled) are represented explicitly over `site_ids`.
- Runtime and SDK treat `sites` + `site_ids` as the primary geometry, with `positions`/`coordinates` as derived compatibility layers.

### 2. Geometry‑Aware ProfileConfigurator UI
- **Ticket:** `profile-configurator-geometry-ui.md`
- **Goal:** Redesign the Python `ProfileConfigurator` so it:
  - visualizes the lattice as a map of sites (1D/2D/3D),
  - lets users choose configuration families and regions by name, and
  - supports creating custom configurations by selecting sites/regions,
while still emitting compatible profile payloads.
- **Why second:** Once lattices/config families exist, the UI can become a true **device geometry editor** instead of a numeric form. This is how users and developers will actually interact with the new abstraction.

Key outcomes:
- Users see the physical array (sites, zones) and configuration occupancy, not just numeric positions.
- Selecting a different configuration family visibly changes which sites are used.
- Custom configurations are defined in terms of sites/regions, and the widget emits `sites`, `site_ids`, regions, timing, and noise in the profile payload.

### 3. Interaction Graphs & Crosstalk Constraints
- **Ticket:** `interaction-and-crosstalk-constraints.md`
- **Goal:** Extend geometry from “where sites are” to **how they interact** by:
  - supporting per‑gate interaction graphs or rules (which site pairs are legal for 2Q gates),
  - allowing anisotropic blockade models and zone-aware parallelism, and
  - expressing and enforcing these constraints in terms of lattice sites, regions, and zones.
- **Why third:** With lattices + configurations + regions in place, we can now safely layer interaction physics on top, and tie scheduler/runtime validation to realistic site‑level constraints.

Key outcomes:
- Each device/profile can describe which site pairs support 2Q gates, and the runtime enforces this.
- Blockade/crosstalk checks are phrased in terms of distances and zones over the lattice.
- Error messages reference site IDs/regions/zones, not only integer qubit indices.

### 4. Physical Transport & Rearrangement Semantics
- **Ticket:** `transport-and-rearrangement.md`
- **Goal:** Model realistic **transport** by adding:
  - a transport graph over sites (which pairs can be connected by moves),
  - move/transport limits (distance, counts, rearrangement windows), and
  - a reachability story for configuration families (which are reachable from a base load under these constraints).
- **Why fourth:** This builds on all earlier work:
  - requires a stable definition of lattices and configurations (1),
  - benefits from region/zone semantics and interaction constraints (3),
  - can expose knobs and visual feedback via the new configurator (2).

Key outcomes:
- Profiles can express which moves are physically reasonable and how many are allowed.
- `MoveAtom` and configuration transitions can be validated against a transport model.
- Documentation and UX can explain the end‑to‑end story: load → rearrange → compute on a realistic NA QPU.

## Acceptance Criteria for the Epic
- Geometry model:
  - All devices/profiles use `sites` and `site_ids` as the primary geometry, with optional regions and configuration families.
  - Legacy `positions`/`coordinates` remain available as derived fields for older clients.
- UX:
  - The ProfileConfigurator presents a recognizable lattice view and configuration selector for real devices.
  - Users can create and save custom configurations without manual index management.
- Physics/constraints:
  - Interaction/zone constraints and (optionally) transport limits are expressed and enforced in terms of lattice sites and regions.
  - Error messages and logs describe violations using geometry that matches how experimentalists and compiler authors talk about the hardware.
- Documentation:
  - Architecture/UX docs explain lattices, regions, configuration families, interaction graphs, and transport in concrete neutral‑atom terms, with examples drawn from realistic device scenarios.

## Status Update
- Lattice + configuration ticket is done, and the ProfileConfigurator UI now surfaces configurations/special regions as part of the earned UX upgrade.
- The interaction/crosstalk ticket has been implemented: the service validates interaction graphs, anisotropic blockade radii, and zone overrides while the SDK exposes the same metadata.
- Transport & rearrangement is now implemented: transport edges/move budgets are validated in the service even when `site_ids` are absent, and SDK bindings/tests exercise the new schema.

## Resolution
- All acceptance criteria have been satisfied by making lattices/configurations first-class, adding geometry-aware UX, enforcing interaction/blockade constraints, and validating transport/rearrangement limits in the service.
- The Neutral Atom Lattices, Configurations, and Geometry-Aware UX epic can be closed as every roadmap item now has corresponding implementation + tests.
