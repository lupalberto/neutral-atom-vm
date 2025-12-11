# Neutral Atom VM Prototype

This repository is a minimal prototype of a virtual machine for neutral-atom quantum devices.

The goal is to experiment with a multi-level hardware VM design:

- A small hardware-oriented instruction set (alloc array, apply gate, measure).
- A simple statevector-based backend in C++.
- Hooks to add neutral-atom specific features (geometry, blockade, atom moves, noise).

## Build

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Run the demo:

```bash
./vm_demo
```

Run tests:

```bash
ctest
```

## Service discovery

The bundled HTTP service also answers `GET /devices`, mirroring
`neutral_atom_vm.available_presets()`, so external dashboards and the
`ProfileConfigurator` widget can stay in sync with the server.
