# Ticket: Backend Support for Multidimensional Coordinates

- **Priority:** High
- **Status:** In Progress

## Summary
Propagate the new multidimensional `coordinates` data from the Python SDK into the VM runtime so blockade checks, routing, and scheduling operate on actual 2-D/3-D geometry instead of flattened scalar positions.

## Notes
- Python `HardwareConfig` now emits `coordinates`, but the C++ core still consumes only `positions`. After submission, the backend treats the layout as 1-D, so row spacing or plane distances are ignored.
- Needs schema updates (service/proto structs, pybind bindings), runtime changes (distance calculations, adjacency), and compatibility strategy for clients that still send only `positions`.
- Consider storing both `positions` (legacy) and `coordinates` (preferred) in presets so old clients keep working.
- Added runtime work to serialize `coordinates` in the service API, populate `SiteDescriptor`s for downstream engines, and enforce Euclidean blockade distance when present.

## Resolution
- Added `coordinates` plumbing from job dictionaries through pybind into the service runner and SIMD engine, so multidimensional layouts now survive submission and are used for blockade checks.  Still need to follow up on scheduler/adjacency/routing improvements downstream.
