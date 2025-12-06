# Ticket: Hardware VM Emulator

- **Priority:** High
- **Status:** Backlog

## Summary
Implement a “hardware-style” virtual machine that executes programs expressed in the hardware ISA v1, acting as a simple emulator of a neutral-atom device rather than a purely abstract gate-level runtime.

## Notes
- Implement an execution engine that consumes `HardwareConfig` and ISA `Instruction` sequences and produces measurement records, while respecting basic hardware constraints (geometry, blockade, timing).
- Keep the VM implementation separate from the ISA definition and from noise models so that multiple engines (ideal, noisy, Stim-backed, etc.) can share the same ISA.
- Provide a clear entry point for clients (service layer, Python bindings) to load programs into the hardware VM and run them end-to-end.
- Add or extend tests to cover the full “program → hardware VM → measurement results” loop using the new ISA.

