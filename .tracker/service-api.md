# Ticket: Define VM Service API

- **Priority:** High
- **Status:** Done

## Summary
Design the end-to-end job schema and RPC/REST surface so Kirin can submit compiled VM programs to the simulation service.

## Notes
- Align payload with Kirin VM dialect (instructions, hardware config, metadata).
- Specify job lifecycle endpoints (submit, status, cancel, stream results) and error codes.
- Include authentication & quota headers for future multi-tenant deployments.

## Resolution
- Added `service::JobRequest`/`JobResult` structs plus JSON serialization (see `src/service/job.hpp`, `job.cpp`).
- Introduced `service_api_tests.cpp` to lock down the request schema emitted from VM programs.
