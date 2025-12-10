import pytest
import neutral_atom_vm

print("module file:", neutral_atom_vm.__file__)

pytest.importorskip("bloqade")

from .squin_programs import bell_pair, single_qubit_rotations, pauli_kick_only


def test_submit_job_accepts_job_request():
    program = [
        {"op": "AllocArray", "n_qubits": 2},
        {"op": "ApplyGate", "name": "X", "targets": [1], "param": 0.0},
        {"op": "Measure", "targets": [0, 1]},
    ]

    # High-level, composable JobRequest: hardware config is nested and
    # device/profile are kept separate from low-level geometry.
    job = neutral_atom_vm.JobRequest(
        program=program,
        hardware=neutral_atom_vm.HardwareConfig(
            positions=[0.0, 1.0],
            blockade_radius=1.0,
        ),
        device_id="local-cpu",
        profile=None,
        shots=32,
    )

    result = neutral_atom_vm.submit_job(job)
    assert result["status"] == "completed"
    assert result["measurements"]
    assert result["measurements"][0]["bits"] == [0, 1]


def test_submit_job_preserves_job_id_roundtrip():
    program = [
        {"op": "AllocArray", "n_qubits": 1},
        {"op": "Measure", "targets": [0]},
    ]

    job = neutral_atom_vm.JobRequest(
        program=program,
        hardware=neutral_atom_vm.HardwareConfig(
            positions=[0.0],
            blockade_radius=1.0,
        ),
        device_id="local-cpu",
        profile=None,
        shots=1,
        job_id="python-test-job-123",
    )

    result = neutral_atom_vm.submit_job(job)
    assert result["job_id"] == "python-test-job-123"


def test_submit_job_applies_noise_loss():
    program = [
        {"op": "AllocArray", "n_qubits": 1},
        {"op": "Measure", "targets": [0]},
    ]

    job = neutral_atom_vm.JobRequest(
        program=program,
        hardware=neutral_atom_vm.HardwareConfig(
            positions=[0.0],
            blockade_radius=1.0,
        ),
        device_id="local-cpu",
        profile=None,
        shots=1,
        noise=neutral_atom_vm.SimpleNoiseConfig(p_loss=1.0),
    )

    result = neutral_atom_vm.submit_job(job)
    assert all(
        bit == -1
        for record in result["measurements"]
        for bit in record["bits"]
    )


def test_idle_noise_causes_phase_flip():
    program = [
        {"op": "AllocArray", "n_qubits": 1},
        {"op": "ApplyGate", "name": "H", "targets": [0], "param": 0.0},
        {"op": "Wait", "duration": 1.0},
        {"op": "ApplyGate", "name": "H", "targets": [0], "param": 0.0},
        {"op": "Measure", "targets": [0]},
    ]

    job = neutral_atom_vm.JobRequest(
        program=program,
        hardware=neutral_atom_vm.HardwareConfig(
            positions=[0.0],
            blockade_radius=1.0,
        ),
        device_id="local-cpu",
        profile=None,
        shots=64,
        noise=neutral_atom_vm.SimpleNoiseConfig(idle_rate=1000.0),
    )

    result = neutral_atom_vm.submit_job(job)
    bits = [record["bits"][0] for record in result["measurements"]]
    # A high idle rate should flip the phase almost every time, so at least
    # one shot should produce a `1`.
    assert any(bit == 1 for bit in bits)


def test_squin_lowering_to_program():
    program = neutral_atom_vm.to_vm_program(bell_pair)
    assert program == [
        {"op": "AllocArray", "n_qubits": 2},
        {"op": "ApplyGate", "name": "H", "targets": [0], "param": 0.0},
        {"op": "ApplyGate", "name": "CX", "targets": [0, 1], "param": 0.0},
        {"op": "Measure", "targets": [0, 1]},
    ]


def test_squin_lowering_param_gates():
    program = neutral_atom_vm.to_vm_program(single_qubit_rotations)
    assert program == [
        {"op": "AllocArray", "n_qubits": 1},
        {"op": "ApplyGate", "name": "RX", "targets": [0], "param": 0.125},
        {"op": "ApplyGate", "name": "RY", "targets": [0], "param": 0.25},
        {"op": "ApplyGate", "name": "RZ", "targets": [0], "param": 0.5},
        {"op": "Measure", "targets": [0]},
    ]


@pytest.mark.skipif(
    not neutral_atom_vm.has_stabilizer_backend(),
    reason="Stim backend not available in this build",
)
def test_stabilizer_job_includes_stim_circuit_for_noise_kernel():
    dev = neutral_atom_vm.connect_device("stabilizer", profile="ideal_small_array")
    request = dev.build_job_request(pauli_kick_only, shots=1)
    payload = request.to_dict()
    circuit = payload.get("stim_circuit")
    assert circuit
    assert "PAULI_CHANNEL_1" in circuit


@pytest.mark.skipif(
    not neutral_atom_vm.has_stabilizer_backend(),
    reason="Stim backend not available in this build",
)
def test_stabilizer_executes_explicit_pauli_channel():
    dev = neutral_atom_vm.connect_device("stabilizer", profile="ideal_small_array")
    handle = dev.submit(pauli_kick_only, shots=1)
    result = handle.result()
    bits = result["measurements"][0]["bits"]
    assert bits == [1]


def test_statevector_rejects_explicit_noise_kernel():
    dev = neutral_atom_vm.connect_device("local-cpu", profile="ideal_small_array")
    with pytest.raises(RuntimeError):
        dev.build_job_request(pauli_kick_only, shots=1)


def main():
    test_submit_job_accepts_job_request()
    test_submit_job_preserves_job_id_roundtrip()
    test_submit_job_applies_noise_loss()
    test_squin_lowering_to_program()


if __name__ == "__main__":
    main()
