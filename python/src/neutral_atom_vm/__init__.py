"""Neutral Atom VM Python package."""

from __future__ import annotations

from .squin_lowering import to_vm_program, LoweringError

try:  # pragma: no cover - exercised in integration tests
    from ._neutral_atom_vm import submit_job
except ImportError:  # pragma: no cover
    def submit_job(*args, **kwargs):
        raise ImportError(
            "The compiled neutral_atom_vm bindings are missing. "
            "Build the C++ extension via CMake or install the package "
            "with a wheel that includes '_neutral_atom_vm'."
        )


__all__ = [
    "to_vm_program",
    "LoweringError",
    "submit_job",
]
