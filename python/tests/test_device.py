import neutral_atom_vm
import neutral_atom_vm.device as device_mod

import pytest
from neutral_atom_vm import to_vm_program
from neutral_atom_vm.device import build_device_from_config, available_presets

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
    assert "logs" in result
    assert any(entry.get("category") == "AllocArray" for entry in result["logs"])
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


def test_loop_kernel_lowers_and_runs():
    pytest.importorskip("bloqade")

    from .squin_programs import loop_cx

    program = to_vm_program(loop_cx)
    assert any(
        instruction.get("op") == "ApplyGate" and instruction.get("name") == "CX"
        for instruction in program
    )

    device = neutral_atom_vm.connect_device("quera.na_vm.sim", profile="ideal_small_array")
    job = device.submit(loop_cx, shots=1)
    result = job.result()
    assert result["status"] == "completed"
    assert result["measurements"]
    assert len(result["measurements"][0]["bits"]) == 2


def test_complex_squin_kernel_runs_on_benchmark_chain():
    """A more complex Squin kernel should run on benchmark_chain and emit logs."""

    pytest.importorskip("bloqade")

    from .squin_programs import benchmark_chain_complex

    device = neutral_atom_vm.connect_device("quera.na_vm.sim", profile="benchmark_chain")
    job = device.submit(benchmark_chain_complex, shots=4)
    result = job.result()

    assert result["status"] == "completed"
    assert result["measurements"]
    # Kernel allocates and measures 8 qubits.
    assert len(result["measurements"][0]["bits"]) == 8
    # Execution logs should include at least some gate/measurement events.
    assert "logs" in result
    categories = {entry.get("category") for entry in result["logs"]}
    assert "ApplyGate" in categories


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


def test_available_presets_lists_built_in_profiles():
    presets = available_presets()

    assert "quera.na_vm.sim" in presets
    assert "runtime" in presets
    assert "local-cpu" in presets
    assert "local-arc" in presets

    sim_profiles = presets["quera.na_vm.sim"]
    for profile in ("ideal_small_array", "benchmark_chain", "readout_stress"):
        assert profile in sim_profiles
        entry = sim_profiles[profile]
        assert entry["positions"], "expected positions in preset"
        assert "metadata" in entry and entry["metadata"].get("description")


@pytest.mark.parametrize("device_id", ["local-cpu", "local-arc"])
def test_connect_device_aliases_preserve_profiles(device_id):
    reference = neutral_atom_vm.connect_device("quera.na_vm.sim", profile="benchmark_chain")
    aliased = neutral_atom_vm.connect_device(device_id, profile="benchmark_chain")

    assert aliased.positions == reference.positions
    assert aliased.blockade_radius == reference.blockade_radius
    assert aliased.noise is not None
    assert aliased.profile == reference.profile


@pytest.mark.parametrize(
    "profile, expected_qubits",
    [
        ("benchmark_chain", 20),
        ("readout_stress", 8),
    ],
)
def test_connect_device_supports_new_presets(profile, expected_qubits):
    device = neutral_atom_vm.connect_device("quera.na_vm.sim", profile=profile)

    assert len(device.positions) == expected_qubits
    assert device.noise is not None
    assert device.noise.gate.single_qubit.px >= 0.0


def test_device_submit_raises_on_blockade_violation():
    device = neutral_atom_vm.connect_device("quera.na_vm.sim", profile="benchmark_chain")
    program = [
        {"op": "AllocArray", "n_qubits": 16},
        {"op": "ApplyGate", "name": "CX", "targets": [0, 15], "param": 0.0},
        {"op": "Measure", "targets": [0, 15]},
    ]

    with pytest.raises(ValueError, match="blockade radius"):
        device.submit(program, shots=1)


def test_device_submit_forwards_thread_limit(monkeypatch):
    captured = {}

    def fake_submit(job):
        captured["job"] = job
        return {"status": "completed", "measurements": []}

    monkeypatch.setattr(device_mod, "submit_job", fake_submit)

    device = neutral_atom_vm.connect_device("runtime")
    program = [
        {"op": "AllocArray", "n_qubits": 1},
        {"op": "Measure", "targets": [0]},
    ]
    device.submit(program, shots=1, max_threads=7)

    assert captured["job"].max_threads == 7
