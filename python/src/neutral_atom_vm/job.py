from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, Mapping, MutableMapping, Sequence


Program = Sequence[Dict[str, Any]]


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
    job_id: str = "python-client"
    metadata: Dict[str, str] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        data: Dict[str, Any] = {
            "job_id": self.job_id,
            "device_id": self.device_id,
            "profile": self.profile,
            "program": list(self.program),
            "hardware": self.hardware.to_dict(),
            "shots": int(self.shots),
        }
        if self.metadata:
            data["metadata"] = dict(self.metadata)
        return data


def _normalize_job_mapping(job: JobRequest | Mapping[str, Any]) -> Dict[str, Any]:
    if isinstance(job, JobRequest):
        return job.to_dict()
    # Accept plain mappings/dicts for flexibility; copy into a mutable dict so
    # we can safely add defaults.
    return dict(job)


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
