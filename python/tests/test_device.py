import neutral_atom_vm

import pytest
from neutral_atom_vm.device import build_device_from_config

def test_device_submit_program_runtime():
    device = neutral_atom_vm.connect_device("runtime")

    program = [
        {"op": "AllocArray", "n_qubits": 2},
        {"op": "ApplyGate", "name": "X", "targets": [1], "param": 0.0},
        {"op": "Measure", "targets": [0, 1]},
    ]

    job = device.submit(program, shots=1)
    result = job.result()
    assert result["status"] == "completed"
    assert result["measurements"]
    assert result["measurements"][0]["bits"] == [0, 1]


def test_device_submit_kernel():
    pytest.importorskip("bloqade")

    from .squin_programs import bell_pair

    device = neutral_atom_vm.connect_device("quera.na_vm.sim", profile="ideal_small_array")
    job = device.submit(bell_pair, shots=1)
    result = job.result()
    assert result["status"] == "completed"
    assert any(record["bits"] for record in result["measurements"])


def test_build_device_from_config_injects_noise():
    cfg = {
        "positions": [0.0],
        "blockade_radius": 1.0,
        "noise": {"p_loss": 1.0},
    }
    device = build_device_from_config("runtime", profile=None, config=cfg)

    program = [
        {"op": "AllocArray", "n_qubits": 1},
        {"op": "Measure", "targets": [0]},
    ]

    job = device.submit(program, shots=1)
    result = job.result()
    assert result["status"] == "completed"
    assert result["measurements"][0]["bits"] == [-1]
