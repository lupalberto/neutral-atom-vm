# Neutral Atom VM Python Package

This directory contains the pure-Python helpers for the Neutral Atom VM prototype:

- `neutral_atom_vm.to_vm_program` – lowers Bloqade/Squin kernels to the VM instruction schema.
- `neutral_atom_vm.JobRequest` / `neutral_atom_vm.HardwareConfig` – lightweight value
  objects that describe jobs and hardware configuration.
- `neutral_atom_vm.submit_job` – high-level helper that submits a `JobRequest`
  (or compatible mapping) to the native `_neutral_atom_vm` extension.

To install the package in editable mode (for development):

```bash
python -m pip install -e python
```

Make sure you build the C++ bindings (`_neutral_atom_vm`) via CMake so that the
native `submit_job` entry point is available at runtime.

## Demo workflow

After building the bindings you can exercise the entire pipeline (kernel → VM program →
job submission) with:

```bash
python -m neutral_atom_vm.workflow_demo
```

The script prints the lowered program along with the job result dictionary so you can
inspect the end-to-end flow without Docker.

## Remote service submission

Once the Dockerized service is running (see `neutral-atom-vm/docs/docker-service.md`),
you can ask `quera-vm` to POST jobs remotely instead of executing them locally. Add
`--service-url http://localhost:8080/job` (and optionally `--service-timeout`) to the
`run` command so the CLI streams the serialized `JobRequest` to the HTTP service you
built above.
