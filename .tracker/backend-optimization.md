# Ticket: Backend Optimization

- **Priority:** Low
- **Status:** Backlog

## Summary
Swap the naive CPU statevector loops for pluggable backends (GPU, tensor networks, fast Clifford simulators) so large-scale experiments and QEC workloads can run efficiently.

## Notes
- Keep the current reference backend for clarity/tests.
- Explore pybind11 or C API so the VM can load backend plugins at runtime.
