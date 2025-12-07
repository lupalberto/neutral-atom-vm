from __future__ import annotations

from collections.abc import Iterable
from typing import Any

from kirin.dialects.func import stmts as func_stmts
from kirin.dialects.py import constant as py_constant
from kirin.dialects.py import indexing as py_indexing
from kirin.dialects.py.binop import stmts as py_binop_stmts
from kirin.dialects.scf import stmts as scf_stmts
from kirin.dialects.ilist import stmts as ilist_stmts
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

    def _lower_stmt(stmt: Any) -> None:
        nonlocal total_qubits

        if isinstance(stmt, py_constant.Constant):
            value_map[stmt.result] = stmt.value.data
            return

        if isinstance(stmt, ilist_stmts.Range):
            # Lower ilist.range(start, stop, step) into a concrete Python list so
            # loop iterables can be resolved, even when the bounds depend on
            # symbolic block arguments (e.g. `range(n)` inside nested loops).
            start_val = _resolve_value(value_map, stmt.start)
            stop_val = _resolve_value(value_map, stmt.stop)
            step_val = _resolve_value(value_map, stmt.step)
            try:
                start_i = int(start_val)
                stop_i = int(stop_val)
                step_i = int(step_val)
            except (TypeError, ValueError) as exc:
                raise LoweringError("range() bounds must be integers") from exc
            value_map[stmt.result] = list(range(start_i, stop_i, step_i))
            return

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
            return

        if isinstance(stmt, func_stmts.Invoke):
            callee = stmt.callee.sym_name
            args = list(stmt.args)

            if callee == "range":
                resolved_args = [_resolve_value(value_map, arg) for arg in args]
                try:
                    if len(resolved_args) == 1:
                        start, stop, step = 0, int(resolved_args[0]), 1
                    elif len(resolved_args) == 2:
                        start, stop = int(resolved_args[0]), int(resolved_args[1])
                        step = 1
                    elif len(resolved_args) == 3:
                        start = int(resolved_args[0])
                        stop = int(resolved_args[1])
                        step = int(resolved_args[2])
                    else:
                        raise LoweringError("range() with unsupported number of arguments")
                except (TypeError, ValueError) as exc:
                    raise LoweringError("range() arguments must be integers") from exc

                value_map[stmt.result] = list(range(start, stop, step))
                return

            if callee == "qalloc":
                n_qubits = int(_resolve_value(value_map, args[0]))
                program.append({"op": "AllocArray", "n_qubits": n_qubits})
                indices = list(range(total_qubits, total_qubits + n_qubits))
                total_qubits += n_qubits
                register_indices[stmt.result] = indices
                value_map[stmt.result] = indices
                return

            if callee == "measure":
                target = _resolve_value(value_map, args[0])
                if isinstance(target, list):
                    targets = target
                else:
                    targets = [target]
                program.append({"op": "Measure", "targets": targets})
                return

            gate_name = GATE_NAME_MAP.get(callee)
            if gate_name is not None:
                resolved_args = [_resolve_value(value_map, arg) for arg in args]
                if callee in PARAMETRIC_GATES:
                    if not resolved_args:
                        raise LoweringError(
                            f"Gate '{callee}' missing parameter argument"
                        )
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
                return

        if isinstance(stmt, scf_stmts.For):
            _lower_for(stmt)
            return

        if isinstance(stmt, py_binop_stmts.Sub):
            lhs_value = _resolve_value(value_map, stmt.lhs)
            rhs_value = _resolve_value(value_map, stmt.rhs)
            if not isinstance(lhs_value, (int, float)) or not isinstance(rhs_value, (int, float)):
                raise LoweringError("Unsupported operands for subtraction")
            value_map[stmt.result] = lhs_value - rhs_value
            return

        if isinstance(stmt, py_binop_stmts.Mult):
            lhs_value = _resolve_value(value_map, stmt.lhs)
            rhs_value = _resolve_value(value_map, stmt.rhs)
            if not isinstance(lhs_value, (int, float)) or not isinstance(rhs_value, (int, float)):
                raise LoweringError("Unsupported operands for multiplication")
            value_map[stmt.result] = lhs_value * rhs_value
            return

        if isinstance(stmt, py_binop_stmts.Add):
            lhs_value = _resolve_value(value_map, stmt.lhs)
            rhs_value = _resolve_value(value_map, stmt.rhs)
            if not isinstance(lhs_value, (int, float)) or not isinstance(rhs_value, (int, float)):
                raise LoweringError("Unsupported operands for addition")
            value_map[stmt.result] = lhs_value + rhs_value
            return

        # Other statements (ConstantNone, Return, etc.) are ignored during lowering.

    def _lower_block(block_node: Any) -> None:
        for inner_stmt in block_node.stmts:
            _lower_stmt(inner_stmt)

    def _lower_for(stmt: scf_stmts.For) -> None:
        iterable_value = _resolve_value(value_map, stmt.iterable)
        try:
            iteration_values = list(iterable_value)
        except TypeError as exc:
            raise LoweringError("Loop iterables must be concrete sequences") from exc

        region = stmt.body
        if len(region.blocks) != 1:
            raise LoweringError("Expected a single block inside loop body")
        loop_block = region.blocks[0]
        block_args = list(loop_block.args)
        expected_iter_args = len(stmt.initializers)
        if len(block_args) != 1 + expected_iter_args:
            raise LoweringError("Unsupported loop structure")

        current_iter_values = [
            _resolve_value(value_map, initializer)
            for initializer in stmt.initializers
        ]

        saved_values = {arg: value_map[arg] for arg in block_args if arg in value_map}
        try:
            for iter_value in iteration_values:
                value_map[block_args[0]] = iter_value
                for idx, arg in enumerate(block_args[1:], start=0):
                    value_map[arg] = current_iter_values[idx]

                for inner_stmt in loop_block.stmts:
                    if isinstance(inner_stmt, scf_stmts.Yield):
                        if len(inner_stmt.args) != expected_iter_args:
                            raise LoweringError("Unexpected yield arguments inside loop")
                        current_iter_values = [
                            _resolve_value(value_map, arg) for arg in inner_stmt.args
                        ]
                        continue
                    _lower_stmt(inner_stmt)

            for result_val, iter_value in zip(stmt.results, current_iter_values):
                value_map[result_val] = iter_value
        finally:
            for arg in block_args:
                if arg in saved_values:
                    value_map[arg] = saved_values[arg]
                else:
                    value_map.pop(arg, None)

    _lower_block(block)
    return program


__all__ = ["to_vm_program", "LoweringError"]
