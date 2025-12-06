from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable, Dict, Mapping, Optional, Sequence, Tuple, Union

from .job import HardwareConfig, JobRequest, SimpleNoiseConfig, submit_job
from .squin_lowering import to_vm_program

ProgramType = Sequence[Dict[str, Any]]
KernelType = Callable[..., Any]


class JobHandle:
    """Represents an in-flight job submitted to a VM device."""

    def __init__(self, result: Dict[str, Any]) -> None:
        self._result = result

    def result(self) -> Dict[str, Any]:
        """Wait for the job to finish (immediate for local runner)."""
        return self._result


@dataclass
class Device:
    """Simple device handle that submits programs to the native VM runner."""

    id: str
    profile: Optional[str]
    positions: Sequence[float]
    blockade_radius: float = 0.0
    noise: SimpleNoiseConfig | None = None

    def submit(
        self,
        program_or_kernel: Union[ProgramType, KernelType],
        shots: int = 1,
    ) -> JobHandle:
        if callable(program_or_kernel):
            program = to_vm_program(program_or_kernel)
        else:
            program = list(program_or_kernel)

        request = JobRequest(
            program=program,
            hardware=HardwareConfig(
                positions=list(self.positions),
                blockade_radius=self.blockade_radius,
            ),
            device_id=self.id,
            profile=self.profile,
            shots=shots,
            noise=self.noise,
        )
        job_result = submit_job(request)
        return JobHandle(job_result)


_PROFILE_TABLE: Dict[Tuple[str, Optional[str]], Dict[str, Any]] = {
    # Legacy local runtime path: small two-qubit line.
    ("runtime", None): {
        "positions": [0.0, 1.0],
        "blockade_radius": 1.0,
        "noise": None,
    },
    # UX-aligned device name; currently backed by the same runtime.
    ("quera.na_vm.sim", "ideal_small_array"): {
        "positions": [0.0, 1.0],
        "blockade_radius": 1.0,
        "noise": None,
    },
}


def build_device_from_config(
    device_id: str,
    *,
    profile: Optional[str],
    config: Mapping[str, Any],
) -> Device:
    if "positions" not in config:
        raise ValueError("Profile config must include 'positions'")
    positions = list(config["positions"])
    blockade = float(config.get("blockade_radius", 0.0))
    noise_cfg = None
    noise_payload = config.get("noise")
    if isinstance(noise_payload, Mapping):
        noise_cfg = SimpleNoiseConfig.from_mapping(noise_payload)
    return Device(
        id=device_id,
        profile=profile,
        positions=positions,
        blockade_radius=blockade,
        noise=noise_cfg,
    )


def connect_device(
    device_id: str,
    *,
    profile: Optional[str] = None,
) -> Device:
    """Return a handle that behaves like a virtual neutral atom device.

    For now, both the legacy ``"runtime"`` device and the UX-aligned
    ``"quera.na_vm.sim"`` / ``profile="ideal_small_array"`` are backed by the
    same local C++ runtime. Profiles are resolved via an internal table so that
    callers do not pass positions directly.
    """
    key = (device_id, profile)
    if key not in _PROFILE_TABLE:
        raise ValueError(f"Unknown device/profile combination: {device_id!r}, {profile!r}")
    cfg = _PROFILE_TABLE[key]
    return build_device_from_config(device_id, profile=profile, config=cfg)
