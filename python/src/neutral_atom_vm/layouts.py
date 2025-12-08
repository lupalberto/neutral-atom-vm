"""Shared helpers for mapping device profiles to grid layouts."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, Mapping, Optional, Tuple


@dataclass(frozen=True)
class GridLayout:
    dim: int
    rows: int
    cols: int
    layers: int = 1
    spacing: Tuple[float, float, float] = (1.0, 1.0, 1.0)

    @property
    def total_slots(self) -> int:
        return self.rows * self.cols * self.layers

    @classmethod
    def from_info(cls, info: Mapping[str, Any] | None) -> Optional["GridLayout"]:
        if not info:
            return None
        dim = int(info.get("dim", 1))
        rows = max(1, int(info.get("rows", 1)))
        cols = max(1, int(info.get("cols", 1)))
        layers = max(1, int(info.get("layers", 1)))
        if dim < 1:
            dim = 1
        if dim == 1:
            layers = 1
            rows = 1
        elif dim == 2:
            layers = 1
        spacing_info = info.get("spacing") or {}
        x = float(spacing_info.get("x", 1.0))
        y = float(spacing_info.get("y", x if dim >= 2 else 1.0))
        z = float(spacing_info.get("z", y if dim == 3 else 1.0))
        return cls(dim=dim, rows=rows, cols=cols, layers=layers, spacing=(x, y, z))


# Profiles that correspond to conceptual grids.
_GRID_LAYOUTS: Dict[str, GridLayout] = {
    "runtime": GridLayout(dim=1, rows=1, cols=2, spacing=(1.0, 1.0, 1.0)),
    "ideal_small_array": GridLayout(dim=1, rows=1, cols=10, spacing=(1.0, 1.0, 1.0)),
    "noisy_square_array": GridLayout(dim=2, rows=4, cols=4, spacing=(1.0, 1.0, 1.0)),
    "lossy_chain": GridLayout(dim=1, rows=1, cols=6, spacing=(1.5, 1.0, 1.0)),
    "lossy_block": GridLayout(dim=3, rows=2, cols=4, layers=2, spacing=(1.5, 1.0, 1.0)),
    "benchmark_chain": GridLayout(dim=1, rows=1, cols=20, spacing=(1.3, 1.0, 1.0)),
    "readout_stress": GridLayout(dim=1, rows=1, cols=8, spacing=(1.0, 1.0, 1.0)),
}


def grid_layout_for_profile(profile: str | None) -> GridLayout | None:
    if profile is None:
        return None
    return _GRID_LAYOUTS.get(profile)


__all__ = ["grid_layout_for_profile", "GridLayout"]
