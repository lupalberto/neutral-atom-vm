# GPU-only Neutral Atom Backend Roadmap

This document summarizes the requirements and architectural approach for a fully SYCL-native backend (measurements, gates, noise) that keeps the statevector on the Intel Arc GPU and optionally parallelizes the shots.

## Goals
1. All gate applications, idle evolution, measurements, and noise hooks execute on the SYCL device without synchronizing to the host between gates.
2. At most one host synchronization occurs per shot (for returning measurement results or for SDK APIs such as `state_vector()`), and logging is disabled in the oneAPI path to avoid blocking host/device interactions.
3. Multiple shots should be enqueued to the GPU in a batch when the profile asks for >1 shot so that the device can run them in parallel/overlapped.

## Key components
### 1. Device-friendly state and API
- Extend `StateBackend` to expose device-native accessors.
  - A SYCL device pointer (e.g., `sycl::buffer<std::complex<double>, 1> state_buffer_`) remains resident during program execution.
  - Add methods like `sycl::accessor<std::complex<double>, 1> get_device_accessor()` so kernels can mutate state without copying.
  - Provide a `needs_sync()` query so the engine knows if a host-side step demands a copy.

### 2. Device Noise/Measurement Engines
- Introduce `DeviceNoiseEngine` (or similar) whose hooks accept SYCL accessors plus a device RNG state. Interface sketch:
  ```cpp
  struct DeviceNoiseTarget {
    int target;
    int n_qubits;
    sycl::accessor<std::complex<double>, 1, sycl::access::mode::read_write> state;
    sycl::nd_range<1> range;
    DeviceRngState& rng;
  };
  class DeviceNoiseEngine {
   public:
    virtual void apply_single_qubit_gate_noise(DeviceNoiseTarget&) const {}
    ...
  };
  ```
- Implement a SYCL-friendly RNG wrapper (e.g., a simple `sycl::rng` based on PCG) that can be seeded per shot and reused across kernels.
- For now, we can disable logging when the GPU path is active; noise engines do not emit logs (no log sink). That skews to the user's request and avoids host-device log channels.

### 3. Measurement kernel
- A GPU measurement kernel must compute outcome probabilities, sample a result, and collapse the amplitudes without returning the whole state to the host.
  - Precompute per-shot distributions: the kernel traverses the amplitude buffer to accumulate probabilities for the measured qubits.
  - Sampling can be done by invoking a device RNG plus deterministic sweep or by staging a small host kernel after copying just the few probability buckets (much smaller than the full state). If necessary, copy only the probability vector (size `2^k`) to sample on the host and then push collapsed state back.
- After collapse, the kernel stores the measurement record and noise log data in device buffers which are copied back once per shot.

### 4. Shot parallelism
- The job runner should allocate `shots` copies of the statevector buffer and run them concurrently on the device.
  - Each shot maintains its own RNG state and measurement/record buffers.
  - Gate kernels operate over a strided layout: the dimension is `shots * dim`, and each thread updates its shot-specific amplitude index.
  - Alternatively, iterate over shots sequentially but rely on SYCL’s queue to overlap kernel execution; start by batching 4–8 shots at a time for simplicity.

### 5. Engine orchestration
- Update `StatevectorEngine` to accept a `DeviceNoiseEngine` (or composite). The existing CPU `NoiseEngine` remains for CPU path; the SYCL run uses the new interface.
- During gate execution, the engine enqueues kernels that mutate the device buffer. It no longer calls `sync_host_to_device()` or `sync_device_to_host()` except when the SDK requests the amplitudes (e.g., via `state_vector()` or for returning measurement results at the end of the shot).
- When measurements occur, the engine enqueues the measurement kernel, copies back only the small measurement record/probabilities, and collapses the device buffer accordingly.

### 6. Testing expectations (TDD)
Before implementing the backend, add tests that fail because the GPU path still copies the whole vector per gate. Proposed tests:
- Ensure that running a profile with the GPU backend does not trigger `sync_device_to_host()` calls when no noise or measurement occurs (similar to the existing `TrackingBackend` test, but keyed to the new GPU path).
- Add tests verifying that `DeviceNoiseEngine` kernels can mutate the device buffer and that the engine can read measurement records without syncing the entire state.
- Build integration tests that run multiple shots and confirm they complete without per-gate host round-trips.

## Next steps
1. Implement the `DeviceNoiseEngine` abstraction and SYCL RNG/logging scaffolding.
2. Extend `OneApiStateBackend` to expose device accessors and to apply gates via kernels without host syncs.
3. Add the measurement kernel and the shot-parallel job runner, wiring everything via a new GPU-specific `StatevectorEngine` path.
4. Write regression tests for the GPU-only path and update documentation/UX notes once the backend is production-ready.
