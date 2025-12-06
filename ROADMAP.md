# Kirin ↔ Neutral Atom VM Roadmap

## Phase 1 – Service Definition (Q1)
- Finalize VM instruction/job schema aligned with Kirin VM dialect.
- Specify gRPC/REST API for submitting jobs, querying status, and streaming results.
- Decide on authentication, resource quotas, and error reporting semantics.

## Phase 2 – Service Implementation (Q2)
- Standalone C++ service embedding the VM core and exposing the API.
- Job manager that provisions VM instances, runs batches, and streams measurements/pulse logs/state snapshots.
- Persistence for optional artifacts (logs, pulse traces) and metrics instrumentation.

## Phase 3 – Client Integrations (Q2–Q3)
- C++ client SDK and Python bindings (pybind11) for use inside Kirin/Bloqade.
- Kirin backend that lowers circuits into the VM dialect, serializes jobs, and consumes responses.
- CLI tooling for ad‑hoc submission/debugging, including local docker-compose workflows.

## Phase 4 – Deployment & Ops (Q3)
- Container image, Helm chart, and Kubernetes manifests for staging/prod clusters.
- Autoscaling policies plus monitoring/alerts (Prometheus/Grafana) for job health.
- Integration with CI to run nightly regression suites on the service.

## Phase 5 – Advanced Features (Q4)
- Realistic neutral-atom physics and noise modeling (Hamiltonian evolution, decoherence, SPAM, atom loss).
- Quantum error correction and mitigation workflows (stabilizer codes, logical error metrics, noise tailoring).
- Multi-backend support (CPU/GPU/tensor-network) selectable per job.
- Checkpointing/resume for long simulations.
- Multi-tenant quota enforcement and per-user billing hooks.
