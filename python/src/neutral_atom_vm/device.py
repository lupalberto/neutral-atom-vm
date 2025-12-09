from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable, Dict, Mapping, Optional, Sequence, Tuple, Union

from copy import deepcopy

from .layouts import GridLayout, grid_layout_for_profile
from .job import (
    ConnectivityKind,
    HardwareConfig,
    JobRequest,
    NativeGate,
    SimpleNoiseConfig,
    TimingLimits,
    has_oneapi_backend,
    has_stabilizer_backend,
    JobResult,
    submit_job,
)
from .service_client import RemoteServiceError, fetch_remote_device_catalog, submit_job_to_service
from .squin_lowering import to_vm_program

_SINGLE_QUBIT_DURATION_NS = 500.0
_TWO_QUBIT_DURATION_NS = 1000.0
_MEASUREMENT_DURATION_NS = 50_000.0


def _timing_limits_config() -> Dict[str, float]:
    return {
        "min_wait_ns": 0.0,
        "max_wait_ns": 0.0,
        "max_parallel_single_qubit": 0,
        "max_parallel_two_qubit": 0,
        "max_parallel_per_zone": 0,
        "measurement_cooldown_ns": _MEASUREMENT_DURATION_NS,
        "measurement_duration_ns": _MEASUREMENT_DURATION_NS,
    }


def _native_gate_catalog(connectivity: str) -> list[Dict[str, Any]]:
    return [
        {
            "name": "X",
            "arity": 1,
            "duration_ns": _SINGLE_QUBIT_DURATION_NS,
            "angle_min": 0.0,
            "angle_max": 0.0,
            "connectivity": "AllToAll",
        },
        {
            "name": "H",
            "arity": 1,
            "duration_ns": _SINGLE_QUBIT_DURATION_NS,
            "angle_min": 0.0,
            "angle_max": 0.0,
            "connectivity": "AllToAll",
        },
        {
            "name": "Z",
            "arity": 1,
            "duration_ns": _SINGLE_QUBIT_DURATION_NS,
            "angle_min": 0.0,
            "angle_max": 0.0,
            "connectivity": "AllToAll",
        },
        {
            "name": "CX",
            "arity": 2,
            "duration_ns": _TWO_QUBIT_DURATION_NS,
            "angle_min": 0.0,
            "angle_max": 0.0,
            "connectivity": connectivity,
        },
    ]


def _sanitize_noise_for_stabilizer(payload: Mapping[str, Any] | None) -> Dict[str, Any] | None:
    if not isinstance(payload, Mapping):
        return None
    sanitized: Dict[str, Any] = {}
    for key in ("p_loss", "p_quantum_flip"):
        if key in payload:
            sanitized[key] = payload[key]
    readout = payload.get("readout")
    if isinstance(readout, Mapping):
        sanitized["readout"] = deepcopy(readout)
    gate_payload = payload.get("gate")
    if isinstance(gate_payload, Mapping):
        gate_clean: Dict[str, Any] = {}
        for field in ("single_qubit", "two_qubit_control", "two_qubit_target"):
            entry = gate_payload.get(field)
            if isinstance(entry, Mapping):
                gate_clean[field] = deepcopy(entry)
        if gate_clean:
            sanitized["gate"] = gate_clean
    return sanitized or None


def _stabilizer_config_from(config: Mapping[str, Any]) -> Dict[str, Any]:
    sanitized = deepcopy(config)
    noise_payload = sanitized.get("noise")
    clean_noise = _sanitize_noise_for_stabilizer(noise_payload if isinstance(noise_payload, Mapping) else None)
    if clean_noise:
        sanitized["noise"] = clean_noise
    else:
        sanitized.pop("noise", None)
    return sanitized


def _parse_timing_limits(payload: Mapping[str, Any]) -> TimingLimits:
    return TimingLimits(
        min_wait_ns=float(payload.get("min_wait_ns", 0.0) or 0.0),
        max_wait_ns=float(payload.get("max_wait_ns", 0.0) or 0.0),
        max_parallel_single_qubit=int(payload.get("max_parallel_single_qubit", 0) or 0),
        max_parallel_two_qubit=int(payload.get("max_parallel_two_qubit", 0) or 0),
        max_parallel_per_zone=int(payload.get("max_parallel_per_zone", 0) or 0),
        measurement_cooldown_ns=float(payload.get("measurement_cooldown_ns", 0.0) or 0.0),
        measurement_duration_ns=float(payload.get("measurement_duration_ns", 0.0) or 0.0),
    )


def _parse_native_gates(payload: Sequence[Mapping[str, Any]]) -> list[NativeGate]:
    gates: list[NativeGate] = []
    for entry in payload:
        name = str(entry.get("name", ""))
        if not name:
            continue
        try:
            connectivity = ConnectivityKind.from_str(
                str(entry.get("connectivity", ConnectivityKind.AllToAll.value))
            )
        except ValueError:
            connectivity = ConnectivityKind.AllToAll
        gate = NativeGate(
            name=name,
            arity=int(entry.get("arity", 1) or 1),
            duration_ns=float(entry.get("duration_ns", 0.0) or 0.0),
            angle_min=float(entry.get("angle_min", 0.0) or 0.0),
            angle_max=float(entry.get("angle_max", 0.0) or 0.0),
            connectivity=connectivity,
        )
        gates.append(gate)
    return gates

ProgramType = Sequence[Dict[str, Any]]
KernelType = Callable[..., Any]
SubmitJobFn = Callable[[JobRequest], Mapping[str, Any]]


class JobHandle:
    """Represents an in-flight job submitted to a VM device."""

    def __init__(
        self,
        payload: Dict[str, Any],
        *,
        device_id: str,
        profile: Optional[str],
        shots: int,
        layout: GridLayout | None = None,
        coordinates: Sequence[Sequence[float]] | None = None,
    ) -> None:
        self._payload = dict(payload)
        self.device_id = device_id
        self.profile = profile
        self.shots = shots
        self.layout = layout
        self.coordinates = coordinates
        self._result: JobResult | None = None

    def result(self) -> JobResult:
        """Wait for the job to finish (immediate for local runner)."""

        if self._result is None:
            self._result = JobResult(
                self._payload,
                device_id=self.device_id,
                profile=self.profile,
                shots=self.shots,
                layout=self.layout,
                coordinates=self.coordinates,
            )
        return self._result


@dataclass
class Device:
    """Simple device handle that submits programs to the native VM runner."""

    id: str
    profile: Optional[str]
    positions: Sequence[float]
    coordinates: Sequence[Sequence[float]] | None = None
    blockade_radius: float = 0.0
    noise: SimpleNoiseConfig | None = None
    grid_layout: GridLayout | None = None
    native_gates: Sequence[NativeGate] | None = None
    timing_limits: TimingLimits | None = None
    submit_job_fn: SubmitJobFn | None = None

    def submit(
        self,
        program_or_kernel: Union[ProgramType, KernelType],
        shots: int = 1,
        max_threads: Optional[int] = None,
    ) -> JobHandle:
        request = self.build_job_request(program_or_kernel, shots=shots, max_threads=max_threads)
        submit_fn = self.submit_job_fn or submit_job
        job_result = submit_fn(request)
        if not isinstance(job_result, dict):
            raise TypeError("submit_job returned a non-dict result")
        return JobHandle(
            job_result,
            device_id=self.id,
            profile=self.profile,
            shots=shots,
            layout=self.grid_layout,
            coordinates=self.coordinates,
        )

    def build_job_request(
        self,
        program_or_kernel: Union[ProgramType, KernelType],
        shots: int = 1,
        max_threads: Optional[int] = None,
    ) -> JobRequest:
        """Create a :class:`JobRequest` describing this device + kernel without executing it."""
        program = self._normalize_program(program_or_kernel)
        _validate_blockade_constraints(program, self.positions, self.blockade_radius, self.coordinates)
        return JobRequest(
            program=program,
            hardware=HardwareConfig(
                positions=list(self.positions),
                coordinates=self.coordinates,
                blockade_radius=self.blockade_radius,
                native_gates=self.native_gates,
                timing_limits=self.timing_limits or TimingLimits(),
            ),
            device_id=self.id,
            profile=self.profile,
            shots=shots,
            max_threads=max_threads,
            noise=self.noise,
        )

    def _normalize_program(self, program_or_kernel: Union[ProgramType, KernelType]) -> ProgramType:
        if callable(program_or_kernel):
            return to_vm_program(program_or_kernel)
        return list(program_or_kernel)


def _validate_blockade_constraints(
    program: Sequence[Dict[str, Any]],
    positions: Sequence[float],
    blockade_radius: float,
    coordinates: Sequence[Sequence[float]] | None = None,
) -> None:
    if not positions or blockade_radius <= 0.0:
        return

    pos_list = list(positions)
    limit = len(pos_list)
    threshold = blockade_radius + 1e-12
    coord_list = None
    if coordinates:
        coord_list = [tuple(float(v) for v in entry) for entry in coordinates]

    for instruction in program:
        if instruction.get("op") != "ApplyGate":
            continue

        targets = instruction.get("targets") or []
        indices: list[int] = []
        for raw in targets:
            try:
                idx = int(raw)
            except (TypeError, ValueError):
                continue
            indices.append(idx)

        if len(indices) < 2:
            continue

        gate_name = instruction.get("name", "<unknown>")
        for idx in indices:
            if idx < 0 or idx >= limit:
                raise ValueError(
                    f"Gate {gate_name} references qubit {idx}, but device has positions 0..{limit - 1}"
                )

        for i in range(len(indices)):
            for j in range(i + 1, len(indices)):
                q0 = indices[i]
                q1 = indices[j]
                if _distance_between(pos_list, coord_list, q0, q1) > threshold:
                    raise ValueError(
                        f"Gate {gate_name} on qubits {q0}/{q1} violates blockade radius {blockade_radius:.3f}"
                    )


def _distance_between(
    positions: Sequence[float],
    coords: Sequence[Sequence[float]] | None,
    idx0: int,
    idx1: int,
) -> float:
    if coords and idx0 < len(coords) and idx1 < len(coords):
        vec0 = coords[idx0]
        vec1 = coords[idx1]
        dim = min(len(vec0), len(vec1))
        if dim:
            return float(sum((float(vec0[d]) - float(vec1[d])) ** 2 for d in range(dim)) ** 0.5)
    return abs(positions[idx0] - positions[idx1])


_PROFILE_METADATA: Dict[Tuple[str, Optional[str]], Dict[str, str]] = {
    (
        "local-cpu",
        "ideal_small_array",
    ): {
        "label": "Ideal tutorial array",
        "description": "Ten-site 1D array with no noise for quick SDK/CLI walkthroughs.",
        "geometry": "1D chain with unit spacing and blockade radius 1.5.",
        "noise_behavior": "All noise sources disabled (deterministic statevector).",
        "persona": "education",
    },
    (
        "local-cpu",
        "ideal_square_grid",
    ): {
        "label": "Ideal square grid",
        "description": "Four-by-four noiseless grid for experimenting with spatially-aware controls.",
        "geometry": "2D 4×4 layout with unit spacing and blockade radius 1.5.",
        "noise_behavior": "Noise-free (idealized) execution for tutorials and layout testing.",
        "persona": "geometry exploration",
    },
    (
        "local-cpu",
        "noisy_square_array",
    ): {
        "label": "Noisy square array",
        "description": "4x4 logical grid with moderate depolarizing and idle dephasing noise.",
        "geometry": "Conceptual 4x4 layout flattened to 16 slots with blockade radius 2.0.",
        "noise_behavior": "Gate depolarizing noise at the 1% level plus idle phase drift.",
        "persona": "algorithm prototyping",
    },
    (
        "local-cpu",
        "lossy_chain",
    ): {
        "label": "Loss-dominated chain",
        "description": "Six-qubit chain that injects heavy loss to exercise erasure-aware code.",
        "geometry": "1D chain with 1.5 spacing and shared blockade radius 1.5.",
        "noise_behavior": "Runtime loss channel with 10% upfront loss and idle losses per gate.",
        "persona": "loss-aware algorithms",
    },
    (
        "local-cpu",
        "lossy_block",
    ): {
        "label": "Lossy block array (2×4×2)",
        "description": "Sixteen-site block spread across a 2×4×2 prism for loss-heavy benchmarking.",
        "geometry": "2×4×2 block with 1.5 spacing along x, 1.0 along y/z, and blockade radius 1.5.",
        "noise_behavior": "Same heavy loss profile as lossy_chain: 10% upfront erasure plus per-gate/idle loss.",
        "persona": "loss-aware algorithms",
    },
    (
        "local-cpu",
        "benchmark_chain",
    ): {
        "label": "Benchmark chain (20 qubits)",
        "description": "Medium-size array for GHZ / volume tests with realistic depolarizing noise.",
        "geometry": "1D chain of 20 qubits at 1.3 spacing, blockade radius 1.6.",
        "noise_behavior": "Balanced single/two-qubit channels, correlated CZ errors, idle dephasing.",
        "persona": "integration + benchmarking",
    },
    (
        "local-cpu",
        "readout_stress",
    ): {
        "label": "Readout stress array",
        "description": "Eight-qubit chain emphasizing SPAM noise and mild runtime loss.",
        "geometry": "1D chain of 8 qubits with unit spacing and blockade radius 1.2.",
        "noise_behavior": "3% symmetric readout flips plus mild depolarizing and idle phase noise.",
        "persona": "diagnostics",
    },
}

has_oneapi = has_oneapi_backend()
has_stabilizer = has_stabilizer_backend()
for (device_id, profile), meta in list(_PROFILE_METADATA.items()):
    if device_id != "local-cpu":
        continue
    if not has_oneapi:
        continue
    arc_meta = dict(meta)
    arc_meta["label"] = f"{meta['label']} (Arc GPU)"
    arc_meta["description"] = (
        meta["description"] + " Executed on Intel Arc via the oneAPI backend."
    )
    _PROFILE_METADATA[("local-arc", profile)] = arc_meta

_PROFILE_TABLE: Dict[Tuple[str, Optional[str]], Dict[str, Any]] = {
    # UX-aligned ideal profile for quick tutorial runs.
    ("local-cpu", "ideal_small_array"): {
        "positions": [float(i) for i in range(10)],
        "blockade_radius": 1.5,
        "grid_layout": {
            "dim": 1,
            "rows": 1,
            "cols": 10,
            "layers": 1,
            "spacing": {"x": 1.0, "y": 1.0, "z": 1.0},
        },
        "noise": None,
        "timing_limits": _timing_limits_config(),
        "native_gates": _native_gate_catalog("NearestNeighborChain"),
    },
    # 2D noiseless grid for layout exploration.
    ("local-cpu", "ideal_square_grid"): {
        "positions": [float(i) for i in range(16)],
        "coordinates": [
            [float(x), float(y)]
            for y in range(4)
            for x in range(4)
        ],
        "blockade_radius": 1.5,
        "grid_layout": {
            "dim": 2,
            "rows": 4,
            "cols": 4,
            "layers": 1,
            "spacing": {"x": 1.0, "y": 1.0, "z": 1.0},
        },
        "noise": None,
        "timing_limits": _timing_limits_config(),
        "native_gates": _native_gate_catalog("NearestNeighborGrid"),
    },
    # Captures a 4x4 grid with moderate depolarizing noise and idle dephasing.
    ("local-cpu", "noisy_square_array"): {
        "positions": [
            0.0,
            1.0,
            2.0,
            3.0,
            0.0,
            1.0,
            2.0,
            3.0,
            0.0,
            1.0,
            2.0,
            3.0,
            0.0,
            1.0,
            2.0,
            3.0,
        ],
        "coordinates": [
            [0.0, 0.0],
            [1.0, 0.0],
            [2.0, 0.0],
            [3.0, 0.0],
            [0.0, 1.0],
            [1.0, 1.0],
            [2.0, 1.0],
            [3.0, 1.0],
            [0.0, 2.0],
            [1.0, 2.0],
            [2.0, 2.0],
            [3.0, 2.0],
            [0.0, 3.0],
            [1.0, 3.0],
            [2.0, 3.0],
            [3.0, 3.0],
        ],
        "blockade_radius": 2.0,
        "grid_layout": {
            "dim": 2,
            "rows": 4,
            "cols": 4,
            "layers": 1,
            "spacing": {"x": 1.0, "y": 1.0, "z": 1.0},
        },
        "noise": {
            "gate": {
                "single_qubit": {"px": 0.005, "py": 0.005, "pz": 0.005},
                "two_qubit_control": {"px": 0.01, "py": 0.01, "pz": 0.01},
                "two_qubit_target": {"px": 0.01, "py": 0.01, "pz": 0.01},
            },
            "idle_rate": 200.0,
            "phase": {"idle": 0.02},
        },
        "timing_limits": _timing_limits_config(),
        "native_gates": _native_gate_catalog("NearestNeighborGrid"),
    },
    # Heavy loss channel illustrating erasure-dominated behavior.
    ("local-cpu", "lossy_chain"): {
        "positions": [float(i) * 1.5 for i in range(6)],
        "blockade_radius": 1.5,
        "grid_layout": {
            "dim": 1,
            "rows": 1,
            "cols": 6,
            "layers": 1,
            "spacing": {"x": 1.5, "y": 1.0, "z": 1.0},
        },
        "noise": {
            "p_loss": 0.1,
            "loss_runtime": {"per_gate": 0.05, "idle_rate": 5.0},
        },
        "timing_limits": _timing_limits_config(),
        "native_gates": _native_gate_catalog("NearestNeighborChain"),
    },
    ("local-cpu", "lossy_block"): {
        "positions": [float(i) for i in range(16)],
        "coordinates": [
            [float(x) * 1.5, float(y), float(z)]
            for z in range(2)
            for y in range(2)
            for x in range(4)
        ],
        "blockade_radius": 1.5,
        "grid_layout": {
            "dim": 3,
            "rows": 2,
            "cols": 4,
            "layers": 2,
            "spacing": {"x": 1.5, "y": 1.0, "z": 1.0},
        },
        "noise": {
            "p_loss": 0.1,
            "loss_runtime": {"per_gate": 0.05, "idle_rate": 5.0},
        },
        "timing_limits": _timing_limits_config(),
        "native_gates": _native_gate_catalog("AllToAll"),
    },
    # 20-qubit benchmark chain for GHZ/volume experiments with moderate noise.
    ("local-cpu", "benchmark_chain"): {
        "positions": [float(i) * 1.3 for i in range(20)],
        "blockade_radius": 1.6,
        "grid_layout": {
            "dim": 1,
            "rows": 1,
            "cols": 20,
            "layers": 1,
            "spacing": {"x": 1.3, "y": 1.0, "z": 1.0},
        },
        "noise": {
            "gate": {
                "single_qubit": {"px": 0.002, "py": 0.002, "pz": 0.001},
                "two_qubit_control": {"px": 0.006, "py": 0.006, "pz": 0.004},
                "two_qubit_target": {"px": 0.006, "py": 0.006, "pz": 0.004},
            },
            "phase": {
                "single_qubit": 0.001,
                "two_qubit_control": 0.004,
                "two_qubit_target": 0.004,
                "idle": 0.012,
            },
            "amplitude_damping": {
                "per_gate": 0.0015,
                "idle_rate": 0.08,
            },
            "idle_rate": 120.0,
            "correlated_gate": {
                "matrix": [
                    [0.0, 0.0, 0.0, 0.0],
                    [0.0, 0.002, 0.0, 0.0],
                    [0.0, 0.0, 0.002, 0.0],
                    [0.0, 0.0, 0.0, 0.0005],
                ]
            },
        },
        "timing_limits": _timing_limits_config(),
        "native_gates": _native_gate_catalog("NearestNeighborChain"),
    },
    # SPAM-focused preset with notable readout flips and mild runtime loss.
    ("local-cpu", "readout_stress"): {
        "positions": [float(i) for i in range(8)],
        "blockade_radius": 1.2,
        "grid_layout": {
            "dim": 1,
            "rows": 1,
            "cols": 8,
            "layers": 1,
            "spacing": {"x": 1.0, "y": 1.0, "z": 1.0},
        },
        "noise": {
            "readout": {"p_flip0_to_1": 0.03, "p_flip1_to_0": 0.03},
            "gate": {
                "single_qubit": {"px": 0.003, "py": 0.003, "pz": 0.002},
                "two_qubit_control": {"px": 0.005, "py": 0.005, "pz": 0.005},
                "two_qubit_target": {"px": 0.005, "py": 0.005, "pz": 0.005},
            },
            "p_loss": 0.02,
            "phase": {"idle": 0.03},
            "loss_runtime": {"per_gate": 0.01, "idle_rate": 3.0},
        },
        "timing_limits": _timing_limits_config(),
        "native_gates": _native_gate_catalog("NearestNeighborChain"),
    },
}

if has_oneapi:
    for (device_id, profile), config in list(_PROFILE_TABLE.items()):
        if device_id != "local-cpu":
            continue
        _PROFILE_TABLE[("local-arc", profile)] = deepcopy(config)

if has_stabilizer:
    for (device_id, profile), config in list(_PROFILE_TABLE.items()):
        if device_id != "local-cpu":
            continue
        sanitized = _stabilizer_config_from(config)
        _PROFILE_TABLE[("stabilizer", profile)] = sanitized
        metadata = dict(_PROFILE_METADATA.get((device_id, profile), {}))
        label = metadata.get("label", profile)
        metadata["label"] = f"{label} (Stim)" if "Stim" not in label else label
        description = metadata.get("description", "")
        if "Stim stabilizer backend" not in description:
            extra = "Runs on the Stim stabilizer backend."
            description = f"{description} {extra}".strip() if description else extra
        metadata["description"] = description
        metadata["persona"] = metadata.get("persona", "stabilizer")
        _PROFILE_METADATA[("stabilizer", profile)] = metadata


def available_presets() -> Dict[str, Dict[Optional[str], Dict[str, Any]]]:
    """Return a structured view of the built-in device/profile presets."""

    presets: Dict[str, Dict[Optional[str], Dict[str, Any]]] = {}
    for key, config in _PROFILE_TABLE.items():
        device_id, profile = key
        entry: Dict[str, Any] = {
            "positions": list(config.get("positions", [])),
            "blockade_radius": float(config.get("blockade_radius", 0.0)),
        }
        if "noise" in config:
            entry["noise"] = deepcopy(config["noise"])
        if "grid_layout" in config:
            entry["grid_layout"] = deepcopy(config["grid_layout"])
        if "coordinates" in config:
            entry["coordinates"] = deepcopy(config["coordinates"])
        if "timing_limits" in config:
            entry["timing_limits"] = deepcopy(config["timing_limits"])
        if "native_gates" in config:
            entry["native_gates"] = deepcopy(config["native_gates"])
        metadata = _PROFILE_METADATA.get(key)
        if metadata:
            entry["metadata"] = dict(metadata)
        presets.setdefault(device_id, {})[profile] = entry
    return presets


def build_device_from_config(
    device_id: str,
    *,
    profile: Optional[str],
    config: Mapping[str, Any] | None = None,
    submit_job_fn: SubmitJobFn | None = None,
    service_url: str | None = None,
    devices_endpoint: str = "/devices",
    service_timeout: float = 10.0,
) -> Device:
    resolved_config = config
    if service_url:
        catalog = fetch_remote_device_catalog(
            service_url,
            devices_endpoint,
            timeout=service_timeout,
        )
        device_catalog = catalog.get(device_id)
        if not device_catalog:
            raise ValueError(f"Unknown device: {device_id!r}")
        if profile not in device_catalog:
            raise ValueError(f"Unknown profile {profile!r} for device {device_id!r}")
        if resolved_config is None:
            resolved_config = device_catalog[profile]
        if submit_job_fn is None:
            submit_job_fn = lambda req: submit_job_to_service(
                req,
                service_url,
                timeout=service_timeout,
            )
    if resolved_config is None or "positions" not in resolved_config:
        raise ValueError("Profile config must include 'positions'")
    if device_id == "stabilizer":
        if not has_stabilizer:
            raise ValueError("Stabilizer backend is unavailable in this build")
        resolved_config = _stabilizer_config_from(resolved_config)
    coordinates = resolved_config.get("coordinates")
    coord_entries = None
    if isinstance(coordinates, Sequence):
        coord_entries = []
        for entry in coordinates:
            if isinstance(entry, Sequence) and not isinstance(entry, (str, bytes)):
                coord_entries.append(tuple(float(value) for value in entry))
    positions = list(resolved_config["positions"])
    if coord_entries and len(positions) != len(coord_entries):
        positions = list(range(len(coord_entries)))
    blockade = float(resolved_config.get("blockade_radius", 0.0))
    noise_cfg = None
    noise_payload = resolved_config.get("noise")
    if isinstance(noise_payload, Mapping):
        noise_cfg = SimpleNoiseConfig.from_mapping(noise_payload)
    timing_limits = None
    timing_payload = resolved_config.get("timing_limits")
    if isinstance(timing_payload, Mapping):
        timing_limits = _parse_timing_limits(timing_payload)
    native_gates = None
    gates_payload = resolved_config.get("native_gates")
    if isinstance(gates_payload, Sequence) and not isinstance(gates_payload, (str, bytes)):
        mapping_entries = [entry for entry in gates_payload if isinstance(entry, Mapping)]
        if mapping_entries:
            parsed = _parse_native_gates(mapping_entries)
            if parsed:
                native_gates = parsed
    layout_payload = resolved_config.get("grid_layout")
    layout = (
        GridLayout.from_info(layout_payload)
        if isinstance(layout_payload, Mapping)
        else None
    )
    if layout is None:
        layout = grid_layout_for_profile(profile)
    return Device(
        id=device_id,
        profile=profile,
        positions=positions,
        coordinates=coord_entries,
        blockade_radius=blockade,
        noise=noise_cfg,
        grid_layout=layout,
        native_gates=native_gates,
        timing_limits=timing_limits,
        submit_job_fn=submit_job_fn,
    )


def connect_device(
    device_id: str,
    *,
    profile: Optional[str] = None,
    service_url: str | None = None,
    devices_endpoint: str = "/devices",
    service_timeout: float = 10.0,
) -> Device:
    """Return a handle that behaves like a virtual neutral atom device.

    The local C++ runtime powers the ``"local-cpu"`` preset and the
    ``"local-arc"`` alias, so callers simply request a device/profile pair and
    ``connect_device`` looks up the matching preset rather than requiring raw
    geometry.
    """
    if service_url:
        return build_device_from_config(
            device_id,
            profile=profile,
            service_url=service_url,
            devices_endpoint=devices_endpoint,
            service_timeout=service_timeout,
        )
    key = (device_id, profile)
    if key not in _PROFILE_TABLE:
        raise ValueError(f"Unknown device/profile combination: {device_id!r}, {profile!r}")
    cfg = _PROFILE_TABLE[key]
    return build_device_from_config(
        device_id,
        profile=profile,
        config=cfg,
    )
