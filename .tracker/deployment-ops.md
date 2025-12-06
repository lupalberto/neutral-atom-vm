# Ticket: Deployment & Ops

- **Priority:** Medium
- **Status:** Backlog

## Summary
Package the VM service as a container image, add Helm/Kubernetes manifests, and wire up monitoring plus CI for automated testing.

## Notes
- Define resource requests/limits and autoscaling settings.
- Integrate Prometheus/Grafana dashboards and alerting rules.
- Hook into CI to run nightly regression suites against a staging cluster.
