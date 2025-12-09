# Ticket: Profile Geometry Editor

- **Priority:** Medium
- **Status:** Done

## Summary
Provide a clearer way to define multidimensional device geometries when creating custom profiles in the notebook widget (e.g., true 2-D coordinates or grid-aware editor) so users aren't forced to input flattened 1-D position lists.

## Notes
- Current UX hints at "0,1,2,3" rows but still maps everything onto a single axis, which confuses users trying to model 4x4 grids.
- Explore allowing (x, y) tuples or a small grid editor so layouts match the mental model.
- Ensure the resulting payload still serializes to whatever format the backend expects (maybe emit structured positions that the VM remaps at load time).

## Resolution
- Notebook widget now accepts structured `(x, y, z)` tuples or interactive grid edits and serializes them into the `coordinates` field expected by the VM, while still populating `positions` for backward compatibility.
- Added visual cues and validation so users can see the lattice they’re defining, plus docs explaining how custom profiles should specify geometry in the editor.
