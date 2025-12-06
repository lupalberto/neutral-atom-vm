# Ticket: Kirin Client & Python Bindings

- **Priority:** Medium
- **Status:** Done

## Summary
Deliver a client SDK (C++ + pybind11) plus a Kirin backend that lowers circuits into the service job schema and consumes responses.

## Notes
- Generate client stubs from the service IDL.
- Integrate with Bloqade tooling so `vm.run()` can transparently target the service.
- Provide CLI tooling for manual submission/debugging.

## Resolution
- Added `service::JobRunner` client helper, pybind11 module (`neutral_atom_vm`) and Python test harness.
- `python/tests/test_client.py` exercises the binding end-to-end via CTest.
