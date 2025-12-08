"""Helpers for rendering Neutral Atom VM job information."""

from __future__ import annotations

from html import escape
from typing import Any, Mapping, Sequence
import uuid
import math
import io
import base64

from .layouts import GridLayout, grid_layout_for_profile


def _normalize_profile(profile: str | None) -> str:
    return str(profile) if profile is not None else "(default)"


def format_status_badge(status: str) -> str:
    normalized = status.lower()
    color = "#2d7d46"
    if normalized in {"failed", "error"}:
        color = "#b3261e"
    elif normalized in {"running", "queued"}:
        color = "#c47f17"
    return (
        f"<span style='display:inline-block;padding:2px 6px;border-radius:4px;"
        f"background:{color};color:#fff;font-size:0.85em;'>{escape(status)}</span>"
    )


def build_result_summary_html(
    *,
    device: str,
    profile: str | None,
    shots: int,
    status: str,
    elapsed: float | None,
    message: str | None,
    log_count: int | None,
) -> str:
    lines: list[str] = [
        f"<b>Device</b>: {escape(device)}",
        f"<b>Profile</b>: {escape(_normalize_profile(profile))}",
        f"<b>Shots</b>: {shots}",
        f"<b>Status</b>: {format_status_badge(status)}",
    ]
    if elapsed is not None:
        lines.append(f"<b>Elapsed</b>: {float(elapsed):.4f} s")
    if message:
        lines.append(f"<b>Message</b>: {escape(message)}")
    if log_count:
        lines.append(f"<b>Logs</b>: {log_count} entries")
    return "<br/>".join(lines)


def _interpret_measurement_value(raw: Any) -> tuple[int | None, bool, str]:
    text = str(raw).strip()
    if text in {"-", "-1"}:
        return None, True, "-"
    try:
        numeric = int(text)
    except ValueError:
        return None, False, escape(text)
    if numeric == -1:
        return None, True, "-"
    if numeric in {0, 1}:
        return numeric, False, text
    return numeric, False, text


def _gather_shot_points(
    measurement: Mapping[str, Any],
    layout: GridLayout,
    *,
    spacing: tuple[float, float, float],
    coordinates: Sequence[Sequence[float]] | None,
) -> list[dict[str, Any]]:
    raw_bits = measurement.get("bits") or []
    slot_count = layout.total_slots
    bits: list[Any] = list(raw_bits)[:slot_count]
    spacing_x, spacing_y, spacing_z = spacing
    coord_list = list(coordinates) if coordinates else None
    points: list[dict[str, Any]] = []
    cells_per_layer = layout.rows * layout.cols
    for index, raw_value in enumerate(bits):
        value, is_loss, _ = _interpret_measurement_value(raw_value)
        layer = index // cells_per_layer if cells_per_layer > 0 else 0
        remainder = index % cells_per_layer if cells_per_layer > 0 else 0
        row = remainder // layout.cols if layout.cols > 0 else 0
        col = remainder % layout.cols if layout.cols > 0 else 0
        if coord_list and index < len(coord_list):
            slot = coord_list[index]
            x = float(slot[0]) if len(slot) > 0 else col * spacing_x
            y = float(slot[1]) if len(slot) > 1 else row * spacing_y
            z_coord = float(slot[2]) if len(slot) > 2 else layer * spacing_z
        else:
            x = col * spacing_x
            y = row * spacing_y
            z_coord = layer * spacing_z
        points.append(
            {
                "x": x,
                "y": y,
                "z": z_coord,
                "value": value,
                "is_loss": is_loss,
            }
        )
    return points


def _ensure_matplotlib():
    try:
        import matplotlib.pyplot as plt  # type: ignore[import]
        from matplotlib.lines import Line2D  # type: ignore[import]
        from mpl_toolkits.mplot3d import Axes3D  # noqa: F401 pylint: disable=unused-import
    except ImportError as exc:  # pragma: no cover - optional dependency
        raise ImportError(
            "Matplotlib is required to render measurement configurations."
        ) from exc
    return plt, Line2D


def _draw_shot_on_axes(
    ax,
    measurement: Mapping[str, Any],
    layout: GridLayout,
    *,
    spacing: tuple[float, float, float],
    coordinates: Sequence[Sequence[float]] | None,
    Line2D,
):
    points = _gather_shot_points(
        measurement,
        layout,
        spacing=spacing,
        coordinates=coordinates,
    )
    xs = [point["x"] for point in points]
    ys = [point["y"] for point in points]
    zs = [point["z"] for point in points]
    facecolors: list[str | tuple[float, float, float, float]] = []
    edgecolors: list[str] = []
    sizes: list[int] = []
    for point in points:
        if point["is_loss"]:
            facecolors.append((0.9, 0.9, 0.9, 0.35))
            edgecolors.append("#999")
            sizes.append(96)
        elif point["value"] == 1:
            facecolors.append("#276ef1")
            edgecolors.append("#222")
            sizes.append(100)
        else:
            facecolors.append("#eee")
            edgecolors.append("#777")
            sizes.append(40)

    ax.scatter(
        xs,
        ys,
        zs,
        facecolors=facecolors,
        edgecolors=edgecolors,
        s=sizes,
        linewidths=0.8,
        depthshade=True,
    )

    span_eps = 1e-6
    x_min, x_max = min(xs), max(xs)
    y_min, y_max = min(ys), max(ys)
    z_min, z_max = min(zs), max(zs)
    x_span = max(x_max - x_min, span_eps)
    y_span = max(y_max - y_min, span_eps)
    z_span = max(z_max - z_min, span_eps)
    margin_factor = 0.15
    margin_x = x_span * margin_factor
    margin_y = y_span * margin_factor
    margin_z = z_span * margin_factor
    ax.set_xlim(x_min - margin_x, x_max + margin_x)
    ax.set_ylim(y_min - margin_y, y_max + margin_y)
    ax.set_zlim(z_min - margin_z, z_max + margin_z)
    ax.set_box_aspect((x_span, y_span, z_span))

    legend_handles = [
        Line2D(
            [0],
            [0],
            marker="o",
            color="#222",
            markerfacecolor="#276ef1",
            markersize=8,
            linestyle="",
            label="Measured → 1",
        ),
        Line2D(
            [0],
            [0],
            marker="o",
            color="#777",
            markerfacecolor="#eee",
            markersize=8,
            linestyle="",
            label="Measured → 0",
        ),
        Line2D(
            [0],
            [0],
            marker="o",
            color="#999",
            markerfacecolor=(0.9, 0.9, 0.9, 0.35),
            markersize=8,
            linestyle="",
            label="Lost atom",
        ),
    ]
    ax.legend(handles=legend_handles, loc="upper right")



def _build_shot_figure(
    measurement: Mapping[str, Any],
    layout: GridLayout,
    *,
    spacing: tuple[float, float, float],
    figsize: tuple[float, float],
    coordinates: Sequence[Sequence[float]] | None,
) -> tuple["matplotlib.figure.Figure", "matplotlib.axes._subplots.Axes3DSubplot"]:
    plt, Line2D = _ensure_matplotlib()
    fig = plt.figure(figsize=figsize)
    ax = fig.add_subplot(projection="3d")
    _draw_shot_on_axes(
        ax,
        measurement,
        layout,
        spacing=spacing,
        coordinates=coordinates,
        Line2D=Line2D,
    )
    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    ax.set_zlabel("Z")
    ax.set_title("Shot configuration")
    ax.view_init(elev=25, azim=45)
    fig.tight_layout(pad=0.3)
    return fig, ax


def _build_shot_multifigure(
    measurements: Sequence[Mapping[str, Any]],
    layout: GridLayout,
    *,
    spacing: tuple[float, float, float],
    figsize: tuple[float, float],
    coordinates: Sequence[Sequence[float]] | None,
    shot_indices: Sequence[int],
) -> tuple["matplotlib.figure.Figure", list["matplotlib.axes._subplots.Axes3DSubplot"]]:
    plt, Line2D = _ensure_matplotlib()
    count = len(shot_indices)
    fig = plt.figure(figsize=(figsize[0] * count, figsize[1]))
    axes = []
    for idx_pos, idx in enumerate(shot_indices):
        ax = fig.add_subplot(1, count, idx_pos + 1, projection="3d")
        _draw_shot_on_axes(
            ax,
            measurements[idx],
            layout,
            spacing=spacing,
            coordinates=coordinates,
            Line2D=Line2D,
        )
        ax.set_xlabel("X")
        ax.set_ylabel("Y")
        ax.set_zlabel("Z")
        ax.set_title(f"Shot {idx}")
        ax.view_init(elev=25, azim=45)
        axes.append(ax)
    fig.tight_layout(pad=0.3)
    return fig, axes


def _build_plotly_shot_figure(
    measurement: Mapping[str, Any],
    layout: GridLayout,
    *,
    spacing: tuple[float, float, float],
    coordinates: Sequence[Sequence[float]] | None,
) -> "plotly.graph_objects.Figure":
    try:
        import plotly.graph_objects as go  # type: ignore[import]
    except ImportError as exc:  # pragma: no cover - optional dependency
        raise ImportError(
            "Plotly is required for interactive display_shot output. "
            "Install it with `pip install plotly`."
        ) from exc

    points = _gather_shot_points(
        measurement,
        layout,
        spacing=spacing,
        coordinates=coordinates,
    )
    xs = [point["x"] for point in points]
    ys = [point["y"] for point in points]
    zs = [point["z"] for point in points]
    span_eps = 1e-6
    x_min, x_max = min(xs), max(xs)
    y_min, y_max = min(ys), max(ys)
    z_min, z_max = min(zs), max(zs)
    x_span = max(x_max - x_min, span_eps)
    y_span = max(y_max - y_min, span_eps)
    z_span = max(z_max - z_min, span_eps)
    margin_x = x_span * 0.05
    margin_y = y_span * 0.05
    margin_z = z_span * 0.05

    def _trace_subset(name: str, subset: list[dict[str, Any]], color: str, line_color: str, opacity: float):
        if not subset:
            return None
        return go.Scatter3d(
            x=[p["x"] for p in subset],
            y=[p["y"] for p in subset],
            z=[p["z"] for p in subset],
            mode="markers",
            marker=dict(
                size=11,
                color=color,
                line=dict(color=line_color, width=1.4),
            ),
            name=name,
            opacity=opacity,
        )

    ones = [p for p in points if p["value"] == 1 and not p["is_loss"]]
    zeros = [p for p in points if p["value"] == 0 and not p["is_loss"]]
    lost = [p for p in points if p["is_loss"]]

    fig = go.Figure()
    for trace_info in [
        ("Measured → 1", ones, "#276ef1", "#222", 1.0),
        ("Measured → 0", zeros, "#eee", "#777", 1.0),
        ("Lost atom", lost, "rgba(210,210,210,0.65)", "#999", 0.35),
    ]:
        trace = _trace_subset(*trace_info)
        if trace is not None:
            fig.add_trace(trace)

    fig.update_layout(
        scene=dict(
            xaxis=dict(range=[x_min - margin_x, x_max + margin_x], title="X"),
            yaxis=dict(range=[y_min - margin_y, y_max + margin_y], title="Y"),
            zaxis=dict(range=[z_min - margin_z, z_max + margin_z], title="Z"),
            aspectmode="manual",
            aspectratio=dict(x=x_span, y=y_span, z=z_span),
            camera=dict(eye=dict(x=1.4, y=1.4, z=1.0)),
        ),
        margin=dict(l=0, r=0, b=0, t=30),
        legend=dict(x=0.01, y=0.99),
        showlegend=True,
        height=600,
        width=600,
    )
    return fig


def _build_plotly_planar_figure(
    measurement: Mapping[str, Any],
    layout: GridLayout,
    *,
    spacing: tuple[float, float, float],
    coordinates: Sequence[Sequence[float]] | None,
) -> "plotly.graph_objects.Figure":
    try:
        import plotly.graph_objects as go  # type: ignore[import]
    except ImportError as exc:  # pragma: no cover - optional dependency
        raise ImportError(
            "Plotly is required for interactive display_shot output. "
            "Install it with `pip install plotly`."
        ) from exc

    points = _gather_shot_points(
        measurement,
        layout,
        spacing=spacing,
        coordinates=coordinates,
    )
    xs = [point["x"] for point in points]
    ys = [point["y"] for point in points]
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    span_eps = 1e-6
    x_span = max(max_x - min_x, span_eps)
    y_span = max(max_y - min_y, span_eps)
    margin_factor = 0.15
    margin_x = x_span * margin_factor
    margin_y = y_span * margin_factor

    def _trace_subset(name: str, subset: list[dict[str, Any]], color: str, line_color: str, opacity: float):
        if not subset:
            return None
        return go.Scatter(
            x=[p["x"] for p in subset],
            y=[p["y"] for p in subset],
            mode="markers",
            marker=dict(
                size=14,
                color=color,
                line=dict(color=line_color, width=1.4),
            ),
            name=name,
            opacity=opacity,
            hoverinfo="text",
            hovertext=[f"{name}: {p['x']:.2f},{p['y']:.2f}" for p in subset],
        )

    ones = [p for p in points if p["value"] == 1 and not p["is_loss"]]
    zeros = [p for p in points if p["value"] == 0 and not p["is_loss"]]
    lost = [p for p in points if p["is_loss"]]

    fig = go.Figure()
    for trace_info in [
        ("Measured → 1", ones, "#276ef1", "#222", 1.0),
        ("Measured → 0", zeros, "#eee", "#777", 1.0),
        ("Lost atom", lost, "rgba(210,210,210,0.65)", "#999", 0.35),
    ]:
        trace = _trace_subset(*trace_info)
        if trace is not None:
            fig.add_trace(trace)

        fig.update_layout(
            xaxis=dict(
                range=[min_x - margin_x, max_x + margin_x],
                title="X",
                showgrid=False,
            ),
            yaxis=dict(
                range=[min_y - margin_y, max_y + margin_y],
                title="Y",
                showgrid=False,
                scaleanchor="x",
                scaleratio=1,
            ),
            margin=dict(l=0, r=0, b=0, t=30),
            legend=dict(x=0.01, y=0.99),
            showlegend=True,
            width=600,
            height=600,
        )
    return fig


def _build_plotly_multishot(
    measurements: Sequence[Mapping[str, Any]],
    layout: GridLayout,
    *,
    spacing: tuple[float, float, float],
    coordinates: Sequence[Sequence[float]] | None,
    shot_indices: Sequence[int],
) -> "plotly.graph_objects.Figure":
    from plotly.subplots import make_subplots  # type: ignore[import]

    builder = (
        _build_plotly_shot_figure
        if layout.dim >= 3
        else _build_plotly_planar_figure
    )
    spec_type = "scene" if layout.dim >= 3 else "xy"
    specs = [[{"type": spec_type} for _ in shot_indices]]
    titles = [f"Shot {idx}" for idx in shot_indices]
    fig = make_subplots(
        rows=1,
        cols=len(shot_indices),
        specs=specs,
        subplot_titles=titles,
    )
    for col, idx in enumerate(shot_indices, start=1):
        single = builder(
            measurements[idx],
            layout,
            spacing=spacing,
            coordinates=coordinates,
        )
        for trace in single.data:
            fig.add_trace(trace, row=1, col=col)
        if layout.dim >= 3:
            scene_name = "scene" if col == 1 else f"scene{col}"
            scene_config = single.layout.scene.to_plotly_json()
            fig.update_layout(**{scene_name: scene_config})
        else:
            xaxis_name = "xaxis" if col == 1 else f"xaxis{col}"
            yaxis_name = "yaxis" if col == 1 else f"yaxis{col}"
            xaxis_config = single.layout.xaxis.to_plotly_json()
            yaxis_config = single.layout.yaxis.to_plotly_json()
            fig.update_layout(
                **{
                    xaxis_name: xaxis_config,
                    yaxis_name: yaxis_config,
                }
            )
    fig.update_layout(
        height=600,
        width=600 * len(shot_indices),
        margin=dict(l=0, r=0, b=0, t=30),
        showlegend=True,
    )
    return fig


def display_shot(
    result: Mapping[str, Any],
    *,
    shot_index: int = 0,
    shot_indices: Sequence[int] | None = None,
    figsize: tuple[float, float] = (4.0, 4.0),
    interactive: bool = False,
) -> tuple["matplotlib.figure.Figure", "matplotlib.axes._subplots.Axes3DSubplot"] | "plotly.graph_objects.Figure" | tuple["matplotlib.figure.Figure", list["matplotlib.axes._subplots.Axes3DSubplot"]]:
    measurements = result.get("measurements") or []
    if not measurements:
        raise ValueError("No measurements available to visualize.")
    if shot_indices is None:
        if not (0 <= shot_index < len(measurements)):
            raise IndexError("shot_index out of range.")
        measurement = measurements[shot_index]
    else:
        for idx in shot_indices:
            if not (0 <= idx < len(measurements)):
                raise IndexError(f"shot_index {idx} out of range.")
    layout = getattr(result, "layout", None)
    if layout is None:
        profile = getattr(result, "profile", result.get("profile"))
        layout = grid_layout_for_profile(profile)
    if layout is None:
        raise ValueError("Grid layout metadata is not available for this profile.")
    coordinates = getattr(result, "coordinates", None)
    spacing = layout.spacing
    if shot_indices is None:
        if interactive:
            if layout.dim >= 3:
                return _build_plotly_shot_figure(
                    measurement,
                    layout,
                    spacing=spacing,
                    coordinates=coordinates,
                )
            return _build_plotly_planar_figure(
                measurement,
                layout,
                spacing=spacing,
                coordinates=coordinates,
            )
        return _build_shot_figure(
            measurement,
            layout,
            spacing=spacing,
            figsize=figsize,
            coordinates=coordinates,
        )
    if interactive:
        return _build_plotly_multishot(
            measurements,
            layout,
            spacing=spacing,
            coordinates=coordinates,
            shot_indices=shot_indices,
        )
    return _build_shot_multifigure(
        measurements,
        layout,
        spacing=spacing,
        figsize=figsize,
        coordinates=coordinates,
        shot_indices=shot_indices,
    )
def render_measurement_configuration(
    measurement: Mapping[str, Any],
    layout: GridLayout,
    *,
    spacing: tuple[float, float, float] = (1.0, 1.0, 1.0),
    figsize: tuple[float, float] = (4.0, 4.0),
    coordinates: Sequence[Sequence[float]] | None = None,
) -> str:
    spacing = spacing or layout.spacing
    try:
        fig, _ = _build_shot_figure(
            measurement,
            layout,
            spacing=spacing,
            figsize=figsize,
            coordinates=coordinates,
        )
    except ImportError:
        return "<em>Matplotlib unavailable; no configuration preview.</em>"
    except ValueError:
        return "<em>Measurement bits do not match layout.</em>"
    buf = io.BytesIO()
    fig.savefig(buf, format="png", dpi=120, bbox_inches="tight")
    fig.clf()
    encoded = base64.b64encode(buf.getvalue()).decode("ascii")
    return (
        f"<img src='data:image/png;base64,{encoded}' "
        "style='width:100%;height:auto;border-radius:4px;box-shadow:0 2px 6px rgba(0,0,0,0.2);'/>"
    )


def format_histogram(
    measurements: Sequence[Mapping[str, Any]],
    *,
    page_size: int = 24,
) -> str:
    counts: dict[str, int] = {}
    for rec in measurements:
        bits = rec.get("bits")
        if not bits:
            continue
        bitstring = "".join(str(int(bit)) for bit in bits)
        counts[bitstring] = counts.get(bitstring, 0) + 1
    if not counts:
        return "<em>No histogram data.</em>"

    sorted_items = sorted(counts.items(), key=lambda item: (-item[1], item[0]))
    pages: list[list[tuple[str, int]]] = []
    for i in range(0, len(sorted_items), max(1, page_size)):
        pages.append(sorted_items[i : i + max(1, page_size)])

    max_count = max(count for _, count in sorted_items)
    total = sum(counts.values()) or 1

    page_blocks: list[str] = []
    for idx, page in enumerate(pages):
        bars: list[str] = []
        display = "block" if idx == 0 else "none"
        for bitstring, count in page:
            width_pct = (count / max_count) * 100 if max_count else 0
            prob = count / total
            bars.append(
                "<div style='display:flex;align-items:center;gap:8px;margin-bottom:4px;'>"
                f"<code style='min-width:70px;'>{escape(bitstring)}</code>"
                f"<div style='background:#e2e8f7;height:14px;flex:1;position:relative;'>"
                    f"<div style='background:#276ef1;height:14px;width:{width_pct:.2f}%;'></div>"
                "</div>"
                "<span style='flex:0 0 120px;min-width:120px;text-align:right;font-family:monospace;white-space:nowrap;'>"
                f"{count:>4} ({prob:.2%})</span>"
                "</div>"
            )
        page_blocks.append(
            f"<div data-hist-page='{idx}' style='display:{display};'>"
            + "".join(bars)
            + "</div>"
        )

    container_id = f"na-vm-hist-{uuid.uuid4().hex}"
    nav_html = ""
    script = ""
    page_count = len(pages)
    if page_count > 1:
        nav_html = (
            "<div style='margin-bottom:6px;display:flex;align-items:center;gap:8px;'>"
            "<button type='button' data-hist-prev>Prev</button>"
            f"<span>Page <span data-hist-page-label>1</span> / {page_count}</span>"
            "<button type='button' data-hist-next>Next</button>"
            "</div>"
        )
        script = (
            "<script>(function(){var root=document.getElementById('"
            + container_id
            + "'); if(!root) return; var pages=root.querySelectorAll('[data-hist-page]');"
            "var prev=root.querySelector('[data-hist-prev]');"
            "var next=root.querySelector('[data-hist-next]');"
            "var label=root.querySelector('[data-hist-page-label]');"
            "var current=0; function show(idx){ if(idx<0||idx>=pages.length) return;"
            "current=idx; pages.forEach(function(el,i){el.style.display = i===idx ? 'block':'none';});"
            "if(label) label.textContent = (idx+1);"
            "if(prev) prev.disabled = idx===0;"
            "if(next) next.disabled = idx===pages.length-1; }"
            "if(prev) prev.addEventListener('click',function(){show(Math.max(0,current-1));});"
            "if(next) next.addEventListener('click',function(){show(Math.min(pages.length-1,current+1));});"
            "show(0);})();</script>"
        )

    pages_html = (
        "<div style='overflow-x:auto;max-width:100%;'>"
        + "".join(page_blocks)
        + "</div>"
    )

    return (
        f"<div id='{container_id}' class='na-vm-histogram'>"
        "<div><strong>Histogram</strong></div>"
        f"{nav_html}{pages_html}</div>"
        + script
    )


def render_job_result_html(
    *,
    result: Mapping[str, Any],
    device: str,
    profile: str | None,
    shots: int,
    layout: GridLayout | None = None,
    coordinates: Sequence[Sequence[float]] | None = None,
) -> str:
    summary = build_result_summary_html(
        device=device,
        profile=profile,
        shots=shots,
        status=str(result.get("status", "unknown")),
        elapsed=result.get("elapsed_time"),
        message=result.get("message"),
        log_count=len(result.get("logs") or []),
    )
    measurements = result.get("measurements") or []
    histogram = format_histogram(measurements)
    parts = [summary]
    if histogram:
        parts.append(histogram)
    body = "<hr/>".join(parts)
    return f"<div class='na-vm-job-result'>{body}</div>"


__all__ = [
    "format_status_badge",
    "build_result_summary_html",
    "render_job_result_html",
    "format_histogram",
    "render_measurement_configuration",
    "display_shot",
]
