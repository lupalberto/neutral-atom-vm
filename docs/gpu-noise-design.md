# GPU Noise & Shot-Parallel Design Notes

This note captures the abstractions and data flow we need to support the GPU-only backend we sketched in `gpu-backend-roadmap.md`. The focus is on the noise hooks, RNG, and shot batching so we can minimize host–device thrashing while keeping the neutral-atom VM semantics intact.

## 1. Device noise API

- Introduce a `DeviceNoiseEngine` interface whose hooks execute directly on SYCL memory. Each hook receives:
  * A `sycl::queue` reference so it can enqueue kernels.
  * A device accessor to the statevector buffer (`sycl::accessor<std::complex<double>, 1>`).
  * Logical metadata (target qubits, shot index, gate duration) so noise sources can access context.
  * A `DeviceRngState` that lives on the device/host boundary and produces deterministic random bits per shot.

- Concrete noise sources (`DevicePauliSource`, `DeviceLossSource`, etc.) will subclass `DeviceNoiseEngine` and implement the virtual methods. They can enqueue simple kernels that read/write the accessible amplitudes without copying them back.

- A `CompositeDeviceNoiseEngine` mirrors the CPU `CompositeNoiseEngine` but dispatches onto device hooks. The GPU path wires one of these into `StatevectorEngine` when `profile_.noise_engine` is configured and `backend_.is_gpu_backend() == true`.

- For now we leave the CPU `NoiseEngine` untouched so we can keep supporting the existing `SimpleNoiseEngine`. The GPU path will only use the device engine; the CPU path remains unchanged. We can later add helpers to build a device variant from the same config.

## 2. Device RNG

- `DeviceRngState` is a small struct with a 64-bit state (e.g., PCG or xoshiro). It exposes:
  * `std::uint64_t next_u64()` for device kernels.
  * `double uniform()` for host helpers (e.g., measurement sampling).
  * Clone/copy so each shot can seed independently.

- We keep a host-side RNG (`std::mt19937_64`) to seed each shot’s `DeviceRngState` before kernel launches. The device kernels read `DeviceRngState` via accessor so they can continue generating random numbers in-flight.

## 3. Measurement kernels

- The GPU measurement kernel already accumulates probabilities and collapses amplitudes on-device. We keep sampling on the host (because `std::discrete_distribution` lives in the STL). The kernel only writes back the probability vector (size `2^k`) and the collapsed state; everything else stays on the device.

- When we expand the measurement kernel we can optionally run the sampling on-device too once we have a portable device RNG.

## 4. Shot-parallel execution

- Instead of launching one kernel per logical gate per shot, we can pack multiple shots into a single SYCL kernel launch by:
  * Allocating the device buffer as `(shots × dim)` so each shot occupies a contiguous stride.
  * Launching kernels where each work item steps through the per-shot region (`index + shot_stride * shot_id`) and processes all shots in one pass.
  * Or launching batch kernels for subsets of shots to balance resource pressure.

- Shots can share the same `DeviceNoiseEngine` instance since the kernels will receive per-shot metadata (e.g., the shot index) as arguments. Measurement logs become per-shot buffers that get copied back once the kernel finishes.

## 5. Next actions

1. Formalize `DeviceNoiseEngine`/`DeviceRngState` in a new header (`noise/device_noise.hpp`) so downstream code can include the interface.
2. Implement at least one `DeviceNoiseEngine` stub (e.g., a no-op) and hook it into `StatevectorEngine` so we can build and test the GPU code even before we port actual noise kernels.
3. Once the interface exists, rewrite `StatevectorEngine` and the GPU job runner to populate per-shot `DeviceRngState`s and enqueue batched kernels.

These notes complement `gpu-backend-roadmap.md` and should guide the upcoming implementation of device noise kernels and shot batching.
