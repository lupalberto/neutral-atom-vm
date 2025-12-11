"""Interactive widgets that help configure virtual machine profiles."""

from __future__ import annotations

from dataclasses import dataclass
from functools import partial
from html import escape
import math
import re
from collections import Counter
from typing import Any, Callable, Dict, Iterable, Mapping, MutableMapping, Sequence
from IPython.display import display
from IPython.display import display

from .device import available_presets
from .display import build_result_summary_html, format_histogram
from .layouts import GridLayout
from .service_client import RemoteServiceError, fetch_remote_device_catalog

try:  # pragma: no cover - exercised indirectly in widget tests
    import ipywidgets as widgets
except ModuleNotFoundError as exc:  # pragma: no cover - informative error at import time
    widgets = None  # type: ignore[assignment]
    _WIDGET_IMPORT_ERROR = exc
else:  # pragma: no cover - kept for clarity
    _WIDGET_IMPORT_ERROR = None

try:
    import plotly.graph_objects as go
except ModuleNotFoundError:
    go = None


@dataclass(frozen=True)
class _NoiseFieldSpec:
    path: str
    label: str
    group: str
    tooltip: str = ""


_NOISE_FIELD_SPECS: Sequence[_NoiseFieldSpec] = [
    _NoiseFieldSpec("p_loss", "Loss probability", "Global", "Probability that an atom disappears entirely."),
    _NoiseFieldSpec("p_quantum_flip", "Quantum flip probability", "Global", "Pauli error applied before a measurement."),
    _NoiseFieldSpec("idle_rate", "Idle depolarizing rate", "Global", "Depolarizing noise while idle."),
    _NoiseFieldSpec("readout.p_flip0_to_1", "P(0→1)", "Readout", "SPAM error: reading |1⟩ when |0⟩ was prepared."),
    _NoiseFieldSpec("readout.p_flip1_to_0", "P(1→0)", "Readout", "SPAM error: reading |0⟩ when |1⟩ was prepared."),
    _NoiseFieldSpec("gate.single_qubit.px", "PX", "Gate: single", "Pauli-X applied to single-qubit gates."),
    _NoiseFieldSpec("gate.single_qubit.py", "PY", "Gate: single", "Pauli-Y applied to single-qubit gates."),
    _NoiseFieldSpec("gate.single_qubit.pz", "PZ", "Gate: single", "Pauli-Z applied to single-qubit gates."),
    _NoiseFieldSpec("gate.two_qubit_control.px", "PX", "Gate: control", "Pauli-X applied to the control leg."),
    _NoiseFieldSpec("gate.two_qubit_control.py", "PY", "Gate: control", "Pauli-Y applied to the control leg."),
    _NoiseFieldSpec("gate.two_qubit_control.pz", "PZ", "Gate: control", "Pauli-Z applied to the control leg."),
    _NoiseFieldSpec("gate.two_qubit_target.px", "PX", "Gate: target", "Pauli-X applied to the target leg."),
    _NoiseFieldSpec("gate.two_qubit_target.py", "PY", "Gate: target", "Pauli-Y applied to the target leg."),
    _NoiseFieldSpec("gate.two_qubit_target.pz", "PZ", "Gate: target", "Pauli-Z applied to the target leg."),
    _NoiseFieldSpec("phase.single_qubit", "Single-qubit", "Phase noise", "Phase drift for single-qubit gates."),
    _NoiseFieldSpec("phase.two_qubit_control", "Control leg", "Phase noise", "Phase drift applied to the control leg."),
    _NoiseFieldSpec("phase.two_qubit_target", "Target leg", "Phase noise", "Phase drift applied to the target leg."),
    _NoiseFieldSpec("phase.idle", "Idle", "Phase noise", "Phase drift while idle."),
    _NoiseFieldSpec("amplitude_damping.per_gate", "Per gate", "Amplitude damping", "Population damping during gates."),
    _NoiseFieldSpec("amplitude_damping.idle_rate", "Idle rate", "Amplitude damping", "Population damping while idle."),
    _NoiseFieldSpec("loss_runtime.per_gate", "Per gate", "Runtime loss", "Atom loss injected by each gate."),
    _NoiseFieldSpec("loss_runtime.idle_rate", "Idle rate", "Runtime loss", "Atom loss accumulated while idle."),
]

_ROLE_OPTIONS = ("DATA", "ANCILLA", "PARKING", "CALIB")


def _format_number(value: float) -> str:
    formatted = f"{float(value):.12g}"
    if formatted == "-0":
        formatted = "0"
    if "." not in formatted and "e" not in formatted:
        formatted += ".0"
    return formatted


def _format_positions(values: Sequence[Any]) -> str:
    entries = _coerce_position_entries(values)
    if not entries:
        return ""
    lines: list[str] = []
    for entry in entries:
        if len(entry) == 1:
            lines.append(_format_number(entry[0]))
        else:
            lines.append(", ".join(_format_number(val) for val in entry))
    return "\n".join(lines)


def _coerce_position_entries(values: Sequence[Any]) -> list[tuple[float, ...]]:
    entries: list[tuple[float, ...]] = []
    for item in values or []:
        if isinstance(item, Sequence) and not isinstance(item, (str, bytes)):
            coords = tuple(float(val) for val in item)
            if coords:
                entries.append(coords)
        else:
            entries.append((float(item),))
    return entries


def _parse_position_entries(text: str) -> list[tuple[float, ...]]:
    stripped = text.strip()
    if not stripped:
        return []

    lines = [line.strip() for line in stripped.splitlines() if line.strip()]
    entries: list[tuple[float, ...]] = []

    if len(lines) <= 1:
        tokens = [tok for tok in re.split(r"[,\s]+", stripped) if tok]
        return [(float(tok),) for tok in tokens]

    chunks: list[str] = []
    for line in lines:
        for chunk in line.split(";"):
            chunk = chunk.strip()
            if chunk:
                chunks.append(chunk)

    for chunk in chunks:
        normalized = chunk.strip("()[]")
        tokens = [tok for tok in re.split(r"[,\s]+", normalized) if tok]
        try:
            coords = tuple(float(tok) for tok in tokens)
        except ValueError as exc:  # pragma: no cover - informative error path
            raise ValueError(f"Invalid position value: {chunk!r}") from exc
        if coords:
            entries.append(coords)
    return entries


def _site_descriptors_from_entries(entries: Sequence[tuple[float, ...]]) -> list[Dict[str, float]]:
    sites: list[Dict[str, float]] = []
    for idx, entry in enumerate(entries):
        x = float(entry[0]) if entry else 0.0
        y = float(entry[1]) if len(entry) > 1 else 0.0
        z = float(entry[2]) if len(entry) > 2 else 0.0
        sites.append({"id": idx, "x": x, "y": y, "z": z, "zone_id": 0})
    return sites


def _coords_from_sites(
    sites: Sequence[Mapping[str, Any]],
    layout_info: Mapping[str, Any] | None = None,
) -> list[list[float]]:
    if not sites:
        return []
    dim = 1
    if isinstance(layout_info, Mapping):
        dim = max(1, int(layout_info.get("dim", 1)))
    else:
        has_y = any(float(site.get("y", 0.0)) != 0.0 for site in sites)
        has_z = any(float(site.get("z", 0.0)) != 0.0 for site in sites)
        if has_z:
            dim = 3
        elif has_y:
            dim = 2
    coords: list[list[float]] = []
    for site in sites:
        x = float(site.get("x", 0.0))
        row: list[float] = [x]
        if dim >= 2:
            row.append(float(site.get("y", 0.0)))
        if dim >= 3:
            row.append(float(site.get("z", 0.0)))
        coords.append(row)
    return coords


def _format_matrix(matrix: Sequence[Sequence[float]] | None) -> str:
    if not matrix:
        return ""
    lines = []
    for row in matrix:
        cleaned = ", ".join(_format_number(float(v)) for v in row)
        lines.append(cleaned)
    return "\n".join(lines)


def _parse_matrix(text: str) -> list[list[float]] | None:
    stripped = text.strip()
    if not stripped:
        return None
    rows: list[list[float]] = []
    for line in stripped.splitlines():
        tokens = re.split(r"[,\s]+", line.strip())
        row: list[float] = []
        for token in tokens:
            if not token:
                continue
            try:
                row.append(float(token))
            except ValueError as exc:  # pragma: no cover - informative error path
                raise ValueError(f"Invalid correlated-gate entry: {token!r}") from exc
        if row:
            rows.append(row)
    return rows or None


def _extract_noise_value(noise: Mapping[str, Any] | None, path: str) -> float:
    if not noise:
        return 0.0
    current: Any = noise
    keys = path.split(".")
    for key in keys:
        if not isinstance(current, Mapping):
            return 0.0
        current = current.get(key)
        if current is None:
            return 0.0
    try:
        return float(current)
    except (TypeError, ValueError):
        return 0.0


def _assign_nested(mapping: MutableMapping[str, Any], path: str, value: float) -> None:
    keys = path.split(".")
    target: MutableMapping[str, Any] = mapping
    for key in keys[:-1]:
        nxt = target.get(key)
        if not isinstance(nxt, MutableMapping):
            nxt = {}
            target[key] = nxt
        target = nxt
    target[keys[-1]] = value


def _coerce_to_str(value: Any) -> str | None:
    if value is None:
        return None
    try:
        return str(value)
    except Exception:
        return None


def _format_metadata(metadata: Mapping[str, Any] | None) -> str:
    if not metadata:
        return "<em>No additional metadata for this profile.</em>"
    lines: list[str] = []
    label = metadata.get("label")
    persona = metadata.get("persona")
    description = metadata.get("description")
    geometry = metadata.get("geometry")
    noise_behavior = metadata.get("noise_behavior")
    if label:
        heading = escape(str(label))
        if persona:
            heading += f" &middot; <small>{escape(str(persona))}</small>"
        lines.append(f"<strong>{heading}</strong>")
    elif persona:
        lines.append(f"<strong>{escape(str(persona))}</strong>")
    if description:
        lines.append(escape(str(description)))
    if geometry:
        lines.append(f"<b>Geometry:</b> {escape(str(geometry))}")
    if noise_behavior:
        lines.append(f"<b>Noise:</b> {escape(str(noise_behavior))}")
    return "<br/>".join(lines) if lines else "<em>No additional metadata for this profile.</em>"


_CUSTOM_PROFILE_VALUE = "__custom_profile__"


class ProfileConfigurator:
    """High-level helper that renders an ipywidgets UI for profile selection."""

    CUSTOM_PROFILE_LABEL = "(Create new profile)"
    CUSTOM_CONFIGURATION_LABEL = "(Create new configuration)"
    _CUSTOM_CONFIGURATION_VALUE = "__custom_configuration__"

    def __init__(
        self,
        *,
        presets: Mapping[str, Mapping[Any, Mapping[str, Any]]] | None = None,
        service_url: str | None = None,
        devices_endpoint: str = "/devices",
        service_timeout: float = 10.0,
        default_device: str | None = None,
        default_profile: str | None = None,
        default_profile_payload: Mapping[str, Any] | None = None,
    ) -> None:
        if widgets is None:  # pragma: no cover - executed when ipywidgets is missing
            raise RuntimeError(
                "ProfileConfigurator requires ipywidgets. Please install the 'ipywidgets' extra."
            ) from _WIDGET_IMPORT_ERROR

        if service_url:
            self._presets = self._load_presets_from_service(
                service_url,
                devices_endpoint,
                service_timeout,
            )
        else:
            self._presets = presets or available_presets()
        if not self._presets:
            raise ValueError("No device presets available to populate the configurator.")

        self._service_url = service_url
        self._devices_endpoint = devices_endpoint
        self._service_timeout = service_timeout

        self._profile_label_to_value: Dict[str, Any] = {}
        self._suspend_profile_callback = False
        self._default_device = default_device
        self._initial_profile_value: Any | None = default_profile
        self._initial_profile_payload = dict(default_profile_payload) if default_profile_payload else None
        if self._initial_profile_payload:
            self._initial_profile_value = _CUSTOM_PROFILE_VALUE
        self._initial_profile_consumed = self._initial_profile_value is None

        device_options = sorted(self._presets.keys())
        device_default = default_device if default_device in device_options else device_options[0]
        self.device_dropdown = widgets.Dropdown(
            description="Device",
            options=device_options,
            value=device_default,
            layout=widgets.Layout(width="280px"),
        )
        self.profile_dropdown = widgets.Dropdown(
            description="Profile",
            options=[],
            layout=widgets.Layout(width="280px"),
        )
        self.configuration_dropdown = widgets.Dropdown(
            description="Configuration",
            options=[],
            layout=widgets.Layout(width="320px"),
            disabled=True,
        )
        self.configuration_dropdown.observe(self._on_configuration_change, names="value")
        self._suspend_configuration_callback = False
        self.custom_profile_name = widgets.Text(
            description="Profile name",
            placeholder="my_profile",
            layout=widgets.Layout(width="280px"),
            style={"description_width": "120px"},
        )
        self.dimension_selector = widgets.Dropdown(
            description="Dimensions",
            options=[1, 2, 3],
            value=2,
            layout=widgets.Layout(width="180px"),
            style={"description_width": "100px"},
        )
        self.slot_count_input = widgets.BoundedIntText(
            description="Slots",
            min=1,
            max=512,
            value=8,
            layout=widgets.Layout(width="200px"),
            style={"description_width": "80px"},
        )
        self.row_input = widgets.BoundedIntText(
            description="Rows",
            min=1,
            max=64,
            value=4,
            layout=widgets.Layout(width="160px"),
            style={"description_width": "80px"},
        )
        self.col_input = widgets.BoundedIntText(
            description="Cols",
            min=1,
            max=64,
            value=4,
            layout=widgets.Layout(width="160px"),
            style={"description_width": "80px"},
        )
        self.layer_input = widgets.BoundedIntText(
            description="Layers",
            min=1,
            max=32,
            value=1,
            layout=widgets.Layout(width="160px"),
            style={"description_width": "80px"},
        )
        self.generate_positions_button = widgets.Button(
            description="Populate positions",
            tooltip="Generate evenly spaced positions using the current layout.",
            layout=widgets.Layout(width="200px"),
        )
        self.spacing_x_input = widgets.FloatText(
            description="Spacing X",
            value=1.0,
            step=0.1,
            layout=widgets.Layout(width="200px"),
            style={"description_width": "100px"},
        )
        self.spacing_y_input = widgets.FloatText(
            description="Spacing Y",
            value=1.0,
            step=0.1,
            layout=widgets.Layout(width="200px"),
            style={"description_width": "100px"},
        )
        self.spacing_z_input = widgets.FloatText(
            description="Spacing Z",
            value=1.0,
            step=0.1,
            layout=widgets.Layout(width="200px"),
            style={"description_width": "100px"},
        )
        self.generate_positions_button.on_click(lambda _: self._generate_positions())
        self.dimension_selector.observe(self._on_dimension_change, names="value")
        self.blockade_input = widgets.FloatText(
            description="Blockade radius",
            step=0.1,
            layout=widgets.Layout(width="260px"),
            style={"description_width": "140px"},
        )
        self.positions_editor = widgets.Textarea(
            description="Positions",
            placeholder="Comma or newline separated floats",
            layout=widgets.Layout(width="100%", height="120px"),
            style={"description_width": "120px"},
        )
        self.positions_help = widgets.HTML(
            value=(
                "<small>Example: 0.0, 1.0, 2.0 or one value per line. "
                "For multi-dimensional layouts, list each slot as a comma- or space-separated tuple "
                "(e.g., `0 0`, `1 0`, `2 0`, `3 0`, `0 1`, ... for the first rows of a 4x4 grid). "
                "The configurator keeps the legacy `positions` list while emitting richer `coordinates`, "
                "`sites`, and `site_ids` so the backend understands the lattice plus slot mapping.</small>"
            ),
            layout=widgets.Layout(margin="0 0 0 8px"),
        )
        self.metadata_html = widgets.HTML(
            layout=widgets.Layout(min_height="48px", padding="4px", border="1px solid #ddd"),
        )
        self.geometry_summary = widgets.HTML(
            layout=widgets.Layout(min_height="48px", padding="4px", border="none"),
        )
        self.zone_summary = widgets.HTML(
            layout=widgets.Layout(min_height="32px", padding="4px", border="none"),
        )
        self.distance_html = widgets.HTML(
            value="<em>Distance data unavailable.</em>",
            layout=widgets.Layout(min_height="24px", padding="4px 0", border="none"),
        )
        self._plotly_available = go is not None
        self.geometry_plot = widgets.Output(
            layout=widgets.Layout(min_height="0", height="100%", padding="8px 0", width="100%", flex="1", overflow="hidden"),
        )
        self._last_geometry_annotations: list[str] = []
        self._last_geometry_figure: Any | None = None
        self.warning_html = widgets.HTML(
            value="<em>No warnings.</em>",
            layout=widgets.Layout(min_height="32px", padding="4px 0", border="none"),
        )

        self.geometry_preview_toggle = widgets.ToggleButton(
            description="Show preview",
            value=True,
            button_style="info",
            tooltip="Toggle the geometry preview",
            layout=widgets.Layout(width="120px"),
        )
        self.geometry_preview_toggle.observe(self._on_preview_toggle, names="value")

        self.geometry_help_button = widgets.Button(
            description="Show geometry help",
            tooltip="Explain how the geometry tabs and controls relate to the device lattice.",
            button_style="info",
            layout=widgets.Layout(width="170px"),
        )
        self.geometry_help_button.on_click(lambda _: self._toggle_geometry_help())
        self.geometry_help_output = widgets.HTML(
            value=(
                "<p><strong>Physical lattice tab</strong> shows the canonical device lattice and the "
                "region/zone-aware occupancy controls. Use the row/column/perimeter tools and the "
                "region palette to toggle which traps are occupied without editing raw indices.</p>"
                "<p><strong>Positions tab</strong> retains the legacy slot count, spacing, and timing "
                "controls plus the raw positions textarea. Generate evenly spaced coordinates or "
                "edit them directly; the configurator will keep the derived site metadata for "
                "backward compatibility.</p>"
            ),
            layout=widgets.Layout(
                display="none",
                padding="8px",
                margin="0 0 8px 0",
                border="1px solid #ddd",
            ),
        )
        self._geometry_help_visible = False
        self.row_selector = widgets.Dropdown(
            description="Row",
            options=[],
            layout=widgets.Layout(width="130px"),
            disabled=True,
        )
        self.select_row_button = widgets.Button(
            description="Toggle row",
            layout=widgets.Layout(width="130px"),
        )
        self.col_selector = widgets.Dropdown(
            description="Column",
            options=[],
            layout=widgets.Layout(width="130px"),
            disabled=True,
        )
        self.select_col_button = widgets.Button(
            description="Toggle column",
            layout=widgets.Layout(width="130px"),
        )
        self.perimeter_button = widgets.Button(
            description="Toggle perimeter",
            layout=widgets.Layout(width="200px"),
        )
        self.clear_button = widgets.Button(
            description="Clear occupancy",
            button_style="warning",
            layout=widgets.Layout(width="160px"),
        )
        self.fill_button = widgets.Button(
            description="Fill all sites",
            button_style="success",
            layout=widgets.Layout(width="160px"),
        )
        self.region_role_dropdown = widgets.Dropdown(
            description="Role",
            options=[(option.capitalize(), option) for option in _ROLE_OPTIONS],
            value="DATA",
            layout=widgets.Layout(width="200px"),
        )
        self.region_assign_button = widgets.Button(
            description="Assign role",
            layout=widgets.Layout(width="150px"),
        )
        self.select_row_button.on_click(lambda _: self._toggle_row_selection())
        self.clear_button.on_click(lambda _: self._set_current_site_ids(set()))
        self.fill_button.on_click(lambda _: self._set_current_site_ids(self._all_site_ids()))
        self.region_assign_button.on_click(lambda _: self._toggle_role_assignment_mode())
        self.layer_selector = widgets.Dropdown(
            description="Layer",
            options=[],
            layout=widgets.Layout(width="130px"),
            disabled=True,
        )
        self.select_layer_button = widgets.Button(
            description="Toggle layer",
            layout=widgets.Layout(width="130px"),
        )
        self.select_layer_button.on_click(lambda _: self._toggle_layer_selection())
        self.lattice_grid = widgets.GridBox(
            layout=widgets.Layout(
                width="100%",
                grid_template_columns="repeat(5, minmax(40px, 1fr))",
                grid_gap="4px",
                overflow_x="hidden",
                overflow_y="hidden",
            ),
        )
        self.region_palette = widgets.VBox(layout=widgets.Layout(gap="4px"))
        self._configuration_families: Dict[str, Mapping[str, Any]] = {}
        self._region_definitions: list[Dict[str, Any]] = []
        self._base_region_definitions: list[Dict[str, Any]] = []
        self._preset_sites: list[Mapping[str, Any]] = []
        self._site_layout_info: Mapping[str, Any] | None = None
        self._current_site_ids: set[int] = set()
        self._custom_configuration_active = False
        self._suspend_region_callbacks = False
        self._role_assignment_mode = False
        self._pending_role_selection: set[int] = set()
        self._last_selection_site_ids: set[int] = set()
        self._custom_region_counter = 0

        self.noise_fields: Dict[str, widgets.FloatText] = {}
        self.correlated_matrix = widgets.Textarea(
            description="CZ matrix",
            placeholder="Rows of comma-separated probabilities",
            layout=widgets.Layout(width="100%", height="110px"),
            style={"description_width": "120px"},
        )

        spacing_box = widgets.HBox(
            [
                self.spacing_x_input,
                self.spacing_y_input,
                self.spacing_z_input,
            ],
            layout=widgets.Layout(),
        )
        dimension_box = widgets.HBox(
            [
                self.dimension_selector,
                self.slot_count_input,
                self.row_input,
                self.col_input,
                self.layer_input,
                self.generate_positions_button,
            ],
            layout=widgets.Layout(),
        )
        selection_controls = widgets.VBox(
            [
                widgets.HBox(
                    [
                        self.row_selector,
                        self.select_row_button,
                        self.col_selector,
                        self.select_col_button,
                        self.layer_selector,
                        self.select_layer_button,
                    ],
                    layout=widgets.Layout(gap="6px"),
                ),
                widgets.HBox(
                    [
                        self.perimeter_button,
                        self.clear_button,
                        self.fill_button,
                    ],
                    layout=widgets.Layout(gap="6px"),
                ),
                widgets.HBox(
                    [
                        self.region_role_dropdown,
                        self.region_assign_button,
                    ],
                    layout=widgets.Layout(gap="6px"),
                ),
            ],
            layout=widgets.Layout(margin="8px 0", gap="4px"),
        )
        map_column = widgets.VBox(
            [
                selection_controls,
                self.lattice_grid,
            ],
            layout=widgets.Layout(flex="2", min_width="0"),
        )
        self.preview_column = widgets.VBox(
            [
                self.geometry_plot,
            ],
            layout=widgets.Layout(
                min_width="260px",
                width="100%",
                height="100%",
                display="flex",
                flex_flow="column",
                align_items="center",
                justify_content="center",
                overflow="hidden",
            ),
        )
        map_preview_row = widgets.Box(
            [map_column, self.preview_column],
            layout=widgets.Layout(
                display="grid",
                grid_template_columns="minmax(0, 2fr) minmax(260px, 1fr)",
                gap="12px",
                align_items="stretch",
                padding="8px",
            ),
        )
        preview_toggle_row = widgets.HBox(
            [self.geometry_preview_toggle],
            layout=widgets.Layout(justify_content="flex-end"),
        )
        self._apply_preview_visibility()
        geometry_map_box = widgets.VBox(
            [
                self.geometry_summary,
                self.zone_summary,
                self.distance_html,
                self.warning_html,
                preview_toggle_row,
                map_preview_row,
                widgets.HTML("<strong>Regions</strong>"),
                self.region_palette,
            ],
            layout=widgets.Layout(padding="4px", border="1px solid #ddd", flex="1"),
        )
        detail_panel = widgets.VBox(
            [
                self.blockade_input,
                dimension_box,
                spacing_box,
                self.positions_editor,
                self.positions_help,
            ],
            layout=widgets.Layout(padding="4px", border="1px solid #ddd", flex="1"),
        )
        lattice_tab_header = widgets.HBox(
            [
                widgets.HTML("<strong>Physical lattice</strong>"),
                self.geometry_help_button,
            ],
            layout=widgets.Layout(justify_content="space-between", align_items="center"),
        )
        lattice_tab = widgets.VBox(
            [
                lattice_tab_header,
                self.geometry_help_output,
                geometry_map_box,
            ],
            layout=widgets.Layout(padding="4px", gap="6px"),
        )
        positions_tab = widgets.VBox(
            [
                widgets.HTML("<strong>Positions & timing</strong>"),
                detail_panel,
            ],
            layout=widgets.Layout(padding="4px", gap="6px"),
        )
        self.geometry_subtabs = widgets.Tab(children=[lattice_tab, positions_tab])
        self.geometry_subtabs.set_title(0, "Physical lattice")
        self.geometry_subtabs.set_title(1, "Positions")
        geometry_box = widgets.VBox(
            [
                self.geometry_subtabs,
            ],
            layout=widgets.Layout(),
        )
        noise_editor = self._build_noise_editor()
        tabs = widgets.Tab(children=[geometry_box, noise_editor])
        tabs.set_title(0, "Geometry")
        tabs.set_title(1, "Noise")

        self.custom_profile_box = widgets.VBox(
            [
                widgets.HTML("<strong>Custom profile</strong>"),
                self.custom_profile_name,
                widgets.HTML(
                    "<em>Use slot count + spacing to seed positions, optionally comment each slot with `x y` coordinates before tweaking manually.</em>"
                ),
            ],
            layout=widgets.Layout(display="none", padding="8px", border="1px dashed #bbb"),
        )

        self.container = widgets.VBox(
            [
                widgets.HBox(
                    [self.device_dropdown, self.profile_dropdown, self.configuration_dropdown],
                    layout=widgets.Layout(justify_content="flex-start"),
                ),
                self.custom_profile_box,
                self.metadata_html,
                tabs,
            ],
            layout=widgets.Layout(width="100%", min_width="0"),
        )

        self._update_dimension_layout()
        self.device_dropdown.observe(self._on_device_change, names="value")
        self.profile_dropdown.observe(self._on_profile_change, names="value")
        self._refresh_profile_options(self.device_dropdown.value)

    def _build_noise_editor(self) -> widgets.Widget:
        grouped: Dict[str, list[widgets.FloatText]] = {}
        for spec in _NOISE_FIELD_SPECS:
            field = widgets.FloatText(
                description=spec.label,
                step=0.001,
                layout=widgets.Layout(width="220px"),
                style={"description_width": "120px"},
                tooltip=spec.tooltip,
            )
            self.noise_fields[spec.path] = field
            grouped.setdefault(spec.group, []).append(field)

        accordion_children = []
        titles: list[str] = []
        for group_name, fields in grouped.items():
            grid = widgets.GridBox(
                children=fields,
                layout=widgets.Layout(
                    grid_template_columns="repeat(2, minmax(200px, 1fr))",
                    grid_gap="6px 18px",
                ),
            )
            accordion_children.append(grid)
            titles.append(group_name)

        accordion = widgets.Accordion(children=accordion_children)
        for idx, title in enumerate(titles):
            accordion.set_title(idx, title)

        return widgets.VBox(
            [
                accordion,
                widgets.HTML(
                    "<strong>Correlated CZ noise</strong><br/><small>Provide a matrix with one row per line.</small>"
                ),
                self.correlated_matrix,
            ]
        )

    def _on_device_change(self, change: Dict[str, Any]) -> None:
        new_value = change.get("new")
        if new_value:
            self._refresh_profile_options(str(new_value))

    def _on_profile_change(self, change: Dict[str, Any]) -> None:
        if self._suspend_profile_callback:
            return
        new_value = change.get("new")
        if new_value is None:
            return
        actual = self._profile_label_to_value.get(str(new_value), str(new_value))
        self._apply_profile(self.device_dropdown.value, actual)

    def _label_for_profile(self, profile: Any) -> str:
        if profile is None:
            return "(default)"
        return str(profile)

    def _label_for_value(self, profile: Any) -> str:
        for label, value in self._profile_label_to_value.items():
            if value == profile:
                return label
        return next(iter(self._profile_label_to_value), str(profile))

    def _choose_preferred_profile(self, device_id: str, ordered_profiles: Sequence[Any]) -> Any:
        best_profile = ordered_profiles[0]
        best_priority = self._profile_priority(device_id, best_profile)
        for profile in ordered_profiles[1:]:
            priority = self._profile_priority(device_id, profile)
            if priority < best_priority:
                best_profile = profile
                best_priority = priority
        return best_profile

    def _profile_priority(self, device_id: str, profile: Any) -> tuple[int, int, int, str]:
        config = self._presets.get(device_id, {}).get(profile, {})
        metadata = config.get("metadata") or {}
        persona = str(metadata.get("persona", "")).lower()
        label = str(metadata.get("label", "")).lower()
        name = "" if profile is None else str(profile).lower()
        return (
            0 if "education" in persona else 1,
            0 if "tutorial" in label or "tutorial" in name else 1,
            0 if "ideal" in label or "ideal" in name else 1,
            name,
        )

    def _refresh_profile_options(self, device_id: str) -> None:
        profiles = self._presets.get(device_id, {})
        if not profiles:
            self.profile_dropdown.options = []
            self.metadata_html.value = "<em>No profiles found for this device.</em>"
            return

        sorted_profiles = sorted(profiles.keys(), key=lambda p: "" if p is None else str(p))
        labels = [self._label_for_profile(p) for p in sorted_profiles]
        self._profile_label_to_value = dict(zip(labels, sorted_profiles))
        labels.append(self.CUSTOM_PROFILE_LABEL)
        self._profile_label_to_value[self.CUSTOM_PROFILE_LABEL] = _CUSTOM_PROFILE_VALUE

        preferred_profile = self._choose_preferred_profile(device_id, sorted_profiles)
        if (not self._initial_profile_consumed) and self._initial_profile_value is not None:
            candidate = self._initial_profile_value
            if candidate == _CUSTOM_PROFILE_VALUE or candidate in sorted_profiles:
                preferred_profile = candidate
            self._initial_profile_consumed = True
        preferred_label = self._label_for_value(preferred_profile)

        self._suspend_profile_callback = True
        try:
            self.profile_dropdown.options = labels
            self.profile_dropdown.value = preferred_label
        finally:
            self._suspend_profile_callback = False

        self._apply_profile(device_id, preferred_profile)

    def _apply_profile(self, device_id: str, profile: Any) -> None:
        if profile == _CUSTOM_PROFILE_VALUE:
            self._enter_custom_mode()
            if self._initial_profile_payload:
                self._load_custom_payload(self._initial_profile_payload)
                self._initial_profile_payload = None
            return

        self._exit_custom_mode()
        config = self._presets.get(device_id, {}).get(profile)
        if not config:
            return
        self._load_configuration_metadata(config)
        self._apply_grid_layout_info(config.get("grid_layout"))
        self.blockade_input.value = float(config.get("blockade_radius", 0.0))
        coords = None
        sites = config.get("sites")
        if isinstance(sites, Sequence):
            coords = _coords_from_sites(sites, config.get("grid_layout"))
        elif config.get("coordinates"):
            coords = config.get("coordinates")
        self.positions_editor.value = _format_positions(coords if coords else config.get("positions", []))
        self.metadata_html.value = _format_metadata(config.get("metadata"))
        self._load_noise(config.get("noise"))
        self._refresh_geometry_panel()

    def _load_configuration_metadata(self, config: Mapping[str, Any], *, custom_mode: bool = False) -> None:
        sites = config.get("sites")
        if isinstance(sites, Sequence):
            self._preset_sites = [dict(site) for site in sites if isinstance(site, Mapping)]
        else:
            self._preset_sites = []
        layout_info = config.get("grid_layout")
        self._site_layout_info = layout_info if isinstance(layout_info, Mapping) else None
        self._base_region_definitions = self._normalize_regions(config.get("regions"))
        self._region_definitions = self._clone_regions(self._base_region_definitions)
        if custom_mode:
            self._configuration_families = {}
            self._custom_configuration_active = True
            self._active_configuration_family_name = self._CUSTOM_CONFIGURATION_VALUE
            self._current_site_ids = set(self._coerce_site_ids(config.get("site_ids")))
            if not self._current_site_ids and self._preset_sites:
                self._current_site_ids = {
                    int(site.get("id", idx)) for idx, site in enumerate(self._preset_sites)
                }
            self._last_selection_site_ids = set(self._current_site_ids)
            return

        self._configuration_families = self._normalize_configuration_families(
            config.get("configuration_families"),
        )
        self._custom_configuration_active = False
        default_family = _coerce_to_str(config.get("default_configuration_family"))
        if not default_family and self._configuration_families:
            default_family = next(iter(self._configuration_families))
        self._active_configuration_family_name = default_family
        if self._configuration_families and self._active_configuration_family_name in self._configuration_families:
            self._current_site_ids = set(
                self._configuration_families[self._active_configuration_family_name]["site_ids"]
            )
        else:
            self._current_site_ids = set(self._coerce_site_ids(config.get("site_ids")))
        if not self._current_site_ids and self._preset_sites:
            self._current_site_ids = {
                int(site.get("id", idx)) for idx, site in enumerate(self._preset_sites)
            }
        self._apply_active_configuration_family_regions()
        self._update_configuration_dropdown()
        self._last_selection_site_ids = set(self._current_site_ids)

    def _normalize_regions(self, entries: Any) -> list[Dict[str, Any]]:
        regions: list[Dict[str, Any]] = []
        if not isinstance(entries, Sequence) or isinstance(entries, (str, bytes)):
            return regions
        for entry in entries:
            if not isinstance(entry, Mapping):
                continue
            site_ids = self._coerce_site_ids(entry.get("site_ids"))
            if not site_ids:
                continue
            role = str(entry.get("role", "DATA")).upper()
            if role not in {"DATA", "ANCILLA", "PARKING", "CALIB"}:
                role = "DATA"
            regions.append(
                {
                    "name": str(entry.get("name", "region")),
                    "site_ids": site_ids,
                    "role": role,
                    "zone_id": entry.get("zone_id"),
                }
            )
        return regions

    def _coerce_site_ids(self, entries: Any) -> list[int]:
        if not isinstance(entries, Sequence) or isinstance(entries, (str, bytes)):
            return []
        ids: list[int] = []
        for entry in entries:
            try:
                ids.append(int(entry))
            except (TypeError, ValueError):
                continue
        return ids

    def _normalize_configuration_families(self, entries: Any) -> Dict[str, Mapping[str, Any]]:
        families: Dict[str, Mapping[str, Any]] = {}
        if not isinstance(entries, Mapping):
            return families
        for name, payload in entries.items():
            if not isinstance(payload, Mapping):
                continue
            site_ids = self._coerce_site_ids(payload.get("site_ids"))
            if not site_ids:
                continue
            family: Dict[str, Any] = {
                "site_ids": site_ids,
                "regions": self._normalize_regions(payload.get("regions")),
                "description": _coerce_to_str(payload.get("description")),
            }
            families[str(name)] = family
        return families

    def _clone_regions(self, entries: Sequence[Mapping[str, Any]] | None) -> list[Dict[str, Any]]:
        clones: list[Dict[str, Any]] = []
        if not entries:
            return clones
        for entry in entries:
            if not isinstance(entry, Mapping):
                continue
            clone = dict(entry)
            clone["site_ids"] = self._coerce_site_ids(entry.get("site_ids"))
            clones.append(clone)
        return clones

    def _apply_active_configuration_family_regions(self) -> None:
        if self._custom_configuration_active:
            return
        regions_source: Sequence[Mapping[str, Any]] | None = self._base_region_definitions
        if (
            self._active_configuration_family_name
            and self._active_configuration_family_name in self._configuration_families
        ):
            family_regions = self._configuration_families[self._active_configuration_family_name].get(
                "regions"
            )
            if family_regions:
                regions_source = family_regions
        self._region_definitions = self._clone_regions(regions_source)

    def _activate_custom_configuration(self) -> None:
        if self._custom_configuration_active and self._active_configuration_family_name == self._CUSTOM_CONFIGURATION_VALUE:
            return
        self._custom_configuration_active = True
        self._active_configuration_family_name = self._CUSTOM_CONFIGURATION_VALUE
        self._update_configuration_dropdown()

    def _update_configuration_dropdown(self) -> None:
        options: list[tuple[str, Any]] = []
        for name, family in self._configuration_families.items():
            label_parts = [name]
            description = family.get("description")
            if description:
                label_parts.insert(0, description)
            site_ids = family.get("site_ids") or []
            count = len(site_ids)
            label = f"{' - '.join(label_parts)} ({count} sites)"
            options.append((label, name))
        if options:
            options.append((self.CUSTOM_CONFIGURATION_LABEL, self._CUSTOM_CONFIGURATION_VALUE))
            desired_value = (
                self._active_configuration_family_name
                if self._active_configuration_family_name in self._configuration_families
                else self._CUSTOM_CONFIGURATION_VALUE
            )
            self.configuration_dropdown.disabled = False
        else:
            options = [(self.CUSTOM_CONFIGURATION_LABEL, self._CUSTOM_CONFIGURATION_VALUE)]
            desired_value = self._CUSTOM_CONFIGURATION_VALUE
            self.configuration_dropdown.disabled = True
        self._suspend_configuration_callback = True
        try:
            self.configuration_dropdown.options = options
            self.configuration_dropdown.value = desired_value
        finally:
            self._suspend_configuration_callback = False

    def _on_configuration_change(self, change: Dict[str, Any]) -> None:
        if self._suspend_configuration_callback:
            return
        new_value = change.get("new")
        if new_value is None:
            return
        if new_value == self._CUSTOM_CONFIGURATION_VALUE:
            base_family = self._active_configuration_family_name
            if base_family and base_family in self._configuration_families:
                self._current_site_ids = set(
                    self._configuration_families[base_family].get("site_ids", [])
                )
            self._activate_custom_configuration()
        else:
            self._custom_configuration_active = False
            self._active_configuration_family_name = str(new_value)
            if self._active_configuration_family_name in self._configuration_families:
                self._current_site_ids = set(
                    self._configuration_families[self._active_configuration_family_name]["site_ids"]
                )
            self._apply_active_configuration_family_regions()
        self._refresh_geometry_panel()

    def _refresh_geometry_panel(self) -> None:
        self.geometry_summary.value = self._geometry_summary_text()
        self._update_layout_selectors()
        self._update_zone_summary()
        self._update_distance_summary()
        self._update_warnings()
        self._update_geometry_plot()
        self._apply_preview_visibility()
        self._render_lattice_map()
        self._render_region_palette()

    def _geometry_summary_text(self) -> str:
        total_sites = len(self._preset_sites)
        occupied = len(self._current_site_ids)
        family_label = (
            "custom configuration"
            if self._custom_configuration_active
            else (self._active_configuration_family_name or "default")
        )
        return (
            f"<strong>Configuration:</strong> {occupied}/{total_sites or '?'} occupied "
            f"sites ({family_label})."
        )

    def _update_layout_selectors(self) -> None:
        info = self._site_layout_info
        layers = max(1, int(info.get("layers", 1))) if info else 1
        if not info:
            self.row_selector.options = []
            self.col_selector.options = []
            self.layer_selector.options = []
            self.row_selector.disabled = True
            self.col_selector.disabled = True
            self.layer_selector.disabled = True
            self.select_row_button.disabled = True
            self.select_col_button.disabled = True
            self.perimeter_button.disabled = True
            self.clear_button.disabled = not bool(self._current_site_ids)
            self.fill_button.disabled = not bool(self._preset_sites)
            return
        rows = max(1, int(info.get("rows", 1)))
        cols = max(1, int(info.get("cols", 1)))
        layers = max(1, int(info.get("layers", 1)))
        self.row_selector.options = list(range(rows))
        self.col_selector.options = list(range(cols))
        self.layer_selector.options = list(range(layers))
        if self.row_selector.options and (self.row_selector.value not in self.row_selector.options):
            self.row_selector.value = self.row_selector.options[0]
        if self.col_selector.options and (self.col_selector.value not in self.col_selector.options):
            self.col_selector.value = self.col_selector.options[0]
        if self.layer_selector.options and (self.layer_selector.value not in self.layer_selector.options):
            self.layer_selector.value = self.layer_selector.options[0]
        self.row_selector.disabled = False
        self.col_selector.disabled = False
        self.layer_selector.disabled = False
        self.select_row_button.disabled = False
        self.select_col_button.disabled = False
        self.select_layer_button.disabled = False
        self.perimeter_button.disabled = not bool(self._perimeter_site_ids())
        self.clear_button.disabled = not bool(self._current_site_ids)
        self.fill_button.disabled = not bool(self._preset_sites)

    def _toggle_row_selection(self) -> None:
        if self.row_selector.value is None:
            return
        ids = self._row_site_ids(int(self.row_selector.value))
        self._select_or_toggle_sites(ids)

    def _toggle_column_selection(self) -> None:
        if self.col_selector.value is None:
            return
        ids = self._column_site_ids(int(self.col_selector.value))
        self._select_or_toggle_sites(ids)

    def _toggle_perimeter_selection(self) -> None:
        ids = self._perimeter_site_ids()
        self._select_or_toggle_sites(ids)

    def _toggle_layer_selection(self) -> None:
        if self.layer_selector.value is None:
            return
        ids = self._layer_site_ids(int(self.layer_selector.value))
        self._select_or_toggle_sites(ids)

    def _handle_site_click(self, site_id: int, _: widgets.Button) -> None:
        self._select_or_toggle_sites({site_id})

    def _select_or_toggle_sites(self, site_ids: Iterable[int]) -> None:
        if self._role_assignment_mode:
            self._select_sites_for_role(site_ids)
        else:
            self._toggle_sites(site_ids)

    def _toggle_geometry_help(self) -> None:
        self._geometry_help_visible = not self._geometry_help_visible
        self.geometry_help_output.layout.display = (
            "block" if self._geometry_help_visible else "none"
        )
        self.geometry_help_button.description = (
            "Hide geometry help" if self._geometry_help_visible else "Show geometry help"
        )

    def _toggle_sites(self, site_ids: Iterable[int]) -> None:
        if not site_ids:
            return
        site_set = set(site_ids)
        if site_set.issubset(self._current_site_ids):
            self._current_site_ids -= site_set
        else:
            self._current_site_ids.update(site_set)
        self._record_assignment_selection(site_set)
        self._activate_custom_configuration()
        self._refresh_geometry_panel()

    def _select_sites_for_role(self, site_ids: Iterable[int]) -> None:
        site_set = {site_id for site_id in site_ids if isinstance(site_id, int)}
        if not site_set:
            return
        added = False
        for site_id in site_set:
            if site_id not in self._current_site_ids:
                self._current_site_ids.add(site_id)
                added = True
        self._record_assignment_selection(site_set)
        if added:
            self._activate_custom_configuration()
        self._refresh_geometry_panel()

    def _set_current_site_ids(self, site_ids: set[int]) -> None:
        self._current_site_ids = set(site_ids)
        self._record_assignment_selection(site_ids)
        self._activate_custom_configuration()
        self._refresh_geometry_panel()

    def _all_site_ids(self) -> set[int]:
        return {
            int(site.get("id", idx))
            for idx, site in enumerate(self._preset_sites)
            if isinstance(site, Mapping)
        }

    def _row_site_ids(self, row: int) -> list[int]:
        info = self._site_layout_info
        if not info:
            return []
        rows = max(1, int(info.get("rows", 1)))
        cols = max(1, int(info.get("cols", 1)))
        layers = max(1, int(info.get("layers", 1)))
        if row < 0 or row >= rows:
            return []
        ids: list[int] = []
        stride = rows * cols
        for layer in range(layers):
            for col in range(cols):
                idx = layer * stride + row * cols + col
                if idx < len(self._preset_sites):
                    ids.append(int(self._preset_sites[idx].get("id", idx)))
        return ids

    def _column_site_ids(self, col: int) -> list[int]:
        info = self._site_layout_info
        if not info:
            return []
        rows = max(1, int(info.get("rows", 1)))
        cols = max(1, int(info.get("cols", 1)))
        layers = max(1, int(info.get("layers", 1)))
        if col < 0 or col >= cols:
            return []
        ids: list[int] = []
        stride = rows * cols
        for layer in range(layers):
            for row in range(rows):
                idx = layer * stride + row * cols + col
                if idx < len(self._preset_sites):
                    ids.append(int(self._preset_sites[idx].get("id", idx)))
        return ids

    def _layer_site_ids(self, layer: int) -> list[int]:
        info = self._site_layout_info
        if not info:
            return []
        rows = max(1, int(info.get("rows", 1)))
        cols = max(1, int(info.get("cols", 1)))
        layers = max(1, int(info.get("layers", 1)))
        if layer < 0 or layer >= layers:
            return []
        ids: list[int] = []
        stride = rows * cols
        for row in range(rows):
            for col in range(cols):
                idx = layer * stride + row * cols + col
                if idx < len(self._preset_sites):
                    ids.append(int(self._preset_sites[idx].get("id", idx)))
        return ids

    def _perimeter_site_ids(self) -> set[int]:
        info = self._site_layout_info
        if not info:
            return set()
        rows = max(1, int(info.get("rows", 1)))
        cols = max(1, int(info.get("cols", 1)))
        layers = max(1, int(info.get("layers", 1)))
        ids: set[int] = set()
        stride = rows * cols
        for layer in range(layers):
            for row in range(rows):
                for col in range(cols):
                    idx = layer * stride + row * cols + col
                    if idx >= len(self._preset_sites):
                        continue
                    is_edge = (
                        row in (0, rows - 1)
                        or col in (0, cols - 1)
                        or layer in (0, layers - 1)
                    )
                    if is_edge:
                        ids.add(int(self._preset_sites[idx].get("id", idx)))
        return ids

    def _record_assignment_selection(self, site_ids: Iterable[int]) -> None:
        self._last_selection_site_ids = set(site_ids)
        if self._role_assignment_mode:
            self._pending_role_selection.update(site_ids)

    def _toggle_role_assignment_mode(self) -> None:
        if not self._role_assignment_mode:
            self._role_assignment_mode = True
            self._pending_role_selection.clear()
            self.region_assign_button.description = "Done"
            self.region_assign_button.button_style = "success"
            self.region_assign_button.tooltip = "Select sites (rows, columns, or individual traps) then click Done"
            return
        self._role_assignment_mode = False
        self.region_assign_button.description = "Assign role"
        self.region_assign_button.button_style = ""
        self.region_assign_button.tooltip = "Pick occupied traps and label their role"
        if self._pending_role_selection:
            self._create_custom_region(self._pending_role_selection, self.region_role_dropdown.value)
        self._pending_role_selection.clear()

    def _create_custom_region(self, site_ids: set[int], role: str | None) -> None:
        if not site_ids:
            return
        role_value = role.upper() if isinstance(role, str) else "DATA"
        for region in self._region_definitions:
            region["site_ids"] = [site_id for site_id in region.get("site_ids", []) if site_id not in site_ids]
        self._region_definitions = [region for region in self._region_definitions if region.get("site_ids")]
        name = f"custom_region_{self._custom_region_counter}"
        self._custom_region_counter += 1
        self._region_definitions.append(
            {
                "name": name,
                "site_ids": sorted(site_ids),
                "role": role_value,
                "_just_created": True,
            }
        )
        self._activate_custom_configuration()
        self._refresh_geometry_panel()

    def _update_zone_summary(self) -> None:
        if not self._preset_sites:
            self.zone_summary.value = ""
            return
        total = Counter()
        occupied = Counter()
        for idx, site in enumerate(self._preset_sites):
            zone = str(site.get("zone_id", 0))
            site_id = int(site.get("id", idx))
            total[zone] += 1
            if site_id in self._current_site_ids:
                occupied[zone] += 1
        parts = []
        for zone in sorted(total.keys()):
            parts.append(f"zone {zone}: {occupied[zone]}/{total[zone]}")
        self.zone_summary.value = "<strong>Zones:</strong> " + ", ".join(parts)

    def _grid_dimension_counts(self) -> tuple[int, int, int]:
        info = self._site_layout_info or {}
        rows = max(1, int(info.get("rows", 1)))
        cols = max(1, int(info.get("cols", len(self._preset_sites))))
        layers = max(1, int(info.get("layers", 1)))
        return rows, cols, layers

    def _axis_range(self, values: Sequence[float], match_span: float | None = None) -> tuple[float, float]:
        if not values:
            return (-0.5, 0.5)
        min_v = min(values)
        max_v = max(values)
        span = max_v - min_v
        if match_span is None:
            margin = max(span * 0.1, 0.25)
            if span == 0:
                margin = max(margin, 0.25)
            return (min_v - margin, max_v + margin)
        center = (min_v + max_v) / 2
        half = match_span / 2
        margin = max(match_span * 0.02, 0.05)
        half += margin
        return (center - half, center + half)

    def _update_distance_summary(self) -> None:
        if len(self._preset_sites) < 2:
            self.distance_html.value = "<em>Distance data unavailable.</em>"
            return

        coords = [self._site_coordinate_triplet(idx) for idx in range(len(self._preset_sites))]

        distances: list[float] = []
        for idx in range(len(coords) - 1):
            x0, y0, z0 = coords[idx]
            for next_idx in range(idx + 1, len(coords)):
                x1, y1, z1 = coords[next_idx]
                dx = x0 - x1
                dy = y0 - y1
                dz = z0 - z1
                distances.append(math.sqrt(dx * dx + dy * dy + dz * dz))

        if not distances:
            self.distance_html.value = "<em>Distance data unavailable.</em>"
            return

        min_distance = min(distances)
        max_distance = max(distances)
        avg_distance = sum(distances) / len(distances)
        self.distance_html.value = (
            f"<strong>Site separation:</strong> min {min_distance:.3f}, "
            f"max {max_distance:.3f}, avg {avg_distance:.3f}"
        )

    def _update_geometry_plot(self) -> None:
        self._last_geometry_annotations = []
        with self.geometry_plot:
            self.geometry_plot.clear_output(wait=True)
            if not self._plotly_available:
                display(widgets.HTML(
                    value="<em>Install plotly to see the geometry preview.</em>"
                ))
                return
            if not self._preset_sites:
                display(widgets.HTML(
                    value="<em>Geometry unavailable.</em>"
                ))
                return

            xs: list[float] = []
            ys: list[float] = []
            annotations: list[str] = []
            colors: list[str] = []
            for idx in range(len(self._preset_sites)):
                site_id = self._site_id_at_index(idx)
                x, y, _ = self._site_coordinate_triplet(idx)
                xs.append(x)
                ys.append(y)
                label = f"s{site_id}"
                annotations.append(label)
                role = self._site_role_for(site_id)
                occupied = site_id in self._current_site_ids
                colors.append(self._color_for_role(role) if occupied else "#f5f5f5")
            self._last_geometry_annotations = annotations

            info = self._site_layout_info or {}
            dim = int(info.get("dim", 2))
            z_values: list[float] = [self._site_coordinate_triplet(idx)[2] for idx in range(len(self._preset_sites))]
            has_z = dim >= 3 or any(abs(z) > 1e-9 for z in z_values)
            hover_template = (
                "Site %{text}<br>X: %{x:.3f}<br>Y: %{y:.3f}"
                + ("<br>Z: %{z:.3f}" if has_z else "")
                + "<extra></extra>"
            )
            scatter_kwargs = dict(
                x=xs,
                y=ys,
                mode="markers+text",
                marker=dict(size=16, color=colors, line=dict(width=1, color="#222")),
                text=annotations,
                textposition="middle center",
                textfont=dict(color="#ffffff"),
                hovertemplate=hover_template,
            )
            if has_z:
                scatter_kwargs["z"] = z_values
                trace = go.Scatter3d(**scatter_kwargs)  # type: ignore[attr-defined]
            else:
                trace = go.Scatter(**scatter_kwargs)  # type: ignore[attr-defined]

            span_x = max(xs) - min(xs) if xs else 0.0
            span_y = max(ys) - min(ys) if ys else 0.0
            span_z = max(z_values) - min(z_values) if z_values else 0.0
            common_span = max(span_x, span_y, span_z, 0.1)
            range_x = self._axis_range(xs, match_span=common_span)
            range_y = self._axis_range(ys, match_span=common_span)
            range_z = self._axis_range(z_values, match_span=common_span)
            if has_z:
                fig = go.Figure(
                    data=[trace],
                    layout=go.Layout(
                        title="Lattice geometry",
                        margin=dict(l=0, r=0, t=24, b=0),
                        scene=dict(
                            aspectmode="cube",
                            xaxis=dict(title="X", zeroline=False, showline=False, range=range_x),
                            yaxis=dict(title="Y", zeroline=False, showline=False, range=range_y),
                            zaxis=dict(title="Z", zeroline=False, showline=False, range=range_z),
                        ),
                    ),
                )
            else:
                fig = go.Figure(
                    data=[trace],
                    layout=go.Layout(
                        title="Lattice geometry",
                        margin=dict(l=0, r=0, t=24, b=0),
                        xaxis=dict(
                            title="X",
                            zeroline=False,
                            showline=False,
                            mirror="ticks",
                            range=range_x,
                        ),
                        yaxis=dict(
                            title="Y",
                            scaleanchor="x",
                            scaleratio=1,
                            zeroline=False,
                            showline=False,
                            mirror="ticks",
                            range=range_y,
                        ),
                    ),
                )
            fig.update_layout(autosize=True)
            self._last_geometry_figure = fig
            display(fig)

    def _on_preview_toggle(self, change: Dict[str, Any]) -> None:
        if not change:
            return
        self._apply_preview_visibility()
        if change.get("new"):
            self._update_geometry_plot()

    def _apply_preview_visibility(self) -> None:
        if not hasattr(self, "preview_column"):
            return
        shown = bool(self.geometry_preview_toggle.value)
        self.preview_column.layout.display = "" if shown else "none"
        self.geometry_preview_toggle.description = "Hide preview" if shown else "Show preview"

    def _update_warnings(self) -> None:
        if not self._current_site_ids:
            self.warning_html.value = "<em>No warnings.</em>"
            return
        non_data = []
        orphan = []
        for site_id in sorted(self._current_site_ids):
            role = self._site_role_for(site_id)
            if role is None:
                orphan.append(site_id)
            elif role != "DATA":
                non_data.append((site_id, role))
        warnings = []
        if non_data:
            regions = ", ".join(sorted({role.lower() for _, role in non_data}))
            sites = ", ".join(str(site_id) for site_id, _ in non_data)
            warnings.append(f"Occupied sites {sites} belong to {regions} region(s).")
        if orphan:
            warnings.append(f"Sites {', '.join(str(site_id) for site_id in orphan)} lack region metadata.")
        self.warning_html.value = "<br/>".join(warnings) if warnings else "<em>No warnings.</em>"

    def _render_lattice_map(self) -> None:
        if not self._preset_sites:
            self.lattice_grid.children = (
                widgets.HTML("<em>Lattice data unavailable for this profile.</em>"),
            )
            return

        rows, cols, layers = self._grid_dimension_counts()
        template = f"repeat({cols}, minmax(40px, 1fr))" if cols else "minmax(40px, 1fr)"
        self.lattice_grid.layout.grid_template_columns = template
        self.lattice_grid.layout.grid_gap = "4px"
        self.lattice_grid.layout.grid_auto_flow = "row"
        self.lattice_grid.layout.width = "100%"
        self.lattice_grid.layout.min_width = "0"

        pending_color = None
        if self._role_assignment_mode:
            pending_role = self.region_role_dropdown.value
            if isinstance(pending_role, str):
                pending_color = self._color_for_role(pending_role.upper())

        children: list[widgets.Widget] = []
        for layer in range(layers - 1, -1, -1):
            for row in range(rows - 1, -1, -1):
                for col in range(cols):
                    site_index = layer * rows * cols + row * cols + col
                    if site_index < len(self._preset_sites):
                        children.append(
                            self._site_button_for_index(site_index, pending_color)
                        )
                    else:
                        children.append(self._empty_grid_cell(height="56px"))

        self.lattice_grid.children = tuple(children)

    def _site_button_for_index(
        self, index: int, pending_color: str | None
    ) -> widgets.Button:
        site = self._preset_sites[index]
        site_id = self._site_id_at_index(index)
        occupied = site_id in self._current_site_ids
        role = self._site_role_for(site_id)
        if self._role_assignment_mode and site_id in self._pending_role_selection and pending_color:
            color = pending_color
        else:
            color = self._color_for_role(role) if occupied else "#f5f5f5"
        tooltip_parts = [
            f"site {site_id}",
            f"zone {site.get('zone_id', 0)}",
        ]
        if role:
            tooltip_parts.append(f"role {role.lower()}")
        tooltip_parts.append("occupied" if occupied else "vacant")
        tooltip = "; ".join(tooltip_parts)
        button = widgets.Button(
            description=f"s{site_id}",
            tooltip=tooltip,
            layout=widgets.Layout(height="56px", min_width="48px", width="100%"),
        )
        button.style.button_color = color
        button.on_click(partial(self._handle_site_click, site_id))
        return button

    def _empty_grid_cell(self, *, width: str = "100%", height: str = "30px") -> widgets.HTML:
        return widgets.HTML(
            value="",
            layout=widgets.Layout(width=width, height=height),
        )

    def _site_id_at_index(self, index: int) -> int:
        site = self._preset_sites[index]
        try:
            return int(site.get("id", index))
        except (TypeError, ValueError):
            return index

    def _site_coordinate_triplet(self, index: int) -> tuple[float, float, float]:
        site = self._preset_sites[index]
        return (
            self._safe_float(site.get("x", 0.0)),
            self._safe_float(site.get("y", 0.0)),
            self._safe_float(site.get("z", 0.0)),
        )

    def _safe_float(self, value: Any) -> float:
        try:
            return float(value)
        except (TypeError, ValueError):
            return 0.0

    def _distance_between_site_indices(self, idx_a: int, idx_b: int) -> float:
        x0, y0, z0 = self._site_coordinate_triplet(idx_a)
        x1, y1, z1 = self._site_coordinate_triplet(idx_b)
        dx = x0 - x1
        dy = y0 - y1
        dz = z0 - z1
        return math.sqrt(dx * dx + dy * dy + dz * dz)

    def _render_region_palette(self) -> None:
        if not self._region_definitions:
            self.region_palette.children = (
                widgets.HTML("<em>No region metadata provided.</em>"),
            )
            return
        boxes: list[widgets.Widget] = []
        self._suspend_region_callbacks = True
        try:
            for region in self._region_definitions:
                name = region.get("name", "region")
                site_ids = region.get("site_ids", [])
                just_created = bool(region.pop("_just_created", False))
                active_site_ids: list[int] = []
                for entry in site_ids:
                    try:
                        sid = int(entry)
                    except (TypeError, ValueError):
                        continue
                    if sid in self._current_site_ids:
                        active_site_ids.append(sid)
                included = just_created or bool(active_site_ids)
                checkbox = widgets.Checkbox(
                    description=f"{name} ({len(active_site_ids)} sites)",
                    value=included,
                    indent=False,
                )
                checkbox.style = {"description_width": "initial"}
                checkbox.observe(self._make_region_handler(region), names="value")
                role_dropdown = widgets.Dropdown(
                    options=[(option.capitalize(), option) for option in _ROLE_OPTIONS],
                    value=region.get("role", "DATA"),
                    layout=widgets.Layout(width="150px"),
                )
                role_dropdown.observe(self._make_region_role_handler(region), names="value")
                box = widgets.HBox(
                    [
                        checkbox,
                        role_dropdown,
                    ],
                    layout=widgets.Layout(justify_content="space-between", align_items="center"),
                )
                boxes.append(box)
        finally:
            self._suspend_region_callbacks = False
        self.region_palette.children = tuple(boxes)

    def _make_region_handler(self, region: Mapping[str, Any]):
        def handler(change: Dict[str, Any]) -> None:
            self._on_region_toggle(region, change)
        return handler

    def _make_region_role_handler(self, region: Mapping[str, Any]):
        def handler(change: Dict[str, Any]) -> None:
            if self._suspend_region_callbacks:
                return
            new_role = change.get("new")
            if not isinstance(new_role, str):
                return
            region["role"] = new_role.upper()
            self._activate_custom_configuration()
            self._refresh_geometry_panel()
        return handler

    def _on_region_toggle(self, region: Mapping[str, Any], change: Dict[str, Any]) -> None:
        if self._suspend_region_callbacks:
            return
        checked = bool(change.get("new"))
        for entry in region.get("site_ids", []):
            try:
                site_id = int(entry)
            except (TypeError, ValueError):
                continue
            if checked:
                self._current_site_ids.add(site_id)
            else:
                self._current_site_ids.discard(site_id)
        self._activate_custom_configuration()
        self._refresh_geometry_panel()

    def _site_role_for(self, site_id: int) -> str | None:
        for region in self._region_definitions:
            if int(site_id) in region.get("site_ids", []):
                return region.get("role")
        return None

    def _color_for_role(self, role: str | None) -> str:
        palette = {
            "DATA": "#4CAF50",
            "ANCILLA": "#2196F3",
            "PARKING": "#FF9800",
            "CALIB": "#9E9E9E",
        }
        return palette.get(role, "#cfd8dc")

    def _apply_grid_layout_info(self, layout_info: Mapping[str, Any] | None) -> None:
        if not layout_info:
            return
        dim = int(layout_info.get("dim", 1))
        self.dimension_selector.value = dim
        rows = max(1, int(layout_info.get("rows", 1)))
        cols = max(1, int(layout_info.get("cols", 1)))
        layers = max(1, int(layout_info.get("layers", 1)))
        spacing = layout_info.get("spacing") or {}
        self.spacing_x_input.value = float(spacing.get("x", 1.0))
        self.spacing_y_input.value = float(spacing.get("y", self.spacing_x_input.value if dim >= 2 else 1.0))
        self.spacing_z_input.value = float(spacing.get("z", self.spacing_y_input.value if dim == 3 else 1.0))
        if dim == 1:
            self.slot_count_input.value = max(1, cols)
        else:
            self.row_input.value = rows
            self.col_input.value = cols
            if dim == 3:
                self.layer_input.value = layers
        self._update_dimension_layout()

    def _load_noise(self, noise: Mapping[str, Any] | None) -> None:
        for path, widget_field in self.noise_fields.items():
            widget_field.value = _extract_noise_value(noise, path)
        correlated = None
        if isinstance(noise, Mapping):
            correlated = noise.get("correlated_gate", {}).get("matrix")  # type: ignore[index]
        self.correlated_matrix.value = _format_matrix(correlated if correlated else None)

    @property
    def profile_payload(self) -> Dict[str, Any]:
        device_id = str(self.device_dropdown.value)
        profile_label = self.profile_dropdown.value
        label = str(profile_label)
        if label in self._profile_label_to_value:
            actual_profile = self._profile_label_to_value[label]
        else:
            actual_profile = profile_label
        custom_profile_name = self.custom_profile_name.value.strip()
        if actual_profile == _CUSTOM_PROFILE_VALUE:
            actual_profile = custom_profile_name or None
        entries = _parse_position_entries(self.positions_editor.value)
        coordinates: list[list[float]] | None = None
        if any(len(entry) > 1 for entry in entries):
            coordinates = [list(entry) for entry in entries]
            scalar_positions = list(range(len(entries)))
        else:
            scalar_positions = [entry[0] for entry in entries]

        config: Dict[str, Any] = {
            "positions": scalar_positions,
            "blockade_radius": float(self.blockade_input.value),
        }
        if coordinates:
            config["coordinates"] = coordinates
        layout_info = self._grid_layout_info()
        site_descriptors = _site_descriptors_from_entries(entries)
        preset_entries: list[tuple[float, ...]] = []
        if self._preset_sites:
            preset_coords = _coords_from_sites(self._preset_sites, layout_info)
            preset_entries = [tuple(entry) for entry in preset_coords]
        current_entries = [tuple(entry) for entry in entries]
        positions_match = bool(preset_entries) and len(preset_entries) == len(current_entries)
        if positions_match:
            positions_match = all(
                preset_entries[idx] == current_entries[idx]
                for idx in range(len(current_entries))
            )
        manual_sites = bool(site_descriptors) and (
            (self._custom_configuration_active and not positions_match)
            or not self._preset_sites
            or len(site_descriptors) != len(self._preset_sites)
        )
        manual_site_ids: list[int] | None = None
        if manual_sites:
            config["sites"] = site_descriptors
            if not self._preset_sites or len(site_descriptors) != len(self._preset_sites):
                manual_site_ids = [site["id"] for site in site_descriptors]
        elif self._preset_sites:
            config["sites"] = [dict(site) for site in self._preset_sites]
        elif site_descriptors:
            config["sites"] = site_descriptors
        if layout_info:
            config["grid_layout"] = layout_info
        noise_payload = self._collect_noise_payload()
        if noise_payload:
            config["noise"] = noise_payload
        ordered_site_ids: list[int] | None = None
        if manual_site_ids is None and self._current_site_ids:
            ordered_ids: list[int] = []
            if self._preset_sites:
                for idx, site in enumerate(self._preset_sites):
                    try:
                        site_id = int(site.get("id", idx))
                    except (TypeError, ValueError):
                        continue
                    if site_id in self._current_site_ids:
                        ordered_ids.append(site_id)
            else:
                ordered_ids = sorted(self._current_site_ids)
            if ordered_ids:
                ordered_site_ids = ordered_ids
        if ordered_site_ids is None:
            ordered_site_ids = manual_site_ids if manual_site_ids is not None else []
        config["site_ids"] = ordered_site_ids
        regions_payload = []
        for region in self._region_definitions:
            active_site_ids = []
            for entry in region.get("site_ids", []):
                try:
                    sid = int(entry)
                except (TypeError, ValueError):
                    continue
                if sid in self._current_site_ids:
                    active_site_ids.append(sid)
            region_entry = {
                "name": region["name"],
                "site_ids": active_site_ids,
                "role": region["role"],
            }
            if region.get("zone_id") is not None:
                region_entry["zone_id"] = region["zone_id"]
            regions_payload.append(region_entry)
        if regions_payload:
            config["regions"] = regions_payload
        if self._active_configuration_family_name:
            family_value = (
                "custom"
                if self._custom_configuration_active
                or self._active_configuration_family_name == self._CUSTOM_CONFIGURATION_VALUE
                else self._active_configuration_family_name
            )
            if family_value != self._CUSTOM_CONFIGURATION_VALUE:
                config["configuration_family"] = family_value
        return {
            "device_id": device_id,
            "profile": None if actual_profile == "(default)" else actual_profile,
            "config": config,
        }

    def _collect_noise_payload(self) -> Dict[str, Any]:
        payload: Dict[str, Any] = {}
        for path, field in self.noise_fields.items():
            _assign_nested(payload, path, float(field.value))
        matrix = _parse_matrix(self.correlated_matrix.value)
        if matrix is not None:
            payload.setdefault("correlated_gate", {})["matrix"] = matrix
        return payload

    def render(self) -> widgets.Widget:
        """Return the root widget so callers can display it in a notebook."""

        return self.container

    def _on_dimension_change(self, change: Dict[str, Any]) -> None:
        self._update_dimension_layout()

    def _update_dimension_layout(self) -> None:
        dim = int(self.dimension_selector.value or 1)
        if dim == 1:
            self.slot_count_input.layout.display = ""
            self.row_input.layout.display = "none"
            self.col_input.layout.display = "none"
            self.layer_input.layout.display = "none"
        elif dim == 2:
            self.slot_count_input.layout.display = "none"
            self.row_input.layout.display = ""
            self.col_input.layout.display = ""
            self.layer_input.layout.display = "none"
        else:
            self.slot_count_input.layout.display = "none"
            self.row_input.layout.display = ""
            self.col_input.layout.display = ""
            self.layer_input.layout.display = ""
        self.spacing_y_input.layout.display = "" if dim >= 2 else "none"
        self.spacing_z_input.layout.display = "" if dim == 3 else "none"

    def _spacing_values(self) -> tuple[float, float, float]:
        sx = float(self.spacing_x_input.value or 1.0)
        if sx <= 0.0:
            sx = 1.0
            self.spacing_x_input.value = sx
        sy = float(self.spacing_y_input.value or sx)
        if sy <= 0.0:
            sy = sx
            self.spacing_y_input.value = sy
        sz = float(self.spacing_z_input.value or sy)
        if sz <= 0.0:
            sz = sy
            self.spacing_z_input.value = sz
        return sx, sy, sz

    def _grid_layout_info(self) -> Dict[str, Any]:
        dim = int(self.dimension_selector.value or 1)
        sx, sy, sz = self._spacing_values()
        rows = 1
        cols = 1
        layers = 1
        if dim == 1:
            cols = max(1, int(self.slot_count_input.value or 0))
        else:
            rows = max(1, int(self.row_input.value or 0))
            cols = max(1, int(self.col_input.value or 0))
            if dim == 3:
                layers = max(1, int(self.layer_input.value or 0))
        return {
            "dim": dim,
            "rows": rows,
            "cols": cols,
            "layers": layers,
            "spacing": {"x": sx, "y": sy, "z": sz},
        }

    def _coords_for_layout(self, layout_info: Dict[str, Any]) -> list[list[float]]:
        dim = layout_info.get("dim", 1)
        rows = layout_info.get("rows", 1)
        cols = layout_info.get("cols", 1)
        layers = layout_info.get("layers", 1)
        spacing = layout_info.get("spacing", {})
        sx = float(spacing.get("x", 1.0))
        sy = float(spacing.get("y", sx if dim >= 2 else 1.0))
        sz = float(spacing.get("z", sy if dim == 3 else 1.0))
        coords: list[list[float]] = []
        if dim == 1:
            for idx in range(cols):
                coords.append([float(idx) * sx])
        elif dim == 2:
            for r in range(rows):
                for c in range(cols):
                    coords.append([float(c) * sx, float(r) * sy])
        else:
            for z in range(layers):
                for r in range(rows):
                    for c in range(cols):
                        coords.append(
                            [
                                float(c) * sx,
                                float(r) * sy,
                                float(z) * sz,
                            ]
                        )
        return coords

    def _load_presets_from_service(
        self,
        service_url: str,
        devices_endpoint: str,
        timeout: float,
    ) -> Mapping[str, Mapping[Any, Mapping[str, Any]]]:
        try:
            return fetch_remote_device_catalog(
                service_url,
                devices_endpoint,
                timeout=timeout,
            )
        except RemoteServiceError as exc:
            raise RuntimeError(
                f"failed to load presets from {service_url}: {exc}"
            ) from exc

    def _generate_positions(self) -> None:
        layout_info = self._grid_layout_info()
        coords = self._coords_for_layout(layout_info)
        if coords:
            self.positions_editor.value = _format_positions(coords)

    def _enter_custom_mode(self) -> None:
        self.custom_profile_box.layout.display = "block"
        self.metadata_html.value = "<em>Define positions, blockade, and noise to create a custom profile.</em>"
        self.configuration_dropdown.disabled = True
        self._configuration_families = {}
        self._active_configuration_family_name = self._CUSTOM_CONFIGURATION_VALUE
        self._preset_sites = []
        self._region_definitions = []
        self._current_site_ids = set()
        self._custom_configuration_active = True
        self._last_selection_site_ids = set()
        self._update_configuration_dropdown()
        self._refresh_geometry_panel()

    def _exit_custom_mode(self) -> None:
        self.custom_profile_box.layout.display = "none"
        self.configuration_dropdown.disabled = not bool(self._configuration_families)

    def _load_custom_payload(self, payload: Mapping[str, Any]) -> None:
        profile_name = payload.get("profile")
        if profile_name:
            self.custom_profile_name.value = str(profile_name)
        config = payload.get("config") or {}
        self._load_configuration_metadata(config, custom_mode=True)
        self._apply_grid_layout_info(config.get("grid_layout"))
        coords = None
        sites = config.get("sites")
        if isinstance(sites, Sequence):
            coords = _coords_from_sites(sites, config.get("grid_layout"))
        elif config.get("coordinates"):
            coords = config.get("coordinates")
        positions = config.get("positions")
        if coords:
            self.positions_editor.value = _format_positions(coords)
        elif positions:
            self.positions_editor.value = _format_positions(positions)
        blockade = config.get("blockade_radius")
        if blockade is not None:
            self.blockade_input.value = float(blockade)
        noise = config.get("noise") if isinstance(config, Mapping) else None
        if isinstance(noise, Mapping):
            self._load_noise(noise)
        else:
            self._load_noise(None)
        self._refresh_geometry_panel()


class JobResultViewer:
    """Simple widget that renders job metadata and counts in notebooks."""

    def __init__(self) -> None:
        if widgets is None:  # pragma: no cover - executed when ipywidgets missing
            raise RuntimeError(
                "JobResultViewer requires ipywidgets. Please install the 'ipywidgets' extra."
            ) from _WIDGET_IMPORT_ERROR

        self.summary_html = widgets.HTML(layout=widgets.Layout(min_height="48px"))
        self.histogram_html = widgets.HTML(layout=widgets.Layout(min_height="48px"))
        self.container = widgets.VBox(
            [self.summary_html, self.histogram_html],
            layout=widgets.Layout(width="720px"),
        )

    def load_result(
        self,
        result: Mapping[str, Any],
        *,
        device: str,
        profile: str | None,
        shots: int,
        layout: GridLayout | None = None,
    ) -> None:
        status = result.get("status", "unknown")
        elapsed = result.get("elapsed_time")
        message = result.get("message")
        logs = len(result.get("logs") or [])
        self.summary_html.value = build_result_summary_html(
            device=device,
            profile=profile,
            shots=shots,
            status=str(status),
            elapsed=elapsed,
            message=str(message) if message else None,
            log_count=logs,
        )
        measurements = result.get("measurements") or []
        self.histogram_html.value = format_histogram(measurements)

    def render(self) -> widgets.Widget:
        return self.container


__all__ = ["ProfileConfigurator", "JobResultViewer"]
