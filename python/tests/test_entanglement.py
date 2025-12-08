from __future__ import annotations

import pytest

from neutral_atom_vm import connect_device

pytest.importorskip("bloqade")

from .squin_programs import bell_pair  # noqa: E402


def test_bell_pair_produces_correlated_bits():
    """Bell pair should yield perfectly correlated measurement outcomes."""

    device = connect_device("runtime")
    handle = device.submit(bell_pair, shots=32)
    result = handle.result()

    measurements = result.get("measurements", [])
    # Sanity-check we received the expected number of shots.
    assert len(measurements) == 32

    for record in measurements:
        bits = record.get("bits")
        # Expect two-qubit readout where both bits are always equal (00 or 11).
        assert bits == [0, 0] or bits == [1, 1]

