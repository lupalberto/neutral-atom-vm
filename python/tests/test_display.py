from neutral_atom_vm.display import render_job_result_html, render_timeline_chart


def test_render_job_result_includes_timeline_chart():
    timeline = [
        {"start_time": 0.0, "duration": 1.0, "op": "AllocArray", "detail": "n=2"},
        {"start_time": 1.0, "duration": 2.5, "op": "ApplyGate", "detail": "X targets=[0]"},
        {"start_time": 3.5, "duration": 1.0, "op": "Measure", "detail": "targets=[0]"},
    ]
    result = {
        "status": "completed",
        "elapsed_time": 0.01,
        "measurements": [{"targets": [0], "bits": [1]}],
        "timeline": timeline,
        "timeline_units": "us",
    }
    html = render_job_result_html(
        result=result,
        device="local-cpu",
        profile="ideal_small_array",
        shots=1,
    )
    assert "na-vm-timeline" in html
    assert "Timeline" in html
    assert "AllocArray" in html


def test_render_timeline_chart_handles_empty_entries():
    assert render_timeline_chart([], unit="us") == ""
