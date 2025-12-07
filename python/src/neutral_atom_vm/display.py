"""Helpers for rendering Neutral Atom VM job information."""

from __future__ import annotations

from html import escape
from typing import Any, Mapping, Sequence
import uuid

from .layouts import grid_layout_for_profile


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


def _format_grid(bitstring: str, rows: int, cols: int) -> str:
    cells = []
    for idx, char in enumerate(bitstring):
        value = int(char)
        color = "#222" if value else "#eee"
        text_color = "#fff" if value else "#333"
        border = "1px solid #ccc"
        cells.append(
            f"<div style='display:flex;align-items:center;justify-content:center;"
            f"background:{color};color:{text_color};border:{border};font-size:0.85em;'>"
            f"{value}</div>"
        )
    grid_style = (
        "display:grid;grid-template-columns:"
        + " ".join(["1fr"] * cols)
        + ";gap:2px;width:120px;"
    )
    return f"<div style='{grid_style}'>{''.join(cells)}</div>"


def format_grid_previews(measurements: Sequence[Mapping[str, Any]], profile: str | None) -> str:
    layout = grid_layout_for_profile(profile)
    if not layout:
        return ""
    rows, cols = layout
    stride = rows * cols
    counts: dict[str, int] = {}
    for rec in measurements:
        bits = rec.get("bits")
        if not bits or len(bits) != stride:
            continue
        bitstring = "".join(str(int(bit)) for bit in bits)
        counts[bitstring] = counts.get(bitstring, 0) + 1
    if not counts:
        return ""
    top = sorted(counts.items(), key=lambda item: (-item[1], item[0]))[:3]
    cards = []
    for bitstring, count in top:
        grid = _format_grid(bitstring, rows, cols)
        cards.append(
            "<div style='margin-right:12px;margin-bottom:12px;'>"
            f"<div style='margin-bottom:4px;font-weight:bold;'>"
            f"{escape(bitstring)} – {count} shots</div>"
            f"{grid}"
            "</div>"
        )
    return (
        "<div><strong>Grid preview</strong></div>"
        f"<div style='display:flex;flex-wrap:wrap;'>{''.join(cards)}</div>"
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
                "<span style='width:90px;text-align:right;font-family:monospace;'>"
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

    return (
        f"<div id='{container_id}' class='na-vm-histogram'>"
        "<div><strong>Histogram</strong></div>"
        f"{nav_html}{''.join(page_blocks)}</div>"
        + script
    )


def render_job_result_html(
    *,
    result: Mapping[str, Any],
    device: str,
    profile: str | None,
    shots: int,
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
    grid_preview = format_grid_previews(measurements, profile)
    histogram = format_histogram(measurements)
    parts = [summary]
    if grid_preview:
        parts.append(grid_preview)
    if histogram:
        parts.append(histogram)
    body = "<hr/>".join(parts)
    return f"<div class='na-vm-job-result'>{body}</div>"


__all__ = [
    "format_status_badge",
    "build_result_summary_html",
    "render_job_result_html",
    "format_grid_previews",
    "format_histogram",
]
