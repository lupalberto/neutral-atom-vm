# Ticket: Hardware VM Emulator

- **Priority:** High
- **Status:** In Progress

## Summary
Implement a “hardware-style” virtual machine that executes programs expressed in the hardware ISA v1, acting as a simple emulator of a neutral-atom device rather than a purely abstract gate-level runtime.

## Notes
- Implement an execution engine that consumes `HardwareConfig` and ISA `Instruction` sequences and produces measurement records, while respecting basic hardware constraints (geometry, blockade, timing).
- Keep the VM implementation separate from the ISA definition and from noise models so that multiple engines (ideal, noisy, Stim-backed, etc.) can share the same ISA.
- Provide a clear entry point for clients (service layer, Python bindings) to load programs into the hardware VM and run them end-to-end.
- Add or extend tests to cover the full “program → hardware VM → measurement results” loop using the new ISA.

## Progress
- The `VM` class interprets `Instruction` sequences defined in `src/vm/isa.hpp`, respects geometry/blockade semantics, logs pulses/waits, and exposes measurements to callers.
- The service layer accepts `JobRequest` objects expressed purely in ISA types and now serializes and enforces ISA version metadata.
- Added a regression test that proves `JobRunner` reports failure when a job targets an unsupported ISA version, ensuring the hardware VM remains tightly coupled to the documented instruction set.
- Introduced `kSupportedISAVersions` and helpers so the runtime can keep a list of all supported dialects, satisfying the requirement to host multiple ISA versions within a single codebase/release.
- Added a first `HardwareVM` façade plus `DeviceProfile` struct that execute ISA programs on top of the existing statevector runtime without changing the Python or service APIs, setting the stage for a QEMU-style hardware VM layer.
