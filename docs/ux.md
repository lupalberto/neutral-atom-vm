# Neutral Atom VM User Experience

This document captures the intended journey for the different personas that will touch the hardware virtual machine (VM). It complements `docs/vm-architecture.md` by focusing on the **user-facing flows**, the analogy to a virtual device, and the CLI/SDK surfaces we should build.

---

## Personas

1. **Algorithm author (Bloqade/Squin user)**  
   Writes kernels against Bloqade/Squin and wants to run them on “QuEra hardware”—either real or emulated.  
2. **Compiler/tooling engineer**  
   Develops Kirin passes or scheduler tooling that targets the VM dialect. Needs a stable target and rich diagnostics.  
3. **Infrastructure/operator**  
   Deploys the VM as a service, selects device profiles, and monitors jobs running across backends.

Each persona interacts with the VM via different surfaces, but they all rely on the same underlying instruction set and service contract.

---

## 1. Python SDK / Bloqade Journey

Goal: `connect_device("quera.na_vm.sim", profile="ideal_small_array")` returns a device handle that behaves like actual hardware.

Flow:

1. Author writes a kernel:
   ```python
   from bloqade import squin

   @squin.kernel
   def ghz():
       q = squin.qalloc(3)
       squin.h(q[0])
       squin.cx(q[0], q[1])
       squin.cx(q[0], q[2])
       squin.measure(q)
   ```
2. Lowers this kernel via `to_vm_program` into a list of `Instruction` dictionaries.
3. Device selection:
   ```python
   from neutral_atom_vm import connect_device
   dev = connect_device("quera.na_vm.sim", profile="ideal_small_array")
   ```
4. Job submission:
   ```python
   job = dev.submit(ghz, shots=1000)
   result = job.result()
   print(result.measurements)
   ```
   The helper `neutral_atom_vm.connect_device` now exists and returns the `dev` handle with minimal configuration. Profiles are resolved internally (e.g., `"ideal_small_array"`), so callers do not configure positions directly. For backwards compatibility we also expose `device_id="runtime"` which still drives the local C++ runtime directly using a built-in default profile.
5. Under the hood: the Python SDK builds a `JobRequest`, attaches hardware/noise profiles, and submits to `JobRunner`, which picks the appropriate backend (ideal, noisy Pauli, etc.) and returns `JobResult`.

Outcome: the user never sees the VM internals—just “device + job + measurements.” The SDK handles instruction lowering, profile selection, and result reporting.

---

## 2. Command-Line Journey (`quera-vm`)

Goal: provide a shortcut for developers to run kernels without writing Python glue.

Example command:
```
quera-vm run \
  --device quera.na_vm.sim \
  --profile dev_debug \
  --shots 1_000 \
  examples/ghz.py
```

What happens:
1. The CLI loads `examples/ghz.py`, uses Bloqade/Kirin to lower the kernel to the VM dialect, and builds a program.
2. It sends a `JobRequest` to a local VM daemon (or an in-process runner) with the requested device/profile.
3. Outputs measurements/pulse logs/diagnostics to the terminal and may write structured JSON to disk.

Target personas: curious researchers, tutorial readers, and automated scripts that want a quick “run and inspect” tool without dealing with the Python SDK directly.

---

## 3. Service/Ops Journey

Goal: treat the VM as a full service—more like QEMU, less like a library.

Deployment:
```
quera-vm serve --config vm-service.toml
```

Client interaction (gRPC/REST):
1. `JobRequest` includes:
   - Device ID (e.g., `quera-na-vm-noisy-cpu`).
   - Profile metadata (geometry, noise model, supported gates and connectivity).
   - ISA version and **hardware constraints**:
     - Gate durations and scheduling/parallelism rules.
     - Native gate set and connectivity graph.
     - Microarchitectural limits (cooldown times, maximum concurrent operations, etc.).
   - Instruction program (same schema as the Python SDK uses).
2. Server enqueues the job, dispatches it to the selected backend (ideal, noisy Pauli, full physics), and streams measurement batches / pulse logs.
3. Clients poll or subscribe to job status and receive rich diagnostics and error codes.

This mode supports CI, staging, and production setups where multiple users submit jobs to shared hardware (virtual or real).

---

## 4. Key UX Goals Moving Forward

1. **Device abstraction**
   - Named devices (`quera.na_vm.sim`, `quera.na_vm.noisy`, etc.) with profiles for geometry, capabilities, and noise.
   - Device IDs exposed through the SDK/CLI so users know what they are hitting.

2. **Compiler visibility**
   - Compile-to-VM passes should produce programs that are easy to inspect/debug (JSON dumps, alignment with a DSL).
   - Provide diagnostics (e.g., “gate CX on qubits 0/1 exceeded blockade radius”) upstream to Kirin/Bloqade.

3. **Service contract**
   - Job statuses, structured errors, logs, and streaming measurements make the VM service feel like hardware rather than a library.
   - Capabilities and constraint reporting let clients discover:
     - Supported gates and connectivity.
     - Timing/duration ranges and allowed parallelism.
     - Noise models and resource limits for each device/profile.

4. **Multiple backends**
   - Behind the machin: ideal statevector, noisy Pauli, future Rydberg physics, or even real chip connectors.
   - The user always speaks the same ISA and API; switching engines is a profile/device selection.

5. **Transparency**
   - Document the VM dialect, noise behavior, and device profiles clearly so advanced users can reason about the hardware abstraction.
   - Provide monitoring/telemetry (job latency, noise config, pulse logs) for ops teams.

These UX goals will guide the next iterations: adding a Python SDK/CLI around the current `submit_job`, exposing device profiles, and enriching the service API so the VM feels like a true virtual device.
