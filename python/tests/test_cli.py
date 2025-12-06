import pytest

pytest.importorskip("bloqade")


def test_quera_vm_run_executes_kernel_and_prints_result(capsys):
    """CLIs should select a VM device and run a kernel via the hardware VM."""

    from neutral_atom_vm import cli

    # Use an existing test kernel so we exercise the full lowering + device path.
    argv = [
        "run",
        "--output",
        "json",
        "--device",
        "quera.na_vm.sim",
        "--profile",
        "ideal_small_array",
        "--shots",
        "2",
        "tests.squin_programs:bell_pair",
    ]

    exit_code = cli.main(argv)
    captured = capsys.readouterr()

    assert exit_code == 0
    out = captured.out.strip()
    # The CLI should emit a JSON dict with at least these keys.
    assert '"status": "completed"' in out or '"status":"completed"' in out
    assert '"measurements"' in out


def test_quera_vm_run_accepts_script_path_with_kernel(capsys, tmp_path):
    """CLIs should also accept path/to/script.py:kernel targets."""

    from neutral_atom_vm import cli

    script = tmp_path / "bell_script.py"
    script.write_text(
        "from bloqade import squin\n"
        "\n"
        "@squin.kernel\n"
        "def bell_pair():\n"
        "    q = squin.qalloc(2)\n"
        "    squin.h(q[0])\n"
        "    squin.cx(q[0], q[1])\n"
        "    squin.measure(q)\n"
    )

    argv = [
        "run",
        "--output",
        "json",
        "--device",
        "quera.na_vm.sim",
        "--profile",
        "ideal_small_array",
        "--shots",
        "2",
        f"{script}:bell_pair",
    ]

    exit_code = cli.main(argv)
    captured = capsys.readouterr()

    assert exit_code == 0
    out = captured.out.strip()
    assert '"status": "completed"' in out or '"status":"completed"' in out
    assert '"measurements"' in out
