import pytest

from neutral_atom_vm import to_vm_program


def test_lowering_supports_range_over_symbolic_n():
    """to_vm_program should handle for-loops over range(n) where n is symbolic."""

    pytest.importorskip("bloqade")

    from .squin_programs import loop_over_symbolic_n

    program = to_vm_program(loop_over_symbolic_n)

    # We expect an AllocArray followed by a sequence of X gates and a Measure.
    ops = [instr.get("op") for instr in program]
    assert "AllocArray" in ops
    assert "ApplyGate" in ops
    assert "Measure" in ops


def test_lowering_supports_nested_range_loops():
    """to_vm_program should also handle nested for-loops over range(n)."""

    pytest.importorskip("bloqade")

    from .squin_programs import nested_loops_over_range

    program = to_vm_program(nested_loops_over_range)

    ops = [instr.get("op") for instr in program]
    # Expect at least one X gate application from the nested loops.
    assert any(
        instr.get("op") == "ApplyGate" and instr.get("name") == "X"
        for instr in program
    )
