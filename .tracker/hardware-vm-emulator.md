# Ticket: Hardware VM Emulator

- **Priority:** High
- **Status:** Done

## Summary
Implement a “hardware-style” virtual machine that executes programs expressed in the hardware ISA v1, acting as a simple emulator of a neutral-atom device rather than a purely abstract gate-level runtime.

## Notes
- Implement an execution engine that consumes `HardwareConfig` and ISA `Instruction` sequences and produces measurement records, while respecting basic hardware constraints (geometry, blockade, timing).
- Keep the VM implementation separate from the ISA definition and from noise models so that multiple engines (ideal, noisy, Stim-backed, etc.) can share the same ISA.
- Provide a clear entry point for clients (service layer, Python bindings) to load programs into the hardware VM and run them end-to-end.
- Add or extend tests to cover the full “program → hardware VM → measurement results” loop using the new ISA.

-## Progress
- The `VM` class interprets ISA `Instruction` sequences, respects geometry/blockade semantics, logs pulses/waits, and exposes measurements to callers.
- The service layer accepts `JobRequest` objects expressed purely in ISA types and now serializes and enforces ISA version metadata.
- Added regression tests proving `JobRunner` rejects unsupported ISA versions, keeping the hardware VM tightly coupled to the documented instruction set.
-introduced `kSupportedISAVersions` and helpers so the runtime can host multiple ISA versions within one release.
- Added a `HardwareVM` façade plus `DeviceProfile` struct that execute ISA programs on top of the existing statevector runtime. Added C++ tests for hardware VM behavior (ideal run, noise, multiple shots) so the emulator is exercised end-to-end and future backends plug in cleanly.
