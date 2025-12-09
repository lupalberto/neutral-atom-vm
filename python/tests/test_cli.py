import json
import pytest

import bloqade


def test_quera_vm_run_executes_kernel_and_prints_result(capsys):
    """CLIs should select a VM device and run a kernel via the hardware VM."""

    from neutral_atom_vm import cli

    # Use an existing test kernel so we exercise the full lowering + device path.
    argv = [
        "run",
        "--output",
        "json",
        "--device",
        "local-cpu",
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


def test_quera_vm_run_writes_logs_to_file(tmp_path, capsys):
    """`--log-file` should capture VM event logs separately from stdout."""

    from neutral_atom_vm import cli

    log_path = tmp_path / "queravm.log"
    argv = [
        "run",
        "--output",
        "json",
        "--device",
        "local-cpu",
        "--profile",
        "ideal_small_array",
        "--shots",
        "2",
        "--log-file",
        str(log_path),
        "tests.squin_programs:bell_pair",
    ]

    exit_code = cli.main(argv)
    captured = capsys.readouterr()

    assert exit_code == 0
    assert '"logs"' not in captured.out

    assert log_path.exists()
    contents = log_path.read_text()
    assert "category=" in contents
    assert "shot=" in contents
    assert "message=" in contents


def test_quera_vm_run_accepts_profile_config(capsys, tmp_path):
    """CLI profile config should override defaults and allow custom noise."""

    from neutral_atom_vm import cli

    script = tmp_path / "noise_script.py"
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

    profile_cfg = tmp_path / "profile.json"
    profile_cfg.write_text(
        json.dumps(
            {
                "positions": [0.0, 1.0],
                "blockade_radius": 1.0,
                "noise": {"p_loss": 1.0},
            }
        )
    )

    argv = [
        "run",
        "--output",
        "json",
        "--profile-config",
        str(profile_cfg),
        "--device",
        "local-cpu",
        "--profile",
        "ideal_small_array",
        "--shots",
        "2",
        f"{script}:bell_pair",
    ]

    exit_code = cli.main(argv)
    captured = capsys.readouterr()

    assert exit_code == 0
    result = json.loads(captured.out)
    assert all(
        bit == -1
        for record in result["measurements"]
        for bit in record["bits"]
    )


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
        "local-cpu",
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


def test_quera_vm_run_reports_blockade_violation(capsys, tmp_path):
    from neutral_atom_vm import cli

    script = tmp_path / "wide_gate.py"
    script.write_text(
        "from bloqade import squin\n\n"
        "@squin.kernel\n"
        "def wide():\n"
        "    q = squin.qalloc(16)\n"
        "    squin.h(q[0])\n"
        "    squin.cx(q[0], q[15])\n"
        "    squin.measure(q)\n"
    )

    argv = [
        "run",
        "--device",
        "local-cpu",
        "--profile",
        "benchmark_chain",
        "--shots",
        "2",
        f"{script}:wide",
    ]

    exit_code = cli.main(argv)
    captured = capsys.readouterr()

    assert exit_code == 1
    assert "Gate CX on qubits 0/15 violates blockade radius" in captured.err


def test_summarize_result_includes_timeline_section():
    from neutral_atom_vm import cli

    timeline = [
        {"start_time": 0.0, "duration": 0.5, "op": "AllocArray", "detail": "n_qubits=2"},
        {"start_time": 0.5, "duration": 0.5, "op": "ApplyGate", "detail": "X targets=[0]"},
    ]
    text = cli._summarize_result(
        {
            "status": "completed",
            "elapsed_time": 0.001,
            "measurements": [],
            "timeline": timeline,
            "timeline_units": "us",
        },
        device="local-cpu",
        profile="ideal_small_array",
        shots=1,
    )
    assert "Timeline (us):" in text
    assert "ApplyGate" in text


def test_quera_vm_grid_output_for_noisy_square_array(capsys):
    """Summary output for the 2D profile should include a grid view."""

    from neutral_atom_vm import cli

    pytest.importorskip("bloqade")

    argv = [
        "run",
        "--device",
        "local-cpu",
        "--profile",
        "noisy_square_array",
        "--shots",
        "1",
        "tests.squin_programs:grid_entangle_4x4",
    ]

    exit_code = cli.main(argv)
    captured = capsys.readouterr()

    assert exit_code == 0
    assert "Grid:" in captured.out
