from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, Mapping, MutableMapping, Optional, Sequence, Tuple
from enum import Enum

import glob
import importlib
import importlib.util
import os
import sys

from .display import render_job_result_html
from .layouts import grid_layout_for_profile


Program = Sequence[Dict[str, Any]]


def _to_float(value: Any, default: float = 0.0) -> float:
    if value is None:
        return default
    return float(value)


def _normalize_mapping(mapping: Mapping[str, Any]) -> Dict[str, Any]:
    return dict(mapping)


_native_module = None


def _load_native_module():
    global _native_module
    if _native_module is not None:
        return _native_module

    package_dir = os.path.dirname(__file__)
    pattern = os.path.join(package_dir, "_neutral_atom_vm*.so")
    candidates = glob.glob(pattern)

    if candidates:
        module_path = candidates[0]
        spec = importlib.util.spec_from_file_location(
            "neutral_atom_vm._neutral_atom_vm", module_path
        )
        if spec is None or spec.loader is None:  # pragma: no cover
            raise ImportError(
                f"Unable to load neutral_atom_vm extension from {module_path!r}"
            )
        module = importlib.util.module_from_spec(spec)
        sys.modules["neutral_atom_vm._neutral_atom_vm"] = module
        spec.loader.exec_module(module)
        _native_module = module
        return module

    try:
        from ._neutral_atom_vm import submit_job as dummy  # noqa: F401 - ensure module can be imported
    except ImportError as exc:
        raise ImportError(
            "The compiled neutral_atom_vm bindings are missing. "
            "Build the C++ extension via CMake or install the package "
            "with a wheel that includes '_neutral_atom_vm'."
        ) from exc
    module = importlib.import_module("neutral_atom_vm._neutral_atom_vm")
    _native_module = module
    return module


def has_oneapi_backend() -> bool:
    module = _load_native_module()
    func = getattr(module, "has_oneapi_backend", None)
    if func is None:
        return False
    return bool(func())


def has_stabilizer_backend() -> bool:
    module = _load_native_module()
    func = getattr(module, "has_stabilizer_backend", None)
    if func is None:
        return False
    return bool(func())


def _prepare_job_dict(job: JobRequest | Mapping[str, Any]):
    job_dict = _normalize_job_mapping(job)
    hardware = job_dict.get("hardware")
    if isinstance(hardware, HardwareConfig):
        job_dict["hardware"] = hardware.to_dict()
        hardware = job_dict["hardware"]
    if hardware is None:
        hardware = {}
    return job_dict, hardware


@dataclass(frozen=True)
class MeasurementNoiseConfig:
    """Classical readout-noise probabilities for measurements."""

    p_flip0_to_1: float = 0.0
    p_flip1_to_0: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return {
            "p_flip0_to_1": self.p_flip0_to_1,
            "p_flip1_to_0": self.p_flip1_to_0,
        }

    @classmethod
    def from_mapping(cls, mapping: Mapping[str, Any]) -> "MeasurementNoiseConfig":
        return cls(
            p_flip0_to_1=_to_float(mapping.get("p_flip0_to_1")),
            p_flip1_to_0=_to_float(mapping.get("p_flip1_to_0")),
        )


@dataclass(frozen=True)
class SingleQubitPauliConfig:
    px: float = 0.0
    py: float = 0.0
    pz: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return {"px": self.px, "py": self.py, "pz": self.pz}

    @classmethod
    def from_mapping(cls, mapping: Mapping[str, Any]) -> "SingleQubitPauliConfig":
        return cls(
            px=_to_float(mapping.get("px")),
            py=_to_float(mapping.get("py")),
            pz=_to_float(mapping.get("pz")),
        )


@dataclass(frozen=True)
class GateNoiseConfig:
    single_qubit: SingleQubitPauliConfig = field(default_factory=SingleQubitPauliConfig)
    two_qubit_control: SingleQubitPauliConfig = field(
        default_factory=SingleQubitPauliConfig
    )
    two_qubit_target: SingleQubitPauliConfig = field(
        default_factory=SingleQubitPauliConfig
    )

    def to_dict(self) -> Dict[str, Any]:
        return {
            "single_qubit": self.single_qubit.to_dict(),
            "two_qubit_control": self.two_qubit_control.to_dict(),
            "two_qubit_target": self.two_qubit_target.to_dict(),
        }

    @classmethod
    def from_mapping(cls, mapping: Mapping[str, Any]) -> "GateNoiseConfig":
        single = mapping.get("single_qubit")
        two_control = mapping.get("two_qubit_control")
        two_target = mapping.get("two_qubit_target")
        return cls(
            single_qubit=SingleQubitPauliConfig.from_mapping(
                _normalize_mapping(single) if isinstance(single, Mapping) else {}
            ),
            two_qubit_control=SingleQubitPauliConfig.from_mapping(
                _normalize_mapping(two_control) if isinstance(two_control, Mapping) else {}
            ),
            two_qubit_target=SingleQubitPauliConfig.from_mapping(
                _normalize_mapping(two_target) if isinstance(two_target, Mapping) else {}
            ),
        )


@dataclass(frozen=True)
class TwoQubitCorrelatedPauliConfig:
    matrix: tuple[tuple[float, float, float, float], ...] = (
        (0.0, 0.0, 0.0, 0.0),
        (0.0, 0.0, 0.0, 0.0),
        (0.0, 0.0, 0.0, 0.0),
        (0.0, 0.0, 0.0, 0.0),
    )

    def to_dict(self) -> Dict[str, Any]:
        return {"matrix": [list(row) for row in self.matrix]}

    @classmethod
    def from_mapping(cls, mapping: Mapping[str, Any]) -> "TwoQubitCorrelatedPauliConfig":
        matrix = mapping.get("matrix")
        if isinstance(matrix, Sequence):
            rows: list[tuple[float, float, float, float]] = []
            for row in matrix:
                if not isinstance(row, Sequence):
                    rows.append((0.0, 0.0, 0.0, 0.0))
                    continue
                raw = list(row)
                values = tuple(
                    _to_float(raw[i]) if i < len(raw) else 0.0 for i in range(4)
                )
                rows.append(values)
            while len(rows) < 4:
                rows.append((0.0, 0.0, 0.0, 0.0))
            matrix_tuple = tuple(rows[:4])
        else:
            matrix_tuple = (
                (0.0, 0.0, 0.0, 0.0),
                (0.0, 0.0, 0.0, 0.0),
                (0.0, 0.0, 0.0, 0.0),
                (0.0, 0.0, 0.0, 0.0),
            )
        return cls(matrix=matrix_tuple)


@dataclass(frozen=True)
class LossRuntimeConfig:
    per_gate: float = 0.0
    idle_rate: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return {
            "per_gate": self.per_gate,
            "idle_rate": self.idle_rate,
        }

    @classmethod
    def from_mapping(cls, mapping: Mapping[str, Any]) -> "LossRuntimeConfig":
        return cls(
            per_gate=_to_float(mapping.get("per_gate")),
            idle_rate=_to_float(mapping.get("idle_rate")),
        )


@dataclass(frozen=True)
class PhaseNoiseConfig:
    single_qubit: float = 0.0
    two_qubit_control: float = 0.0
    two_qubit_target: float = 0.0
    idle: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return {
            "single_qubit": self.single_qubit,
            "two_qubit_control": self.two_qubit_control,
            "two_qubit_target": self.two_qubit_target,
            "idle": self.idle,
        }

    @classmethod
    def from_mapping(cls, mapping: Mapping[str, Any]) -> "PhaseNoiseConfig":
        return cls(
            single_qubit=_to_float(mapping.get("single_qubit")),
            two_qubit_control=_to_float(mapping.get("two_qubit_control")),
            two_qubit_target=_to_float(mapping.get("two_qubit_target")),
            idle=_to_float(mapping.get("idle")),
        )


@dataclass(frozen=True)
class AmplitudeDampingConfig:
    per_gate: float = 0.0
    idle_rate: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return {
            "per_gate": self.per_gate,
            "idle_rate": self.idle_rate,
        }

    @classmethod
    def from_mapping(cls, mapping: Mapping[str, Any]) -> "AmplitudeDampingConfig":
        return cls(
            per_gate=_to_float(mapping.get("per_gate")),
            idle_rate=_to_float(mapping.get("idle_rate")),
        )


@dataclass(frozen=True)
class SimpleNoiseConfig:
    p_quantum_flip: float = 0.0
    p_loss: float = 0.0
    readout: MeasurementNoiseConfig = field(default_factory=MeasurementNoiseConfig)
    gate: GateNoiseConfig = field(default_factory=GateNoiseConfig)
    correlated_gate: TwoQubitCorrelatedPauliConfig = field(
        default_factory=TwoQubitCorrelatedPauliConfig
    )
    idle_rate: float = 0.0
    phase: PhaseNoiseConfig = field(default_factory=PhaseNoiseConfig)
    amplitude_damping: AmplitudeDampingConfig = field(default_factory=AmplitudeDampingConfig)
    loss_runtime: LossRuntimeConfig = field(default_factory=LossRuntimeConfig)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "p_quantum_flip": self.p_quantum_flip,
            "p_loss": self.p_loss,
            "readout": self.readout.to_dict(),
            "gate": self.gate.to_dict(),
            "correlated_gate": self.correlated_gate.to_dict(),
            "idle_rate": self.idle_rate,
            "phase": self.phase.to_dict(),
            "amplitude_damping": self.amplitude_damping.to_dict(),
            "loss_runtime": self.loss_runtime.to_dict(),
        }

    @classmethod
    def from_mapping(cls, mapping: Mapping[str, Any]) -> "SimpleNoiseConfig":
        readout = mapping.get("readout")
        gate = mapping.get("gate")
        correlated = mapping.get("correlated_gate")
        loss_runtime = mapping.get("loss_runtime")
        return cls(
            p_quantum_flip=_to_float(mapping.get("p_quantum_flip")),
            p_loss=_to_float(mapping.get("p_loss")),
            readout=MeasurementNoiseConfig.from_mapping(
                _normalize_mapping(readout) if isinstance(readout, Mapping) else {}
            ),
            gate=GateNoiseConfig.from_mapping(
                _normalize_mapping(gate) if isinstance(gate, Mapping) else {}
            ),
            correlated_gate=TwoQubitCorrelatedPauliConfig.from_mapping(
                _normalize_mapping(correlated) if isinstance(correlated, Mapping) else {}
            ),
            idle_rate=_to_float(mapping.get("idle_rate")),
            phase=PhaseNoiseConfig.from_mapping(
                _normalize_mapping(mapping.get("phase"))
                if isinstance(mapping.get("phase"), Mapping)
                else {}
            ),
            amplitude_damping=AmplitudeDampingConfig.from_mapping(
                _normalize_mapping(mapping.get("amplitude_damping"))
                if isinstance(mapping.get("amplitude_damping"), Mapping)
                else {}
            ),
            loss_runtime=LossRuntimeConfig.from_mapping(
                _normalize_mapping(loss_runtime) if isinstance(loss_runtime, Mapping) else {}
            ),
        )


class ConnectivityKind(Enum):
    AllToAll = "AllToAll"
    NearestNeighborChain = "NearestNeighborChain"
    NearestNeighborGrid = "NearestNeighborGrid"

    @classmethod
    def from_str(cls, value: str) -> "ConnectivityKind":
        try:
            return cls(value)
        except ValueError as exc:
            raise ValueError(f"Unknown connectivity kind: {value}") from exc


@dataclass
class SiteDescriptor:
    id: int = 0
    x: float = 0.0
    y: float = 0.0
    zone_id: int = 0

    def to_dict(self) -> Dict[str, Any]:
        return {"id": self.id, "x": self.x, "y": self.y, "zone_id": self.zone_id}


@dataclass
class NativeGate:
    name: str
    arity: int = 1
    duration_ns: float = 0.0
    angle_min: float = 0.0
    angle_max: float = 0.0
    connectivity: ConnectivityKind = ConnectivityKind.AllToAll

    def to_dict(self) -> Dict[str, Any]:
        return {
            "name": self.name,
            "arity": self.arity,
            "duration_ns": self.duration_ns,
            "angle_min": self.angle_min,
            "angle_max": self.angle_max,
            "connectivity": self.connectivity.value,
        }


@dataclass
class TimingLimits:
    min_wait_ns: float = 0.0
    max_wait_ns: float = 0.0
    max_parallel_single_qubit: int = 0
    max_parallel_two_qubit: int = 0
    max_parallel_per_zone: int = 0
    measurement_cooldown_ns: float = 0.0
    measurement_duration_ns: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return {
            "min_wait_ns": self.min_wait_ns,
            "max_wait_ns": self.max_wait_ns,
            "max_parallel_single_qubit": self.max_parallel_single_qubit,
            "max_parallel_two_qubit": self.max_parallel_two_qubit,
            "max_parallel_per_zone": self.max_parallel_per_zone,
            "measurement_cooldown_ns": self.measurement_cooldown_ns,
            "measurement_duration_ns": self.measurement_duration_ns,
        }


@dataclass
class PulseLimits:
    detuning_min: float = 0.0
    detuning_max: float = 0.0
    duration_min_ns: float = 0.0
    duration_max_ns: float = 0.0
    max_overlapping_pulses: int = 0

    def to_dict(self) -> Dict[str, Any]:
        return {
            "detuning_min": self.detuning_min,
            "detuning_max": self.detuning_max,
            "duration_min_ns": self.duration_min_ns,
            "duration_max_ns": self.duration_max_ns,
            "max_overlapping_pulses": self.max_overlapping_pulses,
        }
 

@dataclass
class HardwareConfig:
    """Low-level hardware configuration for a VM job.

    This keeps geometry-related details in a dedicated value object so that
    higher-level code can compose JobRequests without mixing them into the
    public submit_job API surface.
    """

    positions: Sequence[float]
    blockade_radius: float = 0.0
    coordinates: Sequence[Sequence[float]] | None = None
    sites: Sequence[SiteDescriptor] | None = None
    native_gates: Sequence[NativeGate] | None = None
    timing_limits: TimingLimits = field(default_factory=TimingLimits)
    pulse_limits: PulseLimits = field(default_factory=PulseLimits)

    def to_dict(self) -> Dict[str, Any]:
        payload = {
            "positions": list(self.positions),
            "blockade_radius": float(self.blockade_radius),
        }
        if self.coordinates is not None:
            payload["coordinates"] = [list(coord) for coord in self.coordinates]
        if self.sites:
            payload["sites"] = [site.to_dict() for site in self.sites]
        if self.native_gates:
            payload["native_gates"] = [gate.to_dict() for gate in self.native_gates]
        payload["timing_limits"] = self.timing_limits.to_dict()
        payload["pulse_limits"] = self.pulse_limits.to_dict()
        return payload


@dataclass
class JobRequest:
    """High-level job request description for the Neutral Atom VM.

    This mirrors the C++ service::JobRequest structure while remaining a
    lightweight, composable value object on the Python side.
    """

    program: Program
    hardware: HardwareConfig
    device_id: str = "local-cpu"
    profile: str | None = None
    shots: int = 1
    max_threads: Optional[int] = None
    job_id: str = "python-client"
    metadata: Dict[str, str] = field(default_factory=dict)
    noise: SimpleNoiseConfig | None = None
    stim_circuit: str | None = None

    def to_dict(self) -> Dict[str, Any]:
        data: Dict[str, Any] = {
            "job_id": self.job_id,
            "device_id": self.device_id,
            "profile": self.profile,
            "program": list(self.program),
            "hardware": self.hardware.to_dict(),
            "shots": int(self.shots),
        }
        if self.max_threads is not None:
            data["max_threads"] = int(self.max_threads)
        if self.metadata:
            data["metadata"] = dict(self.metadata)
        if self.noise:
            data["noise"] = self.noise.to_dict()
        if self.stim_circuit:
            data["stim_circuit"] = self.stim_circuit
        return data


class JobResult(dict):
    """Dict-like container that renders nicely in notebooks."""

    def __init__(
        self,
        payload: Mapping[str, Any],
        *,
        device_id: str,
        profile: str | None,
        shots: int,
        layout: GridLayout | None = None,
        coordinates: Sequence[Sequence[float]] | None = None,
    ) -> None:
        super().__init__(payload)
        self.device_id = device_id
        self.profile = profile
        self.shots = shots
        self.layout = layout
        self.coordinates = coordinates
        self.timeline = list(payload.get("timeline", []))
        self.timeline_units = payload.get("timeline_units", "ns")
        self.log_time_units = payload.get("log_time_units", payload.get("time_units", "ns"))

    def _repr_html_(self) -> str:  # pragma: no cover - exercised in notebooks
        return render_job_result_html(
            result=self,
            device=self.device_id,
            profile=self.profile,
            shots=self.shots,
            layout=self.layout,
            coordinates=self.coordinates,
        )


def _normalize_job_mapping(job: JobRequest | Mapping[str, Any]) -> Dict[str, Any]:
    if isinstance(job, JobRequest):
        return job.to_dict()
    # Accept plain mappings/dicts for flexibility; copy into a mutable dict so
    # we can safely add defaults.
    normalized = dict(job)
    noise = normalized.get("noise")
    if isinstance(noise, SimpleNoiseConfig):
        normalized["noise"] = noise.to_dict()
    return normalized


def submit_job(job: JobRequest | Mapping[str, Any]) -> Dict[str, Any]:
    """Submit a JobRequest to the native VM runner.

    The public API accepts either a :class:`JobRequest` instance or a plain
    mapping with at least ``program`` and ``hardware`` keys. Geometry is kept
    inside the ``hardware`` sub-mapping to avoid mixing device/profile and
    low-level configuration in the function signature itself.
    """

    job_dict, hardware = _prepare_job_dict(job)

    module = _load_native_module()
    _native_submit_job = module.submit_job

    positions = list(hardware.get("positions", []))
    blockade_radius = float(hardware.get("blockade_radius", 0.0))
    shots = int(job_dict.get("shots", 1))

    try:
        result = _native_submit_job(job_dict)
    except TypeError:
        result = _native_submit_job(
            job_dict["program"],
            positions=positions,
            blockade_radius=blockade_radius,
            shots=shots,
        )

    if isinstance(result, MutableMapping) and "job_id" in job_dict:
        result["job_id"] = job_dict["job_id"]
    return dict(result) if isinstance(result, MutableMapping) else result


def submit_job_async(job: JobRequest | Mapping[str, Any]) -> Dict[str, Any]:
    job_dict, _ = _prepare_job_dict(job)
    module = _load_native_module()
    if not hasattr(module, "submit_job_async"):
        raise RemoteServiceError("Asynchronous submission is unavailable in this build")
    result = module.submit_job_async(job_dict)
    if not isinstance(result, Mapping):
        raise RemoteServiceError("Async submission returned an unexpected payload")
    return dict(result)


def job_status(job_id: str) -> Dict[str, Any]:
    module = _load_native_module()
    if not hasattr(module, "job_status"):
        raise RemoteServiceError("Job status queries are unavailable in this build")
    result = module.job_status(job_id)
    if not isinstance(result, Mapping):
        raise RemoteServiceError("Job status response is unexpected")
    return dict(result)


def job_result(job_id: str) -> Dict[str, Any]:
    module = _load_native_module()
    if not hasattr(module, "job_result"):
        raise RemoteServiceError("Job result queries are unavailable in this build")
    result = module.job_result(job_id)
    if not isinstance(result, Mapping):
        raise RemoteServiceError("Job result response is unexpected")
    return dict(result)
