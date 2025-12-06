from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable, Dict, Mapping, Optional, Sequence, Tuple, Union

from copy import deepcopy

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

        _validate_blockade_constraints(program, self.positions, self.blockade_radius)

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


def _validate_blockade_constraints(
    program: Sequence[Dict[str, Any]],
    positions: Sequence[float],
    blockade_radius: float,
) -> None:
    if not positions or blockade_radius <= 0.0:
        return

    pos_list = list(positions)
    limit = len(pos_list)
    threshold = blockade_radius + 1e-12

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
                if abs(pos_list[q0] - pos_list[q1]) > threshold:
                    raise ValueError(
                        f"Gate {gate_name} on qubits {q0}/{q1} violates blockade radius {blockade_radius:.3f}"
                    )


_PROFILE_METADATA: Dict[Tuple[str, Optional[str]], Dict[str, str]] = {
    (
        "runtime",
        None,
    ): {
        "label": "Legacy runtime",
        "description": "Minimal two-qubit local runner used by low-level tests.",
        "geometry": "Pair of tweezers separated by 1.0 units (units are arbitrary).",
        "noise_behavior": "Ideal evolution; only deterministic operations are applied.",
        "persona": "service regression",
    },
    (
        "quera.na_vm.sim",
        "ideal_small_array",
    ): {
        "label": "Ideal tutorial array",
        "description": "Ten-site 1D array with no noise for quick SDK/CLI walkthroughs.",
        "geometry": "1D chain with unit spacing and blockade radius 1.5.",
        "noise_behavior": "All noise sources disabled (deterministic statevector).",
        "persona": "education",
    },
    (
        "quera.na_vm.sim",
        "noisy_square_array",
    ): {
        "label": "Noisy square array",
        "description": "4x4 logical grid with moderate depolarizing and idle dephasing noise.",
        "geometry": "Conceptual 4x4 layout flattened to 16 slots with blockade radius 2.0.",
        "noise_behavior": "Gate depolarizing noise at the 1% level plus idle phase drift.",
        "persona": "algorithm prototyping",
    },
    (
        "quera.na_vm.sim",
        "lossy_chain",
    ): {
        "label": "Loss-dominated chain",
        "description": "Six-qubit chain that injects heavy loss to exercise erasure-aware code.",
        "geometry": "1D chain with 1.5 spacing and shared blockade radius 1.5.",
        "noise_behavior": "Runtime loss channel with 10% upfront loss and idle losses per gate.",
        "persona": "loss-aware algorithms",
    },
    (
        "quera.na_vm.sim",
        "benchmark_chain",
    ): {
        "label": "Benchmark chain (20 qubits)",
        "description": "Medium-size array for GHZ / volume tests with realistic depolarizing noise.",
        "geometry": "1D chain of 20 qubits at 1.3 spacing, blockade radius 1.6.",
        "noise_behavior": "Balanced single/two-qubit channels, correlated CZ errors, idle dephasing.",
        "persona": "integration + benchmarking",
    },
    (
        "quera.na_vm.sim",
        "readout_stress",
    ): {
        "label": "Readout stress array",
        "description": "Eight-qubit chain emphasizing SPAM noise and mild runtime loss.",
        "geometry": "1D chain of 8 qubits with unit spacing and blockade radius 1.2.",
        "noise_behavior": "3% symmetric readout flips plus mild depolarizing and idle phase noise.",
        "persona": "diagnostics",
    },
}

_PROFILE_TABLE: Dict[Tuple[str, Optional[str]], Dict[str, Any]] = {
    # Legacy local runtime path: single pair of atoms.
    ("runtime", None): {
        "positions": [0.0, 1.0],
        "blockade_radius": 1.0,
        "noise": None,
    },
    # UX-aligned ideal profile for quick tutorial runs.
    ("quera.na_vm.sim", "ideal_small_array"): {
        "positions": [float(i) for i in range(10)],
        "blockade_radius": 1.5,
        "noise": None,
    },
    # Captures a 4x4 grid with moderate depolarizing noise and idle dephasing.
    ("quera.na_vm.sim", "noisy_square_array"): {
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
        "blockade_radius": 2.0,
        "noise": {
            "gate": {
                "single_qubit": {"px": 0.005, "py": 0.005, "pz": 0.005},
                "two_qubit_control": {"px": 0.01, "py": 0.01, "pz": 0.01},
                "two_qubit_target": {"px": 0.01, "py": 0.01, "pz": 0.01},
            },
            "idle_rate": 200.0,
            "phase": {"idle": 0.02},
        },
    },
    # Heavy loss channel illustrating erasure-dominated behavior.
    ("quera.na_vm.sim", "lossy_chain"): {
        "positions": [float(i) * 1.5 for i in range(6)],
        "blockade_radius": 1.5,
        "noise": {
            "p_loss": 0.1,
            "loss_runtime": {"per_gate": 0.05, "idle_rate": 5.0},
        },
    },
    # 20-qubit benchmark chain for GHZ/volume experiments with moderate noise.
    ("quera.na_vm.sim", "benchmark_chain"): {
        "positions": [float(i) * 1.3 for i in range(20)],
        "blockade_radius": 1.6,
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
    },
    # SPAM-focused preset with notable readout flips and mild runtime loss.
    ("quera.na_vm.sim", "readout_stress"): {
        "positions": [float(i) for i in range(8)],
        "blockade_radius": 1.2,
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
    },
}


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
        metadata = _PROFILE_METADATA.get(key)
        if metadata:
            entry["metadata"] = dict(metadata)
        presets.setdefault(device_id, {})[profile] = entry
    return presets


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
