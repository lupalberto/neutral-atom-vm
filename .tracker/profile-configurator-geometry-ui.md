# Ticket: Geometry‑Aware ProfileConfigurator UI for Neutral Atoms

- **Priority:** Medium
- **Status:** Done

## Summary
Redesign the `ProfileConfigurator` widget so that users interact with **physical NA device concepts**—lattice sites, regions, and configuration families—instead of editing raw `positions` arrays. The new UI should make it easy to:
- understand the physical lattice (sites, zones, dimensions),
- select and compare named configuration families (dense chain, checkerboard, ancilla band, etc.), and
- create/refine custom configurations by selecting regions and sites,
while still emitting a profile payload that the VM and existing SDK can consume (`sites`, `site_ids`, regions, timing/noise).

## Motivation
- Real NA devices are operated in terms of:
  - a fixed physical lattice of trap sites (2D/3D, zones, layers),
  - specific configurations loaded into that lattice (chains, blocks, checkerboards),
  - roles and regions (data, ancilla, parking, disabled, calibration zones).
- The current ProfileConfigurator UI is mostly a **numeric editor**:
  - it exposes `positions`, spacing, and some noise parameters,
  - but it does not visualize the lattice or clearly separate:
    - lattice vs configuration,
    - data vs ancilla regions,
    - which zones/resources are being stressed.
- As we introduce canonical `sites` + `site_ids` and region/configuration semantics, the UX should:
  - help users reason about **where atoms are** and **which traps are used**,
  - support common physical patterns (“use central row as data, outer rows as ancilla”) without hand‑crafting indices,
  - present validation/limits (blockade, parallelism) in terms of the real device layout.

## Proposed Direction
Restructure the widget into three conceptual areas:

1. **Device & Profile Bar** – unchanged in spirit, but with clearer semantics:
- `Device` dropdown – device id such as `local-cpu`, `stabilizer`, hardware IDs.
   - `Profile` dropdown – preset profiles with labels and small hints:
     - label + persona (“Ideal square grid (education)”),
     - tooltip with qubit count and dimensionality.
   - `Configuration` dropdown (new) – for profiles that define multiple **configuration families** over the same lattice:
     - examples: `"default_dense_chain"`, `"sparse_chain"`, `"checkerboard"`, `"ancilla_band"`.
   - `(Create new configuration)` option – entering a custom mode that lets users define their own configuration over the lattice.

2. **Geometry Panel** – focuses on physical layout and configuration selection:

   **2.1 Lattice Overview**
   - Visual, read‑only “map” of the lattice driven by `sites`:
     - 1D devices: a horizontal strip of dots.
     - 2D devices: a simple grid, one dot per site.
     - 3D devices: layer selector or stacked grids, one per `z` plane.
   - Each site rendering encodes:
     - **occupancy** – filled if its `id` is present in current `site_ids`, hollow if unused,
     - **region role** – color/border for `data`, `ancilla`, `parking`, `disabled`, `calib`,
     - **zone** – subtle tint or stripe for `zone_id`.
   - Hover tooltip shows:
     - `site_id`, `(x, y, z)`, `zone_id`, region name(s).

   **2.2 Configuration Families**
   - `Configuration` dropdown (also accessible from the top bar) lists named configuration families exported by the profile:
     - “Default dense chain (20 data)”
     - “Checkerboard data+ancilla (16 data, 8 ancilla)”
     - “Sparse chain (10 data; every other site)”
   - Brief textual summary for the currently selected configuration:
     - “20 occupied sites over 40‑site lattice; 2 zones; data in central row, ancillas on outer band.”
   - “Duplicate as custom” action:
     - takes the currently selected configuration family and copies it into a mutable custom configuration (e.g., `"my_profile_dense_variant"`).

   **2.3 Configuration Editing (Custom Mode)**
   When user selects `(Create new configuration)` or duplicates an existing one:

   - **Region palette**:
     - Checklist of named regions defined in the profile:
       - `Row0_data`, `Row1_ancilla`, `Parking_ring`, etc.
     - Toggling a region on/off corresponds to adding/removing its `site_ids` from the configuration.

   - **Lattice selection tools**:
     - Simple physical selectors that operate on the lattice instead of raw indices:
       - “Select row…” – choose row index, toggles all sites in that row.
       - “Select column…” – for grid geometries.
       - “Select perimeter/interior” – for block‑like layouts.
       - “Clear all” / “Fill all”.
     - Site‑level editing:
       - click or modifier‑click on individual sites to add/remove them from the configuration.

   - **Role assignment**:
     - For currently selected sites:
       - radio buttons: assign role `data` / `ancilla` / `parking` / `disabled`.
     - Under the hood:
       - updates region metadata or creates a “custom_region_X” group for those sites.

   - **Live feedback**:
     - “Configuration: 32 occupied (24 data / 8 ancilla), 16 spare.”
     - Warnings:
       - “2 sites have overlapping roles (data + ancilla).”
       - “4 occupied sites are marked disabled in hardware.”

3. **Details Panel** – surfaced hardware reality beneath the geometry:

   **3.1 Geometry Summary**
   - Read‑only text summarizing lattice and configuration:
     - “Lattice: 6×6×1 (36 sites), spacing (1.5, 1.0, 1.0) µm.”
     - “Zones: 3 (4, 16, 16 sites).”
     - “Current configuration: 20 qubits (16 data, 4 ancilla); longest contiguous chain length 10 in central row.”

   **3.2 Timing & Limits**
   - Present existing timing/limit fields in a behavioral framing:
     - blockades: “Blockade radius: 1.6 µm.”
     - parallelism: “Max 2 CZ per zone per timestep.”
     - measurement: “Measurement duration: 50 µs; cooldown: 50 µs.”
   - Group controls (still numeric editors) under headings:
     - “Measurement”, “Parallelism”, “Transport” (once `move_limits` is added).

   **3.3 Noise Editor**
   - Retain the current noise configuration accordion, but label it in terms of the selected configuration:
     - “Noise model for configuration `dense_chain` (20 data, 0 ancilla).”

## UX Principles
- **Lattice is hardware**:
  - Users should clearly see that `sites` represent fixed trap locations; changing configuration does not move these dots, it only changes which are occupied.

- **Configuration is occupancy**:
  - `site_ids` = “which traps currently host logical qubits.”
  - UI messaging uses phrases like “occupied sites,” “available parking,” “data band,” not only raw indices.

- **Regions mirror physical controls**:
  - Region names and zones reflect real beam footprints, camera regions, or hardware groupings (not arbitrary software artefacts).
  - Tooling for selecting rows/columns/perimeters matches how operators/viewers think about the array.

- **Errors and limits are physical**:
  - Warnings and constraints refer to:
    - sites (“site 12 in outer row”),
    - regions (“ancilla band”), and
    - zones (“zone B limit exceeded”),
    not just “qubit 12” or “index out of bounds.”

- **Backwards compatibility**:
  - `profile_payload["config"]` remains compatible with existing tooling:
    - still includes `positions` and `coordinates` (derived from `sites`/`site_ids`),
    - always includes `sites` and `site_ids` when available,
    - optionally adds `regions` and `configuration_family` name.
  - The raw positions/coordinates textarea stays available as an “advanced” editor:
    - description updated to clarify that the lattice/regional view is primary, and raw edits may recompute or override derived lattice metadata.

## Tasks
- Widget layout:
  - Restructure `ProfileConfigurator` into:
    - top device/profile/configuration bar,
    - left geometry panel (lattice + configuration family selection + editing tools),
    - right details panel (geometry summary, timing/limits, noise).
  - Keep width/height suitable for notebooks (e.g., ~800–900 px width).

- Lattice rendering:
  - Implement a simple, dependency‑light 1D/2D/3D lattice visual using ipywidgets primitives (e.g., Grids of small buttons or HTML canvas/SVG) that:
    - draws one marker per `SiteDescriptor`,
    - colors/marks occupancy and region roles,
    - supports click interactions for selection in custom mode.

- Configuration family handling:
  - Extend the widget’s internal model to understand:
    - a list of configuration families (from presets or service),
    - the currently active family’s `site_ids` and region metadata.
  - Add UI controls to:
    - select a family,
    - fork to a custom configuration,
    - keep default presets read‑only.

- Region/editing tools:
  - Introduce a region palette view bound to the profile’s region metadata.
  - Implement row/column/perimeter selectors that derive site subsets from `sites` and the grid layout.
  - Allow explicit site toggling and role assignment for fine‑grained editing.

- Payload integration:
  - Ensure `profile_payload` always returns a `config` map that includes:
    - `sites`, `site_ids`,
    - derived `positions`, `coordinates`,
    - (when available) `regions` and `configuration_family` name,
    - noise and timing/limit structures as before.

- Documentation:
  - Update the Python README and any UI docs/notebooks to:
    - explain lattice vs configuration vs regions,
    - show screenshots or examples of the new configurator,
    - highlight how this maps to real NA device concepts (data/ancilla rows, parking sites, etc.).

## Acceptance Criteria
- For built-in devices with non-trivial geometry:
  - the configurator shows a recognizable map of the array (chain, grid, block),
  - selecting a different configuration family visibly changes which sites are occupied,
  - custom configurations can be created by selecting regions/sites, without ever touching raw indices.
- `profile_payload["config"]` emitted from the UI:
  - includes `sites` and `site_ids` consistent with the interactive state,
  - includes compatible `positions`/`coordinates` views,
  - round‑trips through `build_device_from_config` without loss of information.
- Users can:
  - switch between presets and custom configurations,
  - see basic physical summaries (qubit counts, rows, zones),
  - understand from the UI alone which traps are used and for what role (data vs ancilla vs parking) for a given configuration.

## Progress

- Implemented a configuration dropdown, lattice map, and region palette so the widget displays the current family plus occupancy/role coloring for each site, directly tied to `site_ids`.
- Added row/column/perimeter selectors, clear/fill actions, and region role assignment controls so users can build custom configurations at the lattice level while the payload still emits consistent `sites`, `site_ids`, `regions`, and derived `positions`/`coordinates`.
- Surfaced zone summaries, occupancy warnings (non-data/unknown regions), and documented the enriched payload so SDKs/services can reason about family selections and region roles (`profile_payload` now keeps `layout_info`, warnings, and `metadata["configuration_family"]`).
- Expanded widget tests to cover configuration families, row toggles, and warning generation, plus SDK/runtime tests continue to validate configuration-family parsing.
- Split the Geometry tab into dedicated "Physical lattice" and "Positions" subtabs and added a toggleable help blurb so the lattice view keeps occupancy checkpoints while the legacy positions editor and timing controls sit in their own workspace.

## Next Steps

- Collect user feedback on the new lattice controls and warning states so follow-up refinements (e.g., improved zone summaries, richer editing tools, or animation hints) can be scoped in later tickets.
