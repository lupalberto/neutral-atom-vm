import pytest
import neutral_atom_vm

print("module file:", neutral_atom_vm.__file__)

pytest.importorskip("bloqade")

from .squin_programs import bell_pair, single_qubit_rotations


def test_submit_job_uses_native_extension():
    module_name = getattr(neutral_atom_vm.submit_job, "__module__", "")
    assert module_name.endswith("_neutral_atom_vm"), module_name


def test_submit_job():
    program = [
        {"op": "AllocArray", "n_qubits": 2},
        {"op": "ApplyGate", "name": "X", "targets": [1], "param": 0.0},
        {"op": "Measure", "targets": [0, 1]},
    ]

    result = neutral_atom_vm.submit_job(program, positions=[0.0, 1.0], blockade_radius=1.0)
    assert result["status"] == "completed"
    assert result["measurements"]
    assert result["measurements"][0]["bits"] == [0, 1]


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


def main():
    test_submit_job()
    test_squin_lowering_to_program()


if __name__ == "__main__":
    main()
