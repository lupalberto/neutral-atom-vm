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

Goal: `connect_device("local-cpu", profile="ideal_small_array")` returns a device handle that behaves like actual hardware.

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
   dev = connect_device("local-cpu", profile="ideal_small_array")
   ```
4. Job submission:
   ```python
   job = dev.submit(ghz, shots=1000)
   result = job.result()
   print(result.measurements)
   ```
   The helper `neutral_atom_vm.connect_device` now exists and returns the `dev` handle with minimal configuration. Profiles are resolved internally (e.g., `"ideal_small_array"`), so callers do not configure positions directly. For backwards compatibility we also expose `device_id="local-cpu"` (and `local-arc`) to reach the same local C++ runtime with the legacy defaults.
5. Under the hood: the Python SDK builds a `JobRequest`, attaches hardware/noise profiles, and submits to `JobRunner`, which picks the appropriate backend (ideal, noisy Pauli, etc.) and returns `JobResult`.

Outcome: the user never sees the VM internals—just “device + job + measurements.” The SDK handles instruction lowering, profile selection, and result reporting.

### Timing-limit example

The `benchmark_chain` profile mentioned above sets `timing_limits.measurement_cooldown_ns = 5.0`. The VM enforces that by remembering when each site was last measured and rejecting the next `ApplyGate` on that site until at least 5.0 logical nanoseconds have elapsed. Because the current lowering never emits any `Wait` instructions, a kernel that measures and then immediately reuses the same qubit will trigger that validation error.

```python
from bloqade import squin

@squin.kernel
def reuse_measured_qubit():
    q = squin.qalloc(3)
    squin.h(q[0])
    squin.measure(q[0])
    squin.h(q[0])  # fails the benchmark_chain cooldown without an intervening Wait
```

Running this via `connect_device("local-cpu", profile="benchmark_chain")` will raise `runtime_error: Gate violates measurement cooldown on qubit 0`. The fix is to insert a `Wait` (or otherwise let the lowered program advance `logical_time`) before applying another gate to that site. This demonstrates how the ISA/profile contract surfaces hardware constraints, and why compilers/users must respect timing limits or the VM will reject the program before it reaches the engine.
By adding `squin.wait(5.0)` (or any duration ≥ 5.0) between the `measure` and the following gate, the lowered program gains a `<Wait duration=5.0>` instruction. That increases `logical_time`, satisfies the cooldown, and allows the next `ApplyGate` to execute. This mirrors real hardware: you must budget time after measurements before reusing the same tweezers.

---

## 2. Command-Line Journey (`quera-vm`)

Goal: provide a shortcut for developers to run kernels without writing Python glue.

Example command:
```
quera-vm run \
  --device local-cpu \
  --profile dev_debug \
  --shots 1_000 \
  examples/ghz.py
```

If you need to cap CPU usage or force a deterministic scheduling order, use `--threads N`
to limit VM worker threads per shot (0 falls back to the hardware concurrency default).

When running with `--output json`, the CLI now includes a top-level `logs` array
that lists each shot/timestamp/category/message generated during execution, letting
you see gate calls, waits, pulses, and measurements in order. Supply
`--log-file path/to/log.txt` to divert those log entries into a standard log file
with timestamped `INFO` lines via Python’s logging framework; the JSON printed to
stdout stays focused on measurements/status and omits the `logs` array entirely
when a log file is requested. The log file now expands the set of categories to
include `Noise` entries (gate/measurement noise events) and `TimingConstraint`
entries (measurement cooldown or wait/pulse violations), providing a chronological
audit trail of enforced hardware constraints plus the existing gate/measurement
events.

For ad-hoc experiments, `--profile-config path/to/profile.json` can override the
built-in profile definitions. The JSON file can specify geometry (`positions`,
`blockade_radius`) and the noise configuration that eventually becomes
`SimpleNoiseConfig`, so developers no longer have to pass raw `--noise` blobs.

### Built-in devices and profiles

`--device` selects a VM backend/geometry pairing (for now everything routes to
the local simulator), while `--profile` chooses the preset noise/parameter set
for that hardware definition. We retired the raw `--noise` flag so that callers
only pick from vetted presets or load a full profile JSON. Both the CLI and the
Python SDK share the same registry, which can be inspected via
`neutral_atom_vm.available_presets()` for discoverability.

| Device ID   | Profile             | Geometry summary                                         | Noise focus                                                     | Primary persona/use case        |
|-------------|---------------------|----------------------------------------------------------|-----------------------------------------------------------------|---------------------------------|
| `local-cpu` | `ideal_small_array` | 10-site 1D chain (unit spacing, blockade radius 1.5)      | Noise disabled - idealized tutorials                            | SDK & CLI onboarding            |
| `local-cpu` | `noisy_square_array`| Conceptual 4x4 grid flattened to 16 slots, blockade 2.0   | ~1% depolarizing gate noise + idle phase drift                  | Algorithm prototyping           |
| `local-cpu` | `lossy_chain`       | 6-site chain (1.5 spacing)                                | Heavy loss: upfront 10% + runtime per-gate/idle loss channels   | Loss-aware algorithm research   |
| `local-cpu` | `lossy_block`       | 16-site 2×4×2 block (1.5×1.0×1.0 spacing, blockade 1.5)   | Heavy loss: upfront 10% + runtime per-gate/idle loss channels   | Loss-aware algorithm research   |
| `local-cpu` | `benchmark_chain`   | 20-site chain (1.3 spacing, blockade 1.6)                 | Moderate depolarizing, idle phase drift, correlated CZ channel  | Integration & throughput tests  |
| `local-cpu` | `readout_stress`    | 8-site chain (unit spacing, blockade 1.2)                 | 3% symmetric readout flips + mild runtime loss                  | Diagnostics / SPAM sensitivity  |

To use one of these presets from the CLI:

```
quera-vm run --device local-cpu --profile benchmark_chain \
  --shots 1000 examples/ghz.py
```

The `local-arc` device ID mirrors `local-cpu`’s profiles but selects the Intel Arc GPU backend when the oneAPI runtime is enabled. The metadata returned by `neutral_atom_vm.available_presets()` includes labels/descriptions that help you differentiate the CPU vs. Arc personas.

When the VM is built with `cmake -DNA_VM_WITH_ONEAPI=ON ..` and a compatible Intel oneAPI runtime is available, `--device local-arc` (or `connect_device("local-arc", profile="benchmark_chain")`) executes on the GPU backend. If the backend is not enabled, the CLI prints an explanatory error mentioning `NA_VM_WITH_ONEAPI=ON` so you can rebuild with the toggle.

To keep the Arc run fast, the oneAPI backend now keeps the statevector resident on the SYCL device and only copies it back to the host when a measurement, noise hook, or SDK inspection needs the amplitudes. This avoids the old per-gate copy/wait handshake that made `local-arc` slower than `local-cpu` for gate-heavy workloads.

Measurements now accumulate outcome probabilities and collapse the state directly on the GPU, so only the small probability vector and the sampled bits are copied off-device. Gate/idle noise hooks still execute on the CPU, so they are skipped when you choose `local-arc`; if you need noise modelling, keep running on `local-cpu`.

If a request violates a hardware constraint (for example, a CX between tweezers outside the preset blockade), the CLI now prints the reason on stderr and exits with status 1 so you can fix the circuit before spending shots. Successful runs also echo any backend `message` (e.g., rich diagnostics or loss counts) in the summary.

From the SDK, the same preset is available via:

```python
from neutral_atom_vm import connect_device

dev = connect_device("local-cpu", profile="benchmark_chain")
```

If you need a bespoke combination (e.g., larger footprint with custom noise),
start from a JSON template returned by `available_presets()` and tweak it, then
pass it to the CLI's `--profile-config` or to
`neutral_atom_vm.build_device_from_config` in Python.

### Notebook profile configurator

To help tutorial authors and solutions engineers iterate on profile settings
without digging through JSON, we now expose
`neutral_atom_vm.widgets.ProfileConfigurator`. It renders an ipywidgets-based UI
directly in Jupyter/VS Code, combining:

- Device/profile dropdowns backed by `available_presets()`, including metadata
  callouts so users understand geometry, persona, and noise focus.
- A geometry tab where blockade radius and the atom positions array can be
  edited inline (comma or newline separated floats). Multi-dimensional layouts
  are supported by entering coordinates per line (e.g., `0 0`, `1 0`, ... for a
  2-D grid), and the configurator exports both the flattened positions and the
  full coordinate list so downstream tools preserve actual distances.
- A noise tab that groups the most common top-level parameters (SPAM, Pauli
  channels, phase, damping, runtime loss) plus a textarea for the correlated CZ
  matrix.

`configurator.profile_payload` returns a dictionary compatible with
`build_device_from_config`/`--profile-config`, so a notebook can capture the
selected profile and feed it into `connect_device` or the CLI. This keeps the
“discover → tweak → run” flow in one place and mirrors the CLI/SDK UX described
above.

Pair it with `neutral_atom_vm.widgets.JobResultViewer` to summarize the job
output inline instead of dumping JSON blobs. Call
`viewer.load_result(result, device=device_id, profile=profile_name, shots=shots)`
right after `job.result()` to render status, elapsed time, and a paginated probability histogram.
When the selected profile maps to a known grid (`noisy_square_array`, etc.),
the viewer and the default HTML repr now render compact grid previews ahead of
the histogram so tutorials can surface spatial patterns and relative weights
without wading through huge JSON dumps.

The job result also keeps the layout metadata and coordinates used for the
device, so you can rebuild a Matplotlib visualization using the display helper
instead of touching raw JSON. Pass the job result to the single helper along
with the shot index:

```
from IPython.display import display
from neutral_atom_vm.display import display_shot

fig, ax = display_shot(
    result,
    shot_index=0,
    figsize=(4, 3),
)
display(fig)
```

When `interactive=True` is passed, `display_shot` returns a Plotly `Figure` rather than a `(fig, ax)` tuple, so use `fig = display_shot(..., interactive=True)` followed by `fig.show()` to activate the interactive controller (Plotly must be installed).

The helper automatically reads `result.layout`/`result.coordinates` (or
falls back to `grid_layout_for_profile(result.profile)`) and respects the
per-axis spacing stored in `layout.spacing`. Keeping the same
rows/cols/layers/spacing before submission keeps the VM layout, the notebook
visualization, and any saved previews deterministic across reloads.

Need something brand-new? The configurator now includes a “Create new profile”
option:

- Enter a name, choose the dimension (1D/2D/3D), configure rows/columns/layers and the per-axis spacing,
  and hit “Populate positions” to seed the geometry (you can still fine‑tune
  the coordinates manually afterward).
- Set the blockade radius and tweak the noise controls just like you would for
  a preset. The resulting `profile_payload` carries your custom name so you can
  pass it straight into `build_device_from_config` or `--profile-config`.

Notebook ergonomics now also improve by default: `JobHandle.result()` returns a
dict-like object with an HTML representation, so simply evaluating `result` in a
cell renders the same summary/counts block that the CLI prints—no need for
`print(json.dumps(...))` unless you explicitly want raw JSON.

You can also point `ProfileConfigurator` at a running service so its dropdowns
track the catalog exposed by that host. Provide `service_url="http://localhost:8080"`
(and `devices_endpoint="/devices"` if you configured a different path) and the
widget fetches `GET /devices` before rendering, keeping notebooks aligned with
the service without duplicating the preset list.

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
   - Device ID (e.g., `local-cpu`).
   - Profile metadata (geometry, noise model, supported gates and connectivity).
   - ISA version and **hardware constraints**:
     - Gate durations and scheduling/parallelism rules.
     - Native gate set and connectivity graph.
     - Microarchitectural limits (cooldown times, maximum concurrent operations, etc.).
 - Instruction program (same schema as the Python SDK uses).
2. `GET /devices` (or the configured devices endpoint) mirrors `neutral_atom_vm.available_presets()` so clients know every supported device/profile combination before submitting jobs.
3. Server enqueues the job, dispatches it to the selected backend (ideal, noisy Pauli, full physics), and streams measurement batches / pulse logs.
4. Clients poll or subscribe to job status and receive rich diagnostics and error codes.

This mode supports CI, staging, and production setups where multiple users submit jobs to shared hardware (virtual or real).

---

## 4. Key UX Goals Moving Forward

1. **Device abstraction**
   - Named devices (`local-cpu`, `local-arc`, etc.) with profiles for geometry, capabilities, and noise.
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
