import pytest

pytest.importorskip("bloqade")


def test_workflow_demo_runs_end_to_end():
    from neutral_atom_vm.workflow_demo import run_demo

    result = run_demo(shots=4)

    assert result["status"] == "completed"
    assert result["measurements"], "expected measurements from demo run"
