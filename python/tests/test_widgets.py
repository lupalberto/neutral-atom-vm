"""Tests for the interactive profile configurator widget."""

from __future__ import annotations

import pytest

def _sample_presets():
    return {
        "device-alpha": {
            "tutorial": {
                "positions": [0.0, 1.0, 2.0],
                "blockade_radius": 1.5,
                "noise": {"p_loss": 0.1, "p_quantum_flip": 0.01},
                "metadata": {"label": "Alpha Tutorial", "persona": "education"},
            },
            "benchmark": {
                "positions": [0.0, 0.8, 1.6, 2.4],
                "blockade_radius": 1.7,
                "noise": {
                    "p_loss": 0.2,
                    "p_quantum_flip": 0.02,
                    "readout": {"p_flip0_to_1": 0.01, "p_flip1_to_0": 0.02},
                },
                "metadata": {"label": "Alpha Benchmark", "persona": "throughput"},
            },
        },
        "device-beta": {
            "dense": {
                "positions": [float(i) for i in range(6)],
                "blockade_radius": 2.0,
                "noise": {
                    "idle_rate": 0.5,
                    "phase": {"idle": 0.05},
                    "gate": {
                        "single_qubit": {"px": 0.01, "py": 0.02, "pz": 0.03},
                        "two_qubit_control": {"px": 0.01, "py": 0.01, "pz": 0.01},
                        "two_qubit_target": {"px": 0.01, "py": 0.01, "pz": 0.01},
                    },
                },
                "metadata": {
                    "label": "Beta Dense",
                    "persona": "diagnostics",
                    "description": "Dense geometry with tunable noise",
                },
            }
        },
    }


def _sample_result():
    return {
        "status": "completed",
        "elapsed_time": 0.123,
        "measurements": [
            {"bits": [0, 1, 1]},
            {"bits": [0, 1, 1]},
            {"bits": [1, 0, 1]},
            {"bits": [0, 1, 1]},
        ],
        "message": "ok",
    }


def _square_result():
    bits = [int(ch) for ch in "0110011001100110"]
    return {
        "status": "completed",
        "measurements": [{"bits": bits} for _ in range(2)],
    }


def _many_bitstrings():
    measurements = []
    for _ in range(4):
        measurements.append({"bits": [1, 1, 1, 1]})  # "1111"
    for _ in range(2):
        measurements.append({"bits": [0, 0, 1, 1]})  # "0011"
    for i in range(6):
        bitstring = f"{i:04b}"
        if bitstring == "0011":
            continue
        measurements.append({"bits": [int(ch) for ch in bitstring]})
    return measurements


def test_profile_configurator_populates_from_presets():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())

    assert configurator.device_dropdown.value == "device-alpha"
    assert list(configurator.profile_dropdown.options) == ["benchmark", "tutorial", ProfileConfigurator.CUSTOM_PROFILE_LABEL]
    assert pytest.approx(configurator.blockade_input.value, rel=1e-6) == 1.5
    assert "0.0" in configurator.positions_editor.value
    assert "alpha tutorial" in configurator.metadata_html.value.lower()


def test_profile_payload_reflects_user_changes():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())

    configurator.device_dropdown.value = "device-beta"
    configurator.profile_dropdown.value = "dense"
    configurator.blockade_input.value = 3.5
    configurator.positions_editor.value = "0.0, 1.1, 2.2"
    configurator.noise_fields["p_loss"].value = 0.42
    configurator.noise_fields["readout.p_flip0_to_1"].value = 0.04
    configurator.noise_fields["gate.single_qubit.px"].value = 0.07
    payload = configurator.profile_payload

    assert payload["device_id"] == "device-beta"
    assert payload["profile"] == "dense"
    assert payload["config"]["blockade_radius"] == 3.5
    assert payload["config"]["positions"] == [0.0, 1.1, 2.2]
    assert payload["config"]["noise"]["p_loss"] == 0.42
    assert payload["config"]["noise"]["readout"]["p_flip0_to_1"] == 0.04
    assert payload["config"]["noise"]["gate"]["single_qubit"]["px"] == 0.07


def test_profile_configurator_supports_custom_profile():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())

    configurator.profile_dropdown.value = ProfileConfigurator.CUSTOM_PROFILE_LABEL
    configurator.custom_profile_name.value = "my_custom"
    configurator.blockade_input.value = 2.5
    configurator.slot_count_input.value = 3
    configurator.slot_spacing_input.value = 1.2
    configurator._generate_positions()

    payload = configurator.profile_payload

    assert payload["profile"] == "my_custom"
    assert payload["config"]["blockade_radius"] == 2.5
    assert payload["config"]["positions"] == [0.0, 1.2, 2.4]
    assert configurator.metadata_html.value.startswith("<em")


def test_profile_configurator_uses_default_device_and_profile():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(
        presets=_sample_presets(),
        default_device="device-beta",
        default_profile="dense",
    )

    assert configurator.device_dropdown.value == "device-beta"
    assert configurator.profile_dropdown.value == "dense"
    assert "beta dense" in configurator.metadata_html.value.lower()


def test_profile_configurator_prefills_custom_payload():
    from neutral_atom_vm.widgets import ProfileConfigurator

    payload = {
        "profile": "bespoke",
        "config": {
            "positions": [0.0, 0.5, 1.0],
            "blockade_radius": 1.7,
            "noise": {"p_loss": 0.2},
        },
    }

    configurator = ProfileConfigurator(
        presets=_sample_presets(),
        default_device="device-alpha",
        default_profile_payload=payload,
    )

    assert configurator.profile_dropdown.value == ProfileConfigurator.CUSTOM_PROFILE_LABEL
    assert configurator.custom_profile_name.value == "bespoke"
    assert configurator.blockade_input.value == 1.7
    assert configurator.positions_editor.value.strip().startswith("0")


def test_job_result_viewer_displays_summary_and_histogram():
    from neutral_atom_vm.widgets import JobResultViewer

    viewer = JobResultViewer()
    viewer.load_result(
        _sample_result(),
        device="device-alpha",
        profile="tutorial",
        shots=4,
    )

    summary = viewer.summary_html.value.lower()
    histogram_html = viewer.histogram_html.value

    assert "completed" in summary
    assert "device-alpha" in summary
    assert "tutorial" in summary
    assert "011" in histogram_html
    assert "101" in histogram_html
    assert "{" not in histogram_html


def test_job_result_viewer_handles_missing_measurements():
    from neutral_atom_vm.widgets import JobResultViewer

    viewer = JobResultViewer()
    viewer.load_result(
        {"status": "failed", "measurements": [], "message": "boom"},
        device="runtime",
        profile=None,
        shots=2,
    )

    histogram = viewer.histogram_html.value.lower()
    assert "no histogram" in histogram


def test_job_result_viewer_renders_grid_preview():
    from neutral_atom_vm.widgets import JobResultViewer

    viewer = JobResultViewer()
    viewer.load_result(
        _square_result(),
        device="device-square",
        profile="noisy_square_array",
        shots=2,
    )

    assert "grid preview" in viewer.grid_html.value.lower()
    assert viewer.container.children[1] is viewer.grid_html


def test_job_result_viewer_renders_histogram():
    from neutral_atom_vm.widgets import JobResultViewer

    viewer = JobResultViewer()
    viewer.load_result(
        {"status": "completed", "measurements": _many_bitstrings()},
        device="device-hist",
        profile=None,
        shots=12,
    )

    assert "Histogram" in viewer.histogram_html.value


def test_histogram_paginated_and_sorted():
    from neutral_atom_vm.display import format_histogram

    measurements = _many_bitstrings()
    html = format_histogram(measurements, page_size=4)

    assert "data-hist-page='0'" in html
    assert "data-hist-page='1'" in html
    assert "data-hist-prev" in html
    assert "data-hist-next" in html
    assert "data-hist-page-label" in html

    assert html.index("1111") < html.index("0011")
