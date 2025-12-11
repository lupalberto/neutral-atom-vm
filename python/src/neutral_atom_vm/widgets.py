"""Interactive widgets that help configure virtual machine profiles."""

from __future__ import annotations

from dataclasses import dataclass
from html import escape
import re
from typing import Any, Dict, Mapping, MutableMapping, Sequence

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
        geometry_box = widgets.VBox(
            [
                self.blockade_input,
                dimension_box,
                spacing_box,
                self.positions_editor,
                self.positions_help,
            ]
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
                    [self.device_dropdown, self.profile_dropdown],
                    layout=widgets.Layout(justify_content="flex-start"),
                ),
                self.custom_profile_box,
                self.metadata_html,
                tabs,
            ],
            layout=widgets.Layout(width="820px"),
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
        site_descriptors = _site_descriptors_from_entries(entries)
        if site_descriptors:
            config["sites"] = site_descriptors
            config["site_ids"] = [site["id"] for site in site_descriptors]
        layout_info = self._grid_layout_info()
        if layout_info:
            config["grid_layout"] = layout_info
        noise_payload = self._collect_noise_payload()
        if noise_payload:
            config["noise"] = noise_payload
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

    def _exit_custom_mode(self) -> None:
        self.custom_profile_box.layout.display = "none"

    def _load_custom_payload(self, payload: Mapping[str, Any]) -> None:
        profile_name = payload.get("profile")
        if profile_name:
            self.custom_profile_name.value = str(profile_name)
        config = payload.get("config") or {}
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
