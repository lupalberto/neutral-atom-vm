# Ticket: Advanced Backend Support

- **Priority:** Low
- **Status:** Backlog

## Summary
Extend the VM service to dispatch jobs onto alternative backends (GPU, tensor network, Clifford) and expose selection knobs in the API.

## Notes
- Define backend capability flags and validation logic.
- Add checkpoint/resume mechanics for long-running jobs.
- Surface per-backend telemetry so scheduling can make informed placement decisions.
