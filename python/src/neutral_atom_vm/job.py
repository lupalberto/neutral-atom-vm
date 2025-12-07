from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, Mapping, MutableMapping, Optional, Sequence, Tuple


Program = Sequence[Dict[str, Any]]


def _to_float(value: Any, default: float = 0.0) -> float:
    if value is None:
        return default
    return float(value)


def _normalize_mapping(mapping: Mapping[str, Any]) -> Dict[str, Any]:
    return dict(mapping)


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
            loss_runtime=LossRuntimeConfig.from_mapping(
                _normalize_mapping(loss_runtime) if isinstance(loss_runtime, Mapping) else {}
            ),
        )


@dataclass(frozen=True)
class HardwareConfig:
    """Low-level hardware configuration for a VM job.

    This keeps geometry-related details in a dedicated value object so that
    higher-level code can compose JobRequests without mixing them into the
    public submit_job API surface.
    """

    positions: Sequence[float]
    blockade_radius: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return {
            "positions": list(self.positions),
            "blockade_radius": float(self.blockade_radius),
        }


@dataclass
class JobRequest:
    """High-level job request description for the Neutral Atom VM.

    This mirrors the C++ service::JobRequest structure while remaining a
    lightweight, composable value object on the Python side.
    """

    program: Program
    hardware: HardwareConfig
    device_id: str = "runtime"
    profile: str | None = None
    shots: int = 1
    max_threads: Optional[int] = None
    job_id: str = "python-client"
    metadata: Dict[str, str] = field(default_factory=dict)
    noise: SimpleNoiseConfig | None = None

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
        return data


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

    job_dict = _normalize_job_mapping(job)

    # Ensure hardware is represented as a plain mapping so that the native
    # binding can mirror service::JobRequest without knowing about the Python
    # dataclasses.
    hardware = job_dict.get("hardware")
    if isinstance(hardware, HardwareConfig):
        job_dict["hardware"] = hardware.to_dict()
        hardware = job_dict["hardware"]
    if hardware is None:
        hardware = {}

    try:
        from ._neutral_atom_vm import submit_job as _native_submit_job
    except ImportError as exc:  # pragma: no cover - exercised in integration tests
        raise ImportError(
            "The compiled neutral_atom_vm bindings are missing. "
            "Build the C++ extension via CMake or install the package "
            "with a wheel that includes '_neutral_atom_vm'."
        ) from exc
    # Prefer the new dict-shaped API if available, but fall back to the legacy
    # flat signature for existing wheels/extensions.
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
