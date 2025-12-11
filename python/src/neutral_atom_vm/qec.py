"""Helper primitives for small QEC workflows on the Neutral Atom VM."""

from __future__ import annotations

from typing import Mapping

from bloqade import squin

from .device import available_presets, build_device_from_config
from .job import SimpleNoiseConfig, JobResult, has_stabilizer_backend


def _build_repetition_kernel(
    distance: int,
    rounds: int,
    logical_state: int,
) -> squin.Method | None:
    if distance <= 0:
        raise ValueError("distance must be positive")
    if rounds <= 0:
        raise ValueError("rounds must be positive")

    data_targets = list(range(distance))

    @squin.kernel
    def _kernel():
        squin.qalloc(distance + 1)
        ancilla_idx = distance
        if logical_state:
            for idx in range(distance):
                squin.x(idx)
        for _ in range(rounds):
            for idx in range(distance):
                squin.cx(idx, ancilla_idx)
            squin.measure(ancilla_idx)
        squin.measure(data_targets)

    return _kernel


def repetition_code_job(
    distance: int,
    rounds: int,
    *,
    shots: int = 64,
    device_id: str | None = None,
    profile: str = "lossy_block",
    noise: SimpleNoiseConfig | None = None,
    profile_noise: bool = True,
    logical_state: int = 0,
    max_threads: int | None = None,
) -> JobResult:
    """
    Submit a small repetition-code QEC circuit to the VM.

    Set ``profile_noise=False`` to ignore the profile's built-in noise model so
    that only the optionally provided ``noise`` argument contributes errors.
    """

    target_device = device_id or ("stabilizer" if has_stabilizer_backend() else "local-cpu")
    presets = available_presets()
    preset_config = presets.get(target_device, {}).get(profile)
    if preset_config is None:
        device = build_device_from_config(target_device, profile=profile)
    else:
        modified = dict(preset_config)
        modified["blockade_radius"] = 0.0
        if not profile_noise:
            modified.pop("noise", None)
        device = build_device_from_config(target_device, profile=profile, config=modified)
    if noise is not None:
        device.noise = noise
    kernel = _build_repetition_kernel(distance, rounds, logical_state)
    handle = device.submit(kernel, shots=shots, max_threads=max_threads)
    return handle.result()


def compute_repetition_code_metrics(
    result: Mapping[str, object],
    *,
    distance: int,
    logical_state: int = 0,
    rounds: int | None = None,
) -> dict[str, float]:
    """Derive logical error metrics from a repetition-code JobResult."""

    measurements = result.get("measurements") or []
    data_targets = tuple(range(distance))
    data_records = []
    for record in measurements:
        if not isinstance(record, Mapping):
            continue
        targets = tuple(record.get("targets") or [])
        if targets != data_targets:
            continue
        data_records.append(record)
    if not data_records:
        raise ValueError("No measurement records matching the repetition code distance were found")

    shots = len(data_records)
    logical_errors = 0
    for record in data_records:
        bits = list(record.get("bits") or [])
        if len(bits) != distance:
            continue
        ones = sum(1 for bit in bits if bit == 1)
        zeros = sum(1 for bit in bits if bit == 0)
        majority = 0 if zeros >= ones else 1
        if any(bit not in (0, 1) for bit in bits):
            majority = -1
        if majority == -1 or majority != logical_state:
            logical_errors += 1

    return {
        "logical_x_error_rate": logical_errors / shots,
        "shots": shots,
        "distance": float(distance),
        "rounds": float(rounds) if rounds is not None else float("nan"),
    }
