# Dockerized Neutral Atom VM with oneAPI

This folder contains a `Dockerfile` that builds `neutral-atom-vm` inside `intel/oneapi-basekit`, compiles the Python bindings with `-DNA_VM_WITH_ONEAPI=ON`, and exposes a simple HTTP service that admits JSON job payloads.

## Image highlights

- base image: `intel/oneapi-basekit:latest` so the Intel SYCL toolchain, `dpcpp`, and related runtime libraries are already present.
- `neutral-atom-vm/python` is `pip install`ed as **UID 1001 / GID 1001** so the build artifacts and runtime files are owned by an unprivileged user.
- `CMAKE_ARGS="-DNA_VM_WITH_ONEAPI=ON"` is injected at install time, ensuring the compiled bindings and the bundled C++ engine pull in the SYCL backend.
- The service entrypoint is `/workspace/neutral-atom-vm/python/scripts/vm_service.py`, which accepts `POST /job` JSON payloads and delegates to `neutral_atom_vm.job.submit_job`.

## Build

From the repository root:

```bash
cd neutral-atom-vm
docker build -t neutral-atom-vm:oneapi .
```

## Run

```bash
docker run --rm -p 8080:8080 neutral-atom-vm:oneapi
```

By default the service listens on `0.0.0.0:8080` and exposes a small REST surface:

- `GET /healthz` — returns `{"status": "ok"}`.
- `POST /job` — enqueues the JSON job payload with the shared VM runner and immediately returns an acknowledgement such as `{"job_id": "job-0", "status": "pending"}`. The job keeps running in the background; use the job-specific endpoints below to follow its progress.
- `GET /job/{job_id}/status` — mirrors `neutral_atom_vm.job.job_status` (`status`, `percent_complete`, `message`, and recent logs) so callers can poll for updates.
- `GET /job/{job_id}/result` — mirrors `neutral_atom_vm.job.job_result` and returns the completed measurements, logs, and diagnostics once the job finishes (returns `404`/`job_id not found` while the job is still running).

You can override the listen parameters via `CMD` arguments. The default entrypoint command is:

```text
python3 python/scripts/vm_service.py --host 0.0.0.0 --port 8080
```

### Submitting jobs from the CLI

Instead of running the job locally you can point `quera-vm run` at the service you just built. Provide the full job endpoint (for example `http://localhost:8080/job`) via `--service-url`, and the CLI will serialize the lowered kernel and POST it to the container. Adding `--service-timeout` lets you increase the HTTP wait period if your jobs take longer than 30 seconds.

## Example request

```bash
curl -X POST http://localhost:8080/job \
  -H 'Content-Type: application/json' \
  -d '{
    "program": [],
    "hardware": {"positions": [0.0]}
  }'
```

The response mirrors the new asynchronous queue, returning just the job identifier and an initial status message:

```json
{
  "job_id": "job-0",
  "status": "pending"
}
```

Use the job-specific endpoints (`GET /job/job-0/status` and `GET /job/job-0/result`) to check progress and fetch the final measurements once the job completes.

### Polling jobs

The service deliberately splits scheduling (a quick HTTP `POST /job`) from execution so you can submit many jobs even if they take a while to finish. Use `curl` (or `quera-vm run` with `--service-url`) to poll the JSON status/result endpoints:

```bash
curl -s http://localhost:8080/job/job-0/status
```

Typical responses look like:

```json
{
  "job_id": "job-0",
  "status": "running",
  "percent_complete": 0.42,
  "message": "",
  "recent_logs": [
    {"shot": 0, "time": 0.1, "category": "Timing", "message": "ApplyGate CX"}
  ]
}
```

```bash
curl -s http://localhost:8080/job/job-0/result
```

When the job has finished, the `/result` response mirrors `submit_job`'s output (measurements, logs, diagnostics). While the job is still running `/result` returns a `404` / `job_id not found`.

### Host connectivity

When you run the Docker image you must publish port `8080` for the host to reach the service. The recommended command is:

```bash
docker run --rm -p 8080:8080 neutral-atom-vm:oneapi
```

Inside the container `localhost:8080` already hits the service, so `quera-vm run --service-url http://localhost:8080/job` works there without extra flags. From the host the same URL only succeeds if the port was published; otherwise the connection is refused (the CLI sees a `ConnectionError`). This explains why the command worked inside the container but failed from the host after the recent service change. Keep the port mapping so host-side tooling can reach the VM service.

## Optional profile

The service can merge a static profile JSON into every request with `--profile /path/to/profile.json`. This is useful if you always want to target a specific geometry or noise model.

## Notes

- The Docker build runs `pip install` as the unprivileged user so the resulting files keep the requested UID/GID.
- You can mount your own job payloads or `vm_service.toml` files with `-v /host/path:/workspace/neutral-atom-vm/job-config` and point the server at them via `--profile`.
- The service is intentionally lightweight (standard library HTTP server) so you can wrap it with `proxy`, `curl`, or gRPC tooling as needed.
