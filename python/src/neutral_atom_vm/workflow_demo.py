"""Utility script to exercise the Neutral Atom VM workflow end-to-end."""

from __future__ import annotations

import json
from typing import Sequence

from bloqade import squin

from . import HardwareConfig, JobRequest, submit_job, to_vm_program


@squin.kernel
def _demo_kernel():
    q = squin.qalloc(2)
    squin.h(q[0])
    squin.cx(q[0], q[1])
    squin.measure(q)


def build_demo_program():
    """Lower the demo kernel to a VM program list."""

    return to_vm_program(_demo_kernel)


def run_demo(*, positions: Sequence[float] | None = None, blockade_radius: float = 5.0, shots: int = 8):
    """Execute the demo program via submit_job and return the result dict."""

    program = build_demo_program()
    demo_positions = list(positions) if positions is not None else [0.0, 0.5]
    if len(demo_positions) < 2:
        raise ValueError("Need two positions for the demo program")

    job = JobRequest(
        program=program,
        hardware=HardwareConfig(positions=demo_positions, blockade_radius=blockade_radius),
        device_id="runtime",
        profile=None,
        shots=shots,
    )
    return submit_job(job)


def main():
    result = run_demo()
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
