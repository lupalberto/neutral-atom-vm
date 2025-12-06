# Ticket: Squin-to-VM Lowering Pass

- **Priority:** High
- **Status:** Done

## Summary
Add a Kirin pass that lowers Squin/Bloqade kernels to the VM instruction schema so programs can be dispatched automatically without manual JSON mapping.

## Notes
- Traverse Kirin IR to collect allocations, gate ops, measurements, move/wait/pulse statements, etc.
- Emit the same instruction dictionaries the Python binding expects.
- Provide a Python helper that takes a `@squin.kernel` and returns a `program` list ready for `submit_job`.

## Changelog
- 2025-12-06: Added regression coverage for Squin RX/RY/RZ lowering and taught `to_vm_program` to extract gate parameters correctly so generated programs can be submitted without manual edits.
