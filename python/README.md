# Neutral Atom VM Python Package

This directory contains the pure-Python helpers for the Neutral Atom VM prototype:

- `neutral_atom_vm.to_vm_program` – lowers Bloqade/Squin kernels to the VM instruction schema.
- `neutral_atom_vm.submit_job` – provided by the `_neutral_atom_vm` extension module built via CMake.

To install the package in editable mode (for development):

```bash
python -m pip install -e python
```

Make sure you build the C++ bindings (`_neutral_atom_vm`) via CMake so that `submit_job`
is available at runtime.

## Demo workflow

After building the bindings you can exercise the entire pipeline (kernel → VM program →
job submission) with:

```bash
python -m neutral_atom_vm.workflow_demo
```

The script prints the lowered program along with the job result dictionary so you can
inspect the end-to-end flow without Docker.
