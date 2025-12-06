from __future__ import annotations

from collections.abc import Iterable
from typing import Any

from kirin.dialects.func import stmts as func_stmts
from kirin.dialects.py import constant as py_constant
from kirin.dialects.py import indexing as py_indexing
from kirin.ir.method import Method

GateInstruction = dict[str, Any]


GATE_NAME_MAP = {
    "h": "H",
    "s": "S",
    "t": "T",
    "x": "X",
    "y": "Y",
    "z": "Z",
    "cx": "CX",
    "cy": "CY",
    "cz": "CZ",
    "rx": "RX",
    "ry": "RY",
    "rz": "RZ",
}

PARAMETRIC_GATES = {
    "rx",
    "ry",
    "rz",
}


class LoweringError(RuntimeError):
    pass


def _resolve_value(value_map: dict[Any, Any], value: Any) -> Any:
    if value in value_map:
        return value_map[value]
    raise LoweringError(f"Unable to resolve value: {value!r}")


def to_vm_program(kernel: Method) -> list[GateInstruction]:
    """Lower a squin kernel to the VM instruction schema."""

    if not isinstance(kernel, Method):
        raise TypeError("Expected a Kirin Method produced by @squin.kernel")

    program: list[GateInstruction] = []
    value_map: dict[Any, Any] = {}
    register_indices: dict[Any, list[int]] = {}
    total_qubits = 0

    body = kernel.code.body
    if len(body.blocks) != 1:
        raise LoweringError("Expected a single basic block in the kernel body")
    block = body.blocks[0]

    for stmt in block.stmts:
        if isinstance(stmt, py_constant.Constant):
            value_map[stmt.result] = stmt.value.data
            continue

        if isinstance(stmt, py_indexing.GetItem):
            base = _resolve_value(value_map, stmt.obj)
            index = _resolve_value(value_map, stmt.index)
            if isinstance(base, list):
                try:
                    resolved = base[index]
                except (IndexError, TypeError) as exc:
                    raise LoweringError("Invalid qubit index access") from exc
            else:
                raise LoweringError(f"Unsupported getitem base: {base!r}")
            value_map[stmt.result] = resolved
            continue

        if isinstance(stmt, func_stmts.Invoke):
            callee = stmt.callee.sym_name
            args = list(stmt.args)

            if callee == "qalloc":
                n_qubits = int(_resolve_value(value_map, args[0]))
                program.append({"op": "AllocArray", "n_qubits": n_qubits})
                indices = list(range(total_qubits, total_qubits + n_qubits))
                total_qubits += n_qubits
                register_indices[stmt.result] = indices
                value_map[stmt.result] = indices
                continue

            if callee == "measure":
                target = _resolve_value(value_map, args[0])
                if isinstance(target, list):
                    targets = target
                else:
                    targets = [target]
                program.append({"op": "Measure", "targets": targets})
                continue

            gate_name = GATE_NAME_MAP.get(callee)
            if gate_name is not None:
                resolved_args = [_resolve_value(value_map, arg) for arg in args]
                if callee in PARAMETRIC_GATES:
                    if not resolved_args:
                        raise LoweringError(f"Gate '{callee}' missing parameter argument")
                    param_value = float(resolved_args[0])
                    target_values = resolved_args[1:]
                else:
                    param_value = 0.0
                    target_values = resolved_args

                resolved_targets: list[int] = []
                for value in target_values:
                    if isinstance(value, Iterable) and not isinstance(value, (str, bytes)):
                        raise LoweringError("Gate targets must be single qubits")
                    resolved_targets.append(int(value))

                program.append(
                    {
                        "op": "ApplyGate",
                        "name": gate_name,
                        "targets": resolved_targets,
                        "param": param_value,
                    }
                )
                continue

    return program


__all__ = ["to_vm_program", "LoweringError"]
