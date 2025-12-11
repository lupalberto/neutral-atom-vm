# Ticket: Post-Epic Lattice Integration Sweep (Scheduler, UI, Validators)

- **Priority:** Medium
- **Status:** Completed

## Summary
Once the **Neutral Atom Lattices & Configurations** epic (`epic-neutral-atom-lattice-and-configurations.md` and its child tickets) is implemented, perform a focused sweep to ensure the rest of the VM stack (scheduler, device presets, validators, UI, docs) actually *uses* the new lattice/configuration abstractions and no longer relies on legacy shortcuts (e.g. `blockade_radius` alone, raw `positions`).

This ticket is about tying up loose ends so that lattices, interaction graphs, regions, and transport semantics are consistently reflected across code, UX, and validation.

## Preconditions
- `lattice-regions-and-config-families.md` is completed:
  - `sites`/`site_ids` are canonical, configuration families and regions are plumbed through presets/SDK/ProfileConfigurator.
- `profile-configurator-geometry-ui.md` is completed:
  - the UI uses lattice/configuration concepts instead of only `positions`.
- `interaction-and-crosstalk-constraints.md` is completed:
  - per-gate interaction constraints / graphs and improved blockade/crosstalk semantics exist.
- `transport-and-rearrangement.md` is completed (if in scope for the release):
  - transport graphs and move/rearrangement limits are defined for relevant devices.

## Areas to Update

### 1. Scheduler & Validation
- **Device-level validation (`python/src/neutral_atom_vm/device.py`):**
  - Replace or downgrade direct `blockade_radius` + `positions` checks with:
    - site-based distance checks via `sites` when present, and
    - interaction-graph constraints where defined (only allow declared 2Q gate pairs).
  - Ensure error messages reference:
    - site IDs and/or `(x, y, z)` coordinates from `sites`,
    - regions or configuration family names when available.
- **Service-level scheduler (`src/service/scheduler.*`):**
  - Update any internal geometry/parallelism checks to:
    - use configuration families and regions instead of raw indices,
    - enforce interaction graphs and zone-based limits for parallel 2Q gates.
  - Confirm that timing constraints (cooldown, max_parallel_per_zone) are evaluated in terms of zones associated with `sites`, not implicit assumptions about indices.

### 2. Presets & Profiles
- **Profile metadata (`python/src/neutral_atom_vm/device.py`):**
  - Audit built-in profiles to ensure:
    - each has an explicit lattice (`sites`) and at least one configuration family (`site_ids` + regions),
    - interaction constraints are attached to relevant 2Q native gates (chain, grid, block).
  - For simple teaching profiles (e.g. `ideal_small_array`), keep `blockade_radius` as a descriptive field but avoid relying on it for core validation once interaction graphs are available.
- **Custom profile configs (`--profile-config` path in CLI):**
  - Update validation to:
    - accept and preserve `sites`/`site_ids` when provided,
    - gracefully fall back to legacy `positions` when lattices are omitted, with clear warnings that the configuration model is simplified.

### 3. UI / ProfileConfigurator
- **Geometry UI:**
  - Confirm the configurator:
    - surfaces configuration families and regions for all updated presets,
    - shows interaction/zone constraints where meaningful (e.g., highlighting legal 2Q neighbors).
  - Ensure the emitted payload for custom configurations includes:
    - `sites` (or a reference to a known lattice),
    - `site_ids`,
    - region metadata (roles, zones),
    - interaction constraint hints when the user chooses a topology (chain, grid, etc.).
- **Backward compatibility:**
  - Verify that older notebooks / examples that rely only on `positions` still function, but that new examples prefer the lattice/configuration API.

### 4. Docs & Examples
- **Architecture docs (`docs/vm-architecture.md`):**
  - Update the geometry section to:
    - emphasize lattices + configuration families as the canonical model,
    - mention that `blockade_radius` is now an effective summary, not the primary constraint.
- **UX docs / Astro site:**
  - Ensure geometry examples and screenshots use:
    - configuration family names (“dense_chain”, “checkerboard”) and region labels,
    - connectivity/interaction constraints described in terms of sites/regions.
  - Update any text that still talks about “arrays of floats” to instead reference `sites` and `site_ids`.
- **Examples & notebooks:**
  - Refresh example profiles in notebooks (`Demo.ipynb`, new stabilizer notebook) to:
    - highlight lattice-aware behavior (e.g., ancillas in specific rows),
    - demonstrate error messages when violating interaction/zone constraints.

### 5. Cleanup & Deprecations
- **Blockade radius:**
  - Decide on the long-term role of `blockade_radius`:
    - keep as descriptive metadata for UX/docs, and
    - avoid using it as the sole legality check when `sites` + interaction graphs are available.
  - Add comments/docs indicating its “effective parameter” status and preferred replacements.
- **Raw `positions` use sites:**
  - Identify and minimize internal code paths that rely directly on `positions` for geometry decisions; prefer `sites`/`coordinates` and `site_ids` as the canonical geometry.
  - Use `positions` as a 1D projection or legacy view:
    - For chain-like devices, continue to treat `positions[i]` as the primary x-coordinate of site `i`.
    - For 2D/3D layouts, derive `positions` from `sites` (e.g., row-major index or x-coordinate) and keep them in sync.
  - Where `positions` must remain (legacy APIs, simple profiles), document that `sites`/`coordinates` are the authoritative trap coordinates and `positions` is a convenience/compatibility field.

## Acceptance Criteria
- Scheduler and validation code paths for 2Q gates and parallelism primarily use `sites`, `site_ids`, regions, and interaction graphs, with `blockade_radius` no longer the sole gate legality criterion.
- ProfileConfigurator and CLI/profile-config flows emit and consume lattice-aware configurations consistently across built-in and custom profiles.
- Docs, notebooks, and error messages explain geometry and constraints in terms of lattices, configurations, and interaction graphs, not just raw index distances.
- Legacy configurations that only define `positions` continue to work but are clearly documented as simplified/legacy compared to the new lattice model.
