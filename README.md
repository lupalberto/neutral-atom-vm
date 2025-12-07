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

## Optional: enable the oneAPI backend

If you have an Intel oneAPI/SYCL toolchain installed, you can build
the GPU-backed statevector engine by configuring CMake with
`-DNA_VM_WITH_ONEAPI=ON`:

```bash
cmake -B build -DNA_VM_WITH_ONEAPI=ON ..
cmake --build build
```

When the build includes the oneAPI backend, the CLI/SDK expose
`local-arc` as a device ID that maps to the same profiles as
`quera.na_vm.sim` but executes on Intel Arc hardware via SYCL.
