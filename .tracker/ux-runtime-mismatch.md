# Ticket: UX vs Runtime Mismatch

- **Priority:** High
- **Status:** Completed

## Summary
The previously described UX gap—where the Python stack claimed device/profile-based APIs and backend selection but the runtime exposed only the bare `StatevectorEngine`—is now resolved:

* `connect_device` / `Device.submit` now build a full `JobRequest` (with hardware config, device_id, profile, shots, and optional `SimpleNoiseConfig`) that the pybind layer forwards to `_neutral_atom_vm.submit_job`.  
* `JobRunner` now creates a `DeviceProfile` and routes every job through `HardwareVM`, which can attach noise engines and provides an explicit hook for backend selection.
* The new `quera-vm` CLI (`neutral_atom_vm.cli`) exercises the same path, accepts MODULE:FUNC or script targets, honors `--noise`, and can render summaries or JSON results.
* Documentation (`docs/vm-architecture.md` and `docs/ux.md`) now states the intent to encode timing, connectivity, native gates, and microarchitectural constraints in the ISA/service contract.

With these changes, the UX+service implementation now matches the documented journey, so this ticket can be closed.

## Findings
- `docs/ux.md` advertises:
  - `connect_device("quera.na_vm.sim", profile="ideal_small_array")` returning a device handle.
  - The SDK building a `JobRequest`, attaching hardware/noise profiles, and having `JobRunner` select an appropriate backend.
- Current implementation:
  - `neutral_atom_vm.connect_device(device_id, profile)` exists and resolves a small internal `_PROFILE_TABLE` in Python.
  - `Device.submit(...)` calls the native `_neutral_atom_vm.submit_job` which builds a `service::JobRequest` with only `hardware` (positions, blockade_radius), `program`, and `shots`.
  - `JobRequest` does not carry `device_id` or `profile`, and no noise profile is attached.
  - `service::JobRunner` unconditionally constructs a `StatevectorEngine` and does not branch on device/profile or any backend selection.
  - The C++ `HardwareVM` and `DeviceProfile` abstractions are not used by the Python path at all.
- CLI and service/ops journeys (`quera-vm run`, `quera-vm serve`, streaming, device discovery) are documented but not implemented.

## Impact
- Python users see the UX-aligned `connect_device` API, but under the hood everything still behaves like a single unnamed simulator instance. Device IDs and profiles are currently cosmetic at the Python layer.
- Future introduction of multiple backends or real hardware could require breaking changes if the JobRequest/service layer is not aligned now with the advertised device/profile semantics.

## Proposed Fix
- Plumb device ID and profile name through the entire stack so they are not Python-only concepts:
  - Extend `service::JobRequest` with `std::string device_id` and `std::string profile`.
  - Update the pybind11 `submit_job` binding to accept `device_id` and `profile` arguments and populate them.
  - Have `neutral_atom_vm.Device` pass its `id` and profile into `submit_job`.
- Introduce a minimal backend-selection hook in `JobRunner`:
  - For now, select the existing `StatevectorEngine` for all devices, but branch on `device_id`/`profile` and leave a clear extension point for future engines.
- Add placeholders for noise/profile attachment:
  - Extend `JobRequest` metadata to include a `noise_profile`/`profile_id` field derived from the Python device/profile.
  - Document that the current implementation ignores it, but that it is part of the public contract.
- Update `docs/ux.md` to describe the actual under-the-hood path (device/profile carried in the JobRequest, runtime selection hook in `JobRunner`) while keeping the user-facing API unchanged.
