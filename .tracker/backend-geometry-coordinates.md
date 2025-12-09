# Ticket: Backend Support for Multidimensional Coordinates

- **Priority:** High
- **Status:** Done

## Summary
Propagate the new multidimensional `coordinates` data from the Python SDK into the VM runtime so blockade checks, routing, and scheduling operate on actual 2-D/3-D geometry instead of flattened scalar positions.

## Notes
- Python `HardwareConfig` now emits `coordinates`, but the C++ core still consumes only `positions`. After submission, the backend treats the layout as 1-D, so row spacing or plane distances are ignored.
- Needs schema updates (service/proto structs, pybind bindings), runtime changes (distance calculations, adjacency), and compatibility strategy for clients that still send only `positions`.
- Consider storing both `positions` (legacy) and `coordinates` (preferred) in presets so old clients keep working.
- Added runtime work to serialize `coordinates` in the service API, populate `SiteDescriptor`s for downstream engines, and enforce Euclidean blockade distance when present.

## Resolution
- Extended the schema, pybind layer, and runtime structs so every job now carries both legacy `positions` and rich `coordinates`.
- `HardwareVM`, scheduler checks, and blockade logic all consume Euclidean distances from the coordinate tuples, so multi-row/3-D layouts are validated correctly.
- Updated presets/tests to include coordinates while remaining backward compatible with clients that send positions only.
