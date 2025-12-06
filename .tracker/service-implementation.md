# Ticket: Implement VM Service

- **Priority:** High
- **Status:** Done

## Summary
Build the standalone C++ service that embeds the VM core, executes submitted jobs, and streams results via the defined API.

## Notes
- Implement job scheduler/runner, measurement streaming, and artifact storage hooks.
- Add basic health checks and metrics endpoints for observability.
- Provide docker-compose entrypoint for local development.

## Resolution
- Added `service::JobRunner` and supporting structs to execute VM programs and return measurement logs.
- Backed by unit tests (`ServiceApiTests.JobRunnerExecutesProgram`) to keep the job bridge working.
- Provides serialization (`to_json`) to integrate with upcoming server/client layers.
