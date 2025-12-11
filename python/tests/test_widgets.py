"""Tests for the interactive profile configurator widget."""

from __future__ import annotations

from typing import Sequence

ROLE_COLORS = {
    "DATA": "#4CAF50",
    "ANCILLA": "#2196F3",
    "PARKING": "#FF9800",
    "CALIB": "#9E9E9E",
}

import ipywidgets as widgets

import pytest


def _linear_sites(values: Sequence[float]) -> list[dict[str, float]]:
    return [
        {"id": idx, "x": float(value), "y": 0.0, "z": 0.0, "zone_id": 0}
        for idx, value in enumerate(values)
    ]

def _sample_presets():
    tutorial_sites = _linear_sites([0.0, 1.0, 2.0])
    benchmark_sites = _linear_sites([0.0, 0.8, 1.6, 2.4])
    dense_sites = _linear_sites([float(i) for i in range(6)])
    return {
        "device-alpha": {
            "tutorial": {
                "positions": [0.0, 1.0, 2.0],
                "blockade_radius": 1.5,
                "noise": {"p_loss": 0.1, "p_quantum_flip": 0.01},
                "metadata": {"label": "Alpha Tutorial", "persona": "education"},
                "sites": tutorial_sites,
                "site_ids": [site["id"] for site in tutorial_sites],
                "regions": [
                    {"name": "center_data", "site_ids": [0, 1], "role": "DATA"},
                    {"name": "edge_parking", "site_ids": [2], "role": "PARKING"},
                ],
                "configuration_families": {
                    "default": {
                    "site_ids": [0, 1, 2],
                    "regions": [
                        {"name": "center_data", "site_ids": [0, 1], "role": "DATA"},
                        {"name": "edge_parking", "site_ids": [2], "role": "PARKING"},
                    ],
                    "description": "Tutorial default",
                },
                "sparse_chain": {
                    "site_ids": [0, 2],
                    "description": "Sparse selection",
                    "regions": [
                        {"name": "sparse_data", "site_ids": [0, 2], "role": "DATA"},
                    ],
                },
            },
                "default_configuration_family": "default",
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
                "sites": benchmark_sites,
                "site_ids": [site["id"] for site in benchmark_sites],
            },
        },
        "device-beta": {
            "dense": {
                "positions": [float(i) for i in range(6)],
                "blockade_radius": 2.0,
                "grid_layout": {
                    "dim": 2,
                    "rows": 2,
                    "cols": 3,
                    "layers": 1,
                    "spacing": {"x": 0.5, "y": 0.5, "z": 1.0},
                },
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
                "sites": dense_sites,
                "site_ids": [site["id"] for site in dense_sites],
            }
        },
        "device-gamma": {
            "block3d": {
                "positions": [float(i) for i in range(8)],
                "blockade_radius": 1.5,
                "grid_layout": {
                    "dim": 3,
                    "rows": 2,
                    "cols": 2,
                    "layers": 2,
                    "spacing": {"x": 1.0, "y": 1.0, "z": 1.0},
                },
                "sites": [
                    {"id": idx, "x": float(idx % 2), "y": float((idx // 2) % 2), "z": float(idx // 4), "zone_id": idx % 2}
                    for idx in range(8)
                ],
                "site_ids": [idx for idx in range(8)],
                "regions": [
                    {"name": "layer0", "site_ids": [0, 1, 2, 3], "role": "DATA"},
                    {"name": "layer1", "site_ids": [4, 5, 6, 7], "role": "ANCILLA"},
                ],
                "configuration_families": {
                    "block": {
                        "site_ids": [idx for idx in range(8)],
                        "regions": [
                            {"name": "layer0", "site_ids": [0, 1, 2, 3], "role": "DATA"},
                            {"name": "layer1", "site_ids": [4, 5, 6, 7], "role": "ANCILLA"},
                        ],
                    }
                },
                "default_configuration_family": "block",
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
    assert payload["config"]["site_ids"] == [0, 1, 2]
    assert payload["config"]["sites"][0]["x"] == 0.0


def test_profile_configurator_emits_coordinates():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())
    configurator.profile_dropdown.value = ProfileConfigurator.CUSTOM_PROFILE_LABEL
    configurator.positions_editor.value = "0 0\n1 0\n1 1"

    payload = configurator.profile_payload

    assert payload["config"]["positions"] == [0, 1, 2]
    assert payload["config"]["coordinates"] == [[0.0, 0.0], [1.0, 0.0], [1.0, 1.0]]
    assert payload["config"]["site_ids"] == [0, 1, 2]


def test_profile_configurator_supports_custom_profile():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())

    configurator.profile_dropdown.value = ProfileConfigurator.CUSTOM_PROFILE_LABEL
    configurator.custom_profile_name.value = "my_custom"
    configurator.blockade_input.value = 2.5
    configurator.dimension_selector.value = 1
    configurator.slot_count_input.value = 3
    configurator.spacing_x_input.value = 1.2
    configurator._generate_positions()

    payload = configurator.profile_payload

    assert payload["profile"] == "my_custom"
    assert payload["config"]["blockade_radius"] == 2.5
    assert payload["config"]["positions"] == [0.0, 1.2, 2.4]
    assert payload["config"]["site_ids"] == [0, 1, 2]
    assert payload["config"]["sites"][2]["x"] == pytest.approx(2.4)
    assert configurator.metadata_html.value.startswith("<em")


def test_profile_configurator_generates_2d_grid():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())
    configurator.dimension_selector.value = 2
    configurator.row_input.value = 2
    configurator.col_input.value = 3
    configurator.spacing_x_input.value = 0.5
    configurator.spacing_y_input.value = 1.0
    configurator._generate_positions()

    lines = [line.strip() for line in configurator.positions_editor.value.splitlines() if line.strip()]
    assert lines[:3] == [
        "0.0, 0.0",
        "0.5, 0.0",
        "1.0, 0.0",
    ]
    assert lines[3:] == [
        "0.0, 1.0",
        "0.5, 1.0",
        "1.0, 1.0",
    ]


def test_profile_configurator_addresses_configuration_families():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())
    assert hasattr(configurator, "configuration_dropdown")
    dropdown = configurator.configuration_dropdown
    values = [value for _, value in dropdown.options]
    assert "default" in values
    assert "sparse_chain" in values
    payload = configurator.profile_payload
    config = payload["config"]
    assert config["configuration_family"] == "default"
    assert any(region["role"] == "DATA" for region in config["regions"])


def test_geometry_tab_has_subtabs_and_help():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())

    tabs = configurator.container.children[-1]
    assert isinstance(tabs, widgets.Tab)
    geometry_tab = tabs.children[0]
    subtabs = geometry_tab.children[0]
    assert isinstance(subtabs, widgets.Tab)
    labels = [subtabs.get_title(idx) for idx in range(len(subtabs.children))]
    assert labels == ["Physical lattice", "Positions"]

    help_button = configurator.geometry_help_button
    help_output = configurator.geometry_help_output
    assert help_output.layout.display == "none"
    help_button.click()
    assert help_output.layout.display != "none"


def test_row_selector_toggle_targets_specific_row():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())
    configurator.device_dropdown.value = "device-beta"
    configurator.profile_dropdown.value = "dense"
    configurator.clear_button.click()
    configurator.row_selector.value = 1
    configurator.select_row_button.click()
    payload = configurator.profile_payload
    assert payload["config"]["site_ids"] == [3, 4, 5]


def test_region_warnings_report_non_data_sites():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())
    configurator.profile_dropdown.value = "tutorial"
    assert "parking" in configurator.warning_html.value.lower()


def test_layer_selector_hides_layer_sites_in_3d_array():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())
    configurator.device_dropdown.value = "device-gamma"
    configurator.profile_dropdown.value = "block3d"
    configurator.clear_button.click()
    configurator.layer_selector.value = 1
    configurator.select_layer_button.click()
    payload = configurator.profile_payload
    assert payload["config"]["site_ids"] == [4, 5, 6, 7]


def test_configuration_family_switch_updates_regions():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())
    configurator.profile_dropdown.value = "tutorial"
    default_regions = [
        box.children[0].description for box in configurator.region_palette.children
    ]
    assert "center_data" in "".join(default_regions)

    configurator.configuration_dropdown.value = "sparse_chain"
    sparse_regions = [
        box.children[0].description for box in configurator.region_palette.children
    ]
    assert len(sparse_regions) == 1
    assert "sparse_data" in sparse_regions[0]


def test_region_toggle_marks_configuration_custom():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())
    configurator.profile_dropdown.value = "tutorial"
    first_region_checkbox = configurator.region_palette.children[0].children[0]
    first_region_checkbox.value = not first_region_checkbox.value

    assert configurator.configuration_dropdown.value == ProfileConfigurator._CUSTOM_CONFIGURATION_VALUE
    payload = configurator.profile_payload
    assert payload["config"]["configuration_family"] == "custom"


def test_clearing_sites_emits_empty_site_ids():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())
    configurator.profile_dropdown.value = "tutorial"
    configurator.clear_button.click()
    payload = configurator.profile_payload
    assert payload["config"]["site_ids"] == []


def test_assign_role_creates_custom_regions_from_selection():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())
    configurator.profile_dropdown.value = "tutorial"
    configurator.clear_button.click()

    buttons = [
        child
        for child in configurator.lattice_grid.children
        if isinstance(child, widgets.Button)
    ]
    assert len(buttons) >= 2

    configurator.region_role_dropdown.value = "DATA"
    configurator.region_assign_button.click()  # enter assign mode
    buttons[0].click()
    configurator.region_assign_button.click()  # finalize DATA group

    configurator.region_role_dropdown.value = "ANCILLA"
    configurator.region_assign_button.click()  # start second assignment
    buttons[1].click()
    configurator.region_assign_button.click()  # finalize ANCILLA group

    roles = {region["role"] for region in configurator.profile_payload["config"]["regions"]}
    assert {"DATA", "ANCILLA"}.issubset(roles)
    assert configurator.region_palette.children[-1].children[0].value


def test_assignment_mode_highlights_pending_selection():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())
    configurator.profile_dropdown.value = "tutorial"
    configurator.region_role_dropdown.value = "ANCILLA"
    configurator.region_assign_button.click()  # enter assignment mode
    button = next(
        child
        for child in configurator.lattice_grid.children
        if isinstance(child, widgets.Button)
    )
    button_id = button.description
    button.click()
    updated = next(
        child
        for child in configurator.lattice_grid.children
        if isinstance(child, widgets.Button) and child.description == button_id
    )
    assert updated.style.button_color == ROLE_COLORS["ANCILLA"]


def test_lattice_buttons_toggle_individual_sites():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())
    first_button = next(
        (child for child in configurator.lattice_grid.children if isinstance(child, widgets.Button)),
        None,
    )
    assert first_button is not None, "expected lattice to render buttons"
    before = configurator.profile_payload["config"]["site_ids"]
    first_button.click()
    after = configurator.profile_payload["config"]["site_ids"]
    assert after != before, "clicking a lattice point should update occupancy"


def test_custom_payload_populates_geometry_panel():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())
    payload = {
        "profile": "my_custom",
        "config": {
            "positions": [0.0, 1.0],
            "sites": [
                {"id": 0, "x": 0.0, "y": 0.0, "zone_id": 1},
                {"id": 1, "x": 1.0, "y": 0.0, "zone_id": 2},
            ],
            "site_ids": [0, 1],
            "regions": [
                {"name": "band", "site_ids": [0], "role": "DATA"},
                {"name": "perimeter", "site_ids": [1], "role": "ANCILLA"},
            ],
            "grid_layout": {"dim": 2, "rows": 1, "cols": 2, "layers": 1, "spacing": {"x": 0.5, "y": 0.5, "z": 1.0}},
        },
    }

    configurator.profile_dropdown.value = ProfileConfigurator.CUSTOM_PROFILE_LABEL
    configurator._load_custom_payload(payload)
    assert any(isinstance(child, widgets.Button) for child in configurator.lattice_grid.children)
    assert "zone" in configurator.zone_summary.value.lower()

def test_profile_configurator_generates_3d_stack():
    from neutral_atom_vm.widgets import ProfileConfigurator

    configurator = ProfileConfigurator(presets=_sample_presets())
    configurator.dimension_selector.value = 3
    configurator.row_input.value = 1
    configurator.col_input.value = 2
    configurator.layer_input.value = 2
    configurator.spacing_x_input.value = 1.0
    configurator.spacing_y_input.value = 2.0
    configurator.spacing_z_input.value = 0.5
    configurator._generate_positions()

    lines = [
        line.strip()
        for line in configurator.positions_editor.value.splitlines()
        if line.strip()
    ]
    assert lines[0] == "0.0, 0.0, 0.0"
    assert lines[1] == "1.0, 0.0, 0.0"
    assert lines[2] == "0.0, 0.0, 0.5"
    assert lines[3] == "1.0, 0.0, 0.5"


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


def test_profile_configurator_fetches_remote_service(monkeypatch):
    from neutral_atom_vm.widgets import ProfileConfigurator

    captured = {}

    def fake_remote_catalog(url, endpoint="/devices", *, timeout=None):
        captured["args"] = (url, endpoint, timeout)
        return {
            "remote-device": {
                "noise_test": {
                    "positions": [0.0, 1.0, 2.0],
                    "blockade_radius": 1.4,
                }
            }
        }

    monkeypatch.setattr(
        "neutral_atom_vm.widgets.fetch_remote_device_catalog",
        fake_remote_catalog,
    )

    configurator = ProfileConfigurator(
        service_url="http://localhost:8080",
        devices_endpoint="/devices",
    )

    assert "remote-device" in configurator.device_dropdown.options
    assert captured["args"][0] == "http://localhost:8080"
    assert captured["args"][1] == "/devices"


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
        device="local-cpu",
        profile=None,
        shots=2,
    )

    histogram = viewer.histogram_html.value.lower()
    assert "no histogram" in histogram


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


def test_histogram_container_is_scrollable():
    from neutral_atom_vm.display import format_histogram

    html = format_histogram(_many_bitstrings(), page_size=4)
    assert "overflow-x:auto" in html
    assert "max-width:100%" in html


def test_histogram_represents_loss_atoms():
    from neutral_atom_vm.display import format_histogram

    html = format_histogram([{"bits": [-1, 0, 1]}])
    assert "L01" in html


def test_display_shot_with_result():
    pytest.importorskip("matplotlib")

    from neutral_atom_vm.display import display_shot
    from neutral_atom_vm.job import JobResult
    from neutral_atom_vm.layouts import GridLayout

    payload = {"measurements": [{"bits": [1, 0, 1, 0]}]}
    layout = GridLayout(dim=2, rows=2, cols=2)
    result = JobResult(
        payload,
        device_id="device-foo",
        profile="noisy_square_array",
        shots=1,
        layout=layout,
    )

    fig, ax = display_shot(result, shot_index=0, figsize=(2.0, 2.0))
    assert hasattr(fig, "canvas")
    assert hasattr(ax, "scatter")


def test_display_shot_handles_lost_atoms():
    pytest.importorskip("matplotlib")

    from neutral_atom_vm.display import display_shot
    from neutral_atom_vm.job import JobResult
    from neutral_atom_vm.layouts import GridLayout

    payload = {"measurements": [{"bits": [-1, 0, 1, -1]}]}
    layout = GridLayout(dim=2, rows=2, cols=2)
    result = JobResult(
        payload,
        device_id="device-foo",
        profile="lossy_chain",
        shots=1,
        layout=layout,
    )

    fig, ax = display_shot(result, shot_index=0, figsize=(2.0, 2.0))
    assert hasattr(fig, "canvas")
    assert hasattr(ax, "scatter")
    sizes = ax.collections[0].get_sizes()
    assert any(size >= 96 for size in sizes)
    facecolors = ax.collections[0].get_facecolors()
    assert any(color[3] < 1.0 for color in facecolors), "expected ghost markers to be semi-transparent"
    legend = ax.get_legend()
    assert legend is not None
    labels = [text.get_text() for text in legend.get_texts()]
    for expected in ("Measured → 1", "Measured → 0", "Lost atom"):
        assert expected in labels
    spacing = layout.spacing
    x_span = max(spacing[0] * (layout.cols - 1), spacing[0], 1e-6)
    y_span = max(spacing[1] * (layout.rows - 1), spacing[1], 1e-6)
    z_span = max(spacing[2] * (layout.layers - 1), spacing[2], 1e-6)
    x_lim = ax.get_xlim()[1] - ax.get_xlim()[0]
    y_lim = ax.get_ylim()[1] - ax.get_ylim()[0]
    z_lim = ax.get_zlim()[1] - ax.get_zlim()[0]
    assert x_lim >= x_span
    assert y_lim >= y_span


def test_display_shot_interactive_plotly():
    pytest.importorskip("plotly")

    from neutral_atom_vm.display import display_shot
    from neutral_atom_vm.job import JobResult
    from neutral_atom_vm.layouts import GridLayout
    import plotly.graph_objects as go

    payload = {"measurements": [{"bits": [-1, 0, 1, -1]}]}
    layout = GridLayout(dim=2, rows=2, cols=2)
    result = JobResult(
        payload,
        device_id="device-foo",
        profile="lossy_chain",
        shots=1,
        layout=layout,
    )

    fig = display_shot(result, shot_index=0, figsize=(2.0, 2.0), interactive=True)
    assert isinstance(fig, go.Figure)
    assert len(fig.data) >= 3
    lost_trace = next((trace for trace in fig.data if trace.name == "Lost atom"), None)
    assert lost_trace is not None
    assert lost_trace.opacity < 1.0
    assert fig.layout.height == 600
    assert fig.layout.width == 600
    assert fig.layout.showlegend


def test_display_shot_interactive_planar_for_1d():
    pytest.importorskip("plotly")

    from neutral_atom_vm.display import display_shot
    from neutral_atom_vm.job import JobResult
    from neutral_atom_vm.layouts import GridLayout
    import plotly.graph_objects as go

    payload = {"measurements": [{"bits": [1, 0, -1]}]}
    layout = GridLayout(dim=1, rows=1, cols=3)
    result = JobResult(
        payload,
        device_id="device-linear",
        profile=None,
        shots=1,
        layout=layout,
    )

    fig = display_shot(result, shot_index=0, figsize=(2.0, 2.0), interactive=True)
    assert isinstance(fig, go.Figure)
    trace_types = {trace.type for trace in fig.data}
    assert "scatter" in trace_types
    assert "scatter3d" not in trace_types
    assert fig.layout.xaxis is not None
    assert fig.layout.yaxis is not None
    assert fig.layout.showlegend


def test_display_shot_interactive_planar_for_2d():
    pytest.importorskip("plotly")

    from neutral_atom_vm.display import display_shot
    from neutral_atom_vm.job import JobResult
    from neutral_atom_vm.layouts import GridLayout
    import plotly.graph_objects as go

    payload = {"measurements": [{"bits": [1, 0, 1, 0]}]}
    layout = GridLayout(dim=2, rows=2, cols=2)
    result = JobResult(
        payload,
        device_id="device-grid",
        profile="noisy_square_array",
        shots=1,
        layout=layout,
    )

    fig = display_shot(result, shot_index=0, figsize=(2.0, 2.0), interactive=True)
    assert isinstance(fig, go.Figure)
    trace_types = {trace.type for trace in fig.data}
    assert "scatter" in trace_types
    assert "scatter3d" not in trace_types
    assert fig.layout.xaxis is not None
    assert fig.layout.yaxis is not None
    assert fig.layout.showlegend


def test_display_shot_interactive_handles_multiple_indices():
    pytest.importorskip("plotly")

    from neutral_atom_vm.display import display_shot
    from neutral_atom_vm.job import JobResult
    from neutral_atom_vm.layouts import GridLayout
    import plotly.graph_objects as go

    measurements = [
        {"bits": [1, 0, -1]},
        {"bits": [0, 1, 0]},
    ]
    layout = GridLayout(dim=1, rows=1, cols=3)
    result = JobResult(
        {"measurements": measurements},
        device_id="device-linear",
        profile=None,
        shots=2,
        layout=layout,
    )

    fig = display_shot(
        result,
        shot_indices=[0, 1],
        interactive=True,
    )
    assert isinstance(fig, go.Figure)
    assert fig.layout.showlegend
    axis_suffixes = ["", "2"]
    for suffix in axis_suffixes:
        xaxis_prop = getattr(fig.layout, f"xaxis{suffix}", None)
        yaxis_prop = getattr(fig.layout, f"yaxis{suffix}", None)
        assert xaxis_prop is not None
        assert yaxis_prop is not None


def test_display_shot_handles_multiple_indices_non_interactive():
    from neutral_atom_vm.display import display_shot
    from neutral_atom_vm.job import JobResult
    from neutral_atom_vm.layouts import GridLayout

    measurements = [
        {"bits": [1, 0, -1]},
        {"bits": [0, 1, 0]},
    ]
    layout = GridLayout(dim=1, rows=1, cols=3)
    result = JobResult(
        {"measurements": measurements},
        device_id="device-linear",
        profile=None,
        shots=2,
        layout=layout,
    )

    figs = display_shot(result, shot_indices=[0, 1])
    assert isinstance(figs, tuple)
    fig, axes = figs
    assert hasattr(fig, "axes")
    assert len(axes) == 2
def test_display_shot_interactive_planar_handles_partial_measurement():
    pytest.importorskip("plotly")

    from neutral_atom_vm.display import display_shot
    from neutral_atom_vm.job import JobResult
    from neutral_atom_vm.layouts import GridLayout
    import plotly.graph_objects as go

    payload = {"measurements": [{"bits": [1, 0]}]}
    layout = GridLayout(dim=1, rows=1, cols=10)
    result = JobResult(
        payload,
        device_id="device-linear",
        profile=None,
        shots=1,
        layout=layout,
    )

    fig = display_shot(result, shot_index=0, interactive=True)
    assert isinstance(fig, go.Figure)
    trace_names = {trace.name for trace in fig.data}
    assert "Lost atom" not in trace_names
    point_count = sum(len(trace.x or []) for trace in fig.data)
    assert point_count == 2


def test_configurator_applies_grid_layout():
    from neutral_atom_vm import available_presets
    from neutral_atom_vm.widgets import ProfileConfigurator

    presets = available_presets()
    configurator = ProfileConfigurator(
        presets=presets,
        default_device="local-cpu",
    )
    label = configurator._label_for_value("lossy_block")
    configurator.profile_dropdown.value = label

    assert configurator.dimension_selector.value == 3
    assert configurator.row_input.value == 2
    assert configurator.col_input.value == 4
    assert configurator.layer_input.value == 2
    assert pytest.approx(1.5, rel=1e-6) == configurator.spacing_x_input.value
    assert pytest.approx(1.0, rel=1e-6) == configurator.spacing_y_input.value
    assert pytest.approx(1.0, rel=1e-6) == configurator.spacing_z_input.value


def test_render_measurement_configuration_is_3d():
    from neutral_atom_vm.display import render_measurement_configuration
    from neutral_atom_vm.layouts import GridLayout

    layout = GridLayout(dim=3, rows=2, cols=2, layers=2)
    measurement = {"bits": [1, 0, 0, 1, 1, 1, 0, 0]}
    html = render_measurement_configuration(measurement, layout, spacing=(1.0, 1.0, 0.5))

    assert html.startswith("<img")


def test_display_shot_exposes_matplotlib():
    pytest.importorskip("matplotlib")

    from neutral_atom_vm.display import display_shot
    from neutral_atom_vm.job import JobResult

    payload = {"measurements": [{"bits": [1, 0] * 8}]}
    result = JobResult(
        payload,
        device_id="device-foo",
        profile="noisy_square_array",
        shots=1,
    )

    fig, ax = display_shot(result, shot_index=0, figsize=(2.0, 2.0))

    assert hasattr(fig, "canvas")
    assert hasattr(ax, "scatter")
