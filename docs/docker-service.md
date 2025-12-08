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

By default the service listens on `0.0.0.0:8080` and exposes two endpoints:

- `GET /healthz` — returns `{"status": "ok"}`.
- `POST /job` — expects a JSON object shaped like `neutral_atom_vm.job.submit_job` takes (`program`, `hardware`, etc.).

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

The response mirrors `submit_job`, returning the job status, measurements, and diagnostics.

## Optional profile

The service can merge a static profile JSON into every request with `--profile /path/to/profile.json`. This is useful if you always want to target a specific geometry or noise model.

## Notes

- The Docker build runs `pip install` as the unprivileged user so the resulting files keep the requested UID/GID.
- You can mount your own job payloads or `vm_service.toml` files with `-v /host/path:/workspace/neutral-atom-vm/job-config` and point the server at them via `--profile`.
- The service is intentionally lightweight (standard library HTTP server) so you can wrap it with `proxy`, `curl`, or gRPC tooling as needed.
