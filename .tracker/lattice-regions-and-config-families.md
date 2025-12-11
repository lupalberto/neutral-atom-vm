# Ticket: Lattice Regions & Configuration Families

- **Priority:** High
- **Status:** Backlog

## Summary
Elevate `sites` + `site_ids` from an internal implementation detail to a first-class *hardware capability* for real neutral-atom QPUs by:
- treating `sites` as the canonical physical lattice (trap layout in 2D/3D with zones), and
- defining named **configuration families** and **regions** over that lattice (dense chains, checkerboards, ancilla rows, parking sites, etc.),
with end-to-end support in presets, SDK, and the ProfileConfigurator UI.

## Motivation
- Real devices expose a fixed trap lattice, then load/rearrange atoms into configurable subsets (load pattern, rearrangement pattern, ancilla/data layout).
- Today profiles just encode a single `positions`/`coordinates` list; there’s no notion of:
  - multiple named configurations sharing the same lattice,
  - region roles (data vs ancilla vs parking),
  - region-aware UX (e.g., “put ancillas on the outer ring” instead of “use indices 8–15”).
- Schedulers and compilers need a shared vocabulary for:
  - where “data” vs “ancilla” qubits live,
  - what configurations are legal on a given device,
  - how to describe geometry choices in a human-readable way for users.

## Proposed Direction
- Treat the ISA-level `sites` list as the canonical physical lattice; `site_ids` describe *one* configuration over that lattice.
- Introduce **Region** and **ConfigurationFamily** concepts at the profile level:
  - `Region`: `{ name, site_ids, role = DATA | ANCILLA | PARKING | CALIB, zone_id? }`.
  - `ConfigurationFamily`: `{ name, site_ids, regions = [...], description, intended_use }`.
- Teach presets to declare:
  - a default configuration family (what we use today),
  - additional families like `"dense_chain"`, `"sparse_chain"`, `"checkerboard"`, `"ancilla_band"`, etc., over the same lattice.
- Extend the ProfileConfigurator to:
  - display lattice-derived coordinates (from `sites`) instead of raw `positions` where possible,
  - let users select a configuration family by name,
  - optionally toggle inclusion of specific regions (e.g., “enable ancilla row 0”).
- Keep `positions`/`coordinates` as derived compatibility views; new work should not require callers to reason about them directly.

## Tasks
- Schema / presets:
  - Extend the preset/config schema to carry `regions` and `configuration_families` keyed by name.
  - Migrate built-in profiles (ideal arrays, noisy grids, lossy block, benchmark chains, readout_stress) to:
    - define a canonical lattice (`sites`) and
    - extract their current geometry into one or more configuration families.
- SDK / runtime:
  - Add SDK helpers to query available configuration families and regions for a device/profile.
  - Ensure `build_device_from_config` and `Device.build_job_request` preserve the chosen family/regions, emitting consistent `site_ids` and `sites`.
  - Make scheduler error messages refer to configuration names/regions where possible (e.g., “illegal gate across regions data/ancilla”).
- UI:
  - Update ProfileConfigurator to:
    - show available configuration families (dropdown or radio list),
    - display summary of regions (counts, roles, rough geometry),
    - still allow fully custom “freehand” site selection via the positions/coordinates editor, but emit a named custom configuration.
- Docs:
  - Extend `docs/vm-architecture.md` and UX docs to define:
    - lattice vs configuration families,
    - region roles and how compilers/schedulers are expected to use them,
    - examples for real devices (e.g., “data in central band, ancillas at edges”).

## Acceptance Criteria
- Built-in devices can:
  - share one `sites` lattice and expose multiple named configuration families, and
  - describe region roles (data/ancilla/parking) over that lattice.
- ProfileConfigurator can:
  - select a configuration family and show its geometry,
  - emit a profile payload that includes `sites`, `site_ids`, `configuration_family`, and `regions` metadata (when applicable).
- Schedulers / error messages:
  - refer to configuration/region names where useful, not just integer indices.
- Documentation clearly distinguishes:
  - physical lattice vs logical configuration families vs regions, with examples grounded in real neutral-atom hardware layouts.

