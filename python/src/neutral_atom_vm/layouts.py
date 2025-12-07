"""Shared helpers for mapping device profiles to grid layouts."""

from __future__ import annotations

from typing import Tuple

# Profiles that correspond to conceptual 2D grids.
_GRID_LAYOUTS: dict[str, Tuple[int, int]] = {
    "noisy_square_array": (4, 4),
}


def grid_layout_for_profile(profile: str | None) -> Tuple[int, int] | None:
    if profile is None:
        return None
    return _GRID_LAYOUTS.get(profile)


__all__ = ["grid_layout_for_profile"]
