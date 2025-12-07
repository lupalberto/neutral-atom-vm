# Ticket: Profile Geometry Editor

- **Priority:** Medium
- **Status:** Open

## Summary
Provide a clearer way to define multidimensional device geometries when creating custom profiles in the notebook widget (e.g., true 2-D coordinates or grid-aware editor) so users aren't forced to input flattened 1-D position lists.

## Notes
- Current UX hints at "0,1,2,3" rows but still maps everything onto a single axis, which confuses users trying to model 4x4 grids.
- Explore allowing (x, y) tuples or a small grid editor so layouts match the mental model.
- Ensure the resulting payload still serializes to whatever format the backend expects (maybe emit structured positions that the VM remaps at load time).

## Resolution
- *Pending*
