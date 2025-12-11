import pytest

from bloqade import squin

from neutral_atom_vm import (
    qec,
    available_presets,
    build_device_from_config,
    has_stabilizer_backend,
)


@squin.kernel
def repetition_code_with_pauli_noise():
    """Distance-3 repetition code with explicit Pauli-X noise on the stabilizer backend."""

    distance = 3
    rounds = 2
    px = 0.02

    q = squin.qalloc(distance + 1)
    data = q[:distance]
    ancilla = q[distance]

    for _ in range(rounds):
        for idx in range(distance):
            squin.cx(data[idx], ancilla)
            squin.single_qubit_pauli_channel(px, 0.0, 0.0, data[idx])
        squin.single_qubit_pauli_channel(px, 0.0, 0.0, ancilla)
        squin.measure(ancilla)

    squin.measure(data)


@pytest.mark.skipif(not has_stabilizer_backend(), reason="stabilizer backend unavailable")
def test_repetition_code_metrics_with_explicit_pauli_channels():
    """Ensure we can run a Pauli-annotated repetition code on the stabilizer backend and extract QEC metrics."""

    distance = 3
    rounds = 2
    shots = 64

    presets = available_presets()
    config = dict(presets["stabilizer"]["ideal_square_grid"])
    # Match the QEC repetition helper pattern: disable blockade constraints
    config["blockade_radius"] = 0.0
    dev = build_device_from_config("stabilizer", profile="ideal_square_grid", config=config)

    result = dev.submit(repetition_code_with_pauli_noise, shots=shots).result()
    metrics = qec.compute_repetition_code_metrics(
        result,
        distance=distance,
        rounds=rounds,
    )

    assert metrics["shots"] == shots
    assert 0.0 <= metrics["logical_x_error_rate"] <= 1.0

