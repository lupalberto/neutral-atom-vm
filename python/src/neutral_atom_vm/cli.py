from __future__ import annotations

import argparse
import importlib
import json
import logging
import os
import runpy
import sys
from collections import Counter
from typing import Any, Callable, Mapping, Sequence

from .device import connect_device, build_device_from_config


def _load_kernel(target: str) -> Callable[..., Any]:
    """Load a kernel callable from either MODULE:FUNC or path/to/script.py[:FUNC]."""

    module_spec, func_name = (target.split(":", 1) + [None])[:2] if ":" in target else (target, None)

    # Path-based target: path/to/script.py[:FUNC]
    if module_spec.endswith(".py") or os.path.sep in module_spec:
        script_path = module_spec
        if not os.path.exists(script_path):
            raise SystemExit(f"Script path {script_path!r} does not exist")
        globals_ns = runpy.run_path(script_path, run_name="__quera_vm_script__")
        if func_name is not None:
            obj = globals_ns.get(func_name)
            if obj is None or not callable(obj):
                raise SystemExit(
                    f"Kernel {func_name!r} not found or not callable in script {script_path!r}"
                )
            return obj

        # No function specified: if there is exactly one public callable, use it.
        candidates = [
            obj
            for name, obj in globals_ns.items()
            if callable(obj) and not name.startswith("_")
        ]
        if len(candidates) == 1:
            return candidates[0]
        raise SystemExit(
            f"Script {script_path!r} defines multiple callables; "
            "please specify one explicitly as path/to/script.py:FUNC"
        )

    # Module-based target: MODULE:FUNC
    if func_name is None:
        raise SystemExit(f"Invalid target {target!r}; expected MODULE:FUNC or path/to/script.py[:FUNC]")

    module = importlib.import_module(module_spec)
    try:
        kernel = getattr(module, func_name)
    except AttributeError as exc:
        raise SystemExit(f"Kernel {func_name!r} not found in module {module_spec!r}") from exc
    if not callable(kernel):
        raise SystemExit(f"Target {target!r} is not callable")
    return kernel


GRID_LAYOUTS: dict[str, tuple[int, int]] = {
    "noisy_square_array": (4, 4),
}


def _grid_layout_for_profile(profile: str | None) -> tuple[int, int] | None:
    if profile is None:
        return None
    return GRID_LAYOUTS.get(profile)


def _display_bit(bit: int) -> str:
    if bit == -1:
        return "?"
    return str(bit)


def _grid_lines_for_bits(
    bits: Sequence[int],
    rows: int,
    cols: int,
) -> list[str] | None:
    if len(bits) != rows * cols:
        return None
    lines: list[str] = []
    for r in range(rows):
        row_bits = bits[r * cols : (r + 1) * cols]
        lines.append(" ".join(_display_bit(bit) for bit in row_bits))
    return lines


def _summarize_result(
    result: Mapping[str, Any],
    *,
    device: str,
    profile: str | None,
    shots: int,
) -> None:
    status = result.get("status", "unknown")
    elapsed = result.get("elapsed_time", None)
    measurements = result.get("measurements", [])

    print(f"Device: {device} (profile={profile!r})")
    print(f"Shots: {shots}")
    print(f"Status: {status}")
    if elapsed is not None:
        print(f"Elapsed time: {elapsed:.6f} s")
    message = result.get("message")
    if message:
        print(f"Message: {message}")

    counts: Counter[str] = Counter()
    for rec in measurements:
        bits = rec.get("bits", [])
        if not bits:
            continue
        key = "".join(str(int(b)) for b in bits)
        counts[key] += 1

    grid_layout = _grid_layout_for_profile(profile)
    grid_examples: dict[str, list[int]] = {}

    for rec in measurements:
        bits = rec.get("bits", [])
        if not bits:
            continue
        key = "".join(str(int(b)) for b in bits)
        if grid_layout:
            rows, cols = grid_layout
            if len(bits) == rows * cols:
                grid_examples.setdefault(key, list(int(b) for b in bits))

    if counts:
        print("Counts:")
        for bitstring, count in sorted(counts.items()):
            print(f"  {bitstring}: {count}")
            if grid_layout:
                rows, cols = grid_layout
                lines = _grid_lines_for_bits(grid_examples.get(bitstring, []), rows, cols)
                if lines:
                    print("    Grid:")
                    for line in lines:
                        print(f"      {line}")
    else:
        print("Counts: (no measurements)")


def _write_log_file(path: str, logs: Sequence[Mapping[str, Any]]) -> None:
    log_dir = os.path.dirname(path)
    if log_dir:
        os.makedirs(log_dir, exist_ok=True)
    formatter = logging.Formatter("%(asctime)s %(levelname)s %(message)s")
    handler = logging.FileHandler(path, encoding="utf-8")
    handler.setFormatter(formatter)
    logger = logging.Logger("neutral_atom_vm.cli.log_file")
    logger.setLevel(logging.INFO)
    logger.propagate = False
    logger.addHandler(handler)
    try:
        for entry in logs:
            shot = entry.get("shot")
            logical_time = entry.get("time")
            category = entry.get("category")
            message = entry.get("message")
            logger.info(
                "shot=%s time=%s category=%s message=%s",
                shot,
                logical_time,
                category,
                message,
            )
    finally:
        handler.flush()
        handler.close()
        logger.removeHandler(handler)


def _cmd_run(args: argparse.Namespace) -> int:
    kernel = _load_kernel(args.target)

    if args.profile_config:
        profile_payload = _load_profile_config(args.profile_config)
        profile_name = profile_payload.get("profile", args.profile)
        device = build_device_from_config(
            args.device,
            profile=profile_name,
            config=profile_payload,
        )
    else:
        device = connect_device(args.device, profile=args.profile)

    if args.threads < 0:
        raise SystemExit("--threads must be >= 0")
    thread_limit: int | None = args.threads if args.threads > 0 else None

    try:
        job = device.submit(kernel, shots=args.shots, max_threads=thread_limit)
    except ValueError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    result = job.result()
    if args.log_file:
        logs = result.get("logs", [])
        try:
            _write_log_file(args.log_file, logs)
        except OSError as exc:
            print(f"Error writing log file {args.log_file!r}: {exc}", file=sys.stderr)
            return 1
        result.pop("logs", None)

    if args.output == "json":
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        _summarize_result(
            result,
            device=args.device,
            profile=device.profile,
            shots=args.shots,
        )
    return 0


def _load_profile_config(path: str) -> Mapping[str, Any]:
    if not os.path.exists(path):
        raise SystemExit(f"Profile config path {path!r} does not exist")
    try:
        with open(path, "r", encoding="utf-8") as fh:
            payload = json.load(fh)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"Invalid profile JSON: {exc}") from exc
    if not isinstance(payload, Mapping):
        raise SystemExit("Profile config must be a JSON object")
    if "positions" not in payload:
        raise SystemExit("Profile config missing required 'positions' field")
    return payload


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="quera-vm", description="Neutral Atom VM CLI")
    subparsers = parser.add_subparsers(dest="command", required=True)

    run_parser = subparsers.add_parser("run", help="Run a kernel on a VM device")
    run_parser.add_argument(
        "--output",
        choices=("summary", "json"),
        default="summary",
        help="Output format (summary or json)",
    )
    run_parser.add_argument(
        "--profile-config",
        help=(
            "Path to a JSON file describing a custom profile (positions, "
            "blockade_radius, optional noise). Overrides --profile when set."
        ),
    )
    run_parser.add_argument(
        "--device",
        default="quera.na_vm.sim",
        help="Device identifier (e.g. quera.na_vm.sim, runtime)",
    )
    run_parser.add_argument(
        "--profile",
        default="ideal_small_array",
        help="Device profile name (e.g. ideal_small_array)",
    )
    run_parser.add_argument(
        "--shots",
        type=int,
        default=1,
        help="Number of shots to execute",
    )
    run_parser.add_argument(
        "--threads",
        type=int,
        default=0,
        help="Maximum number of worker threads for shot execution (0=auto)",
    )
    run_parser.add_argument(
        "--log-file",
        help="Path to dump detailed VM logs (JSON array) instead of embedding them in stdout.",
    )
    run_parser.add_argument(
        "target",
        help="Kernel target to run, as MODULE:FUNC",
    )

    args = parser.parse_args(list(argv) if argv is not None else None)

    if args.command == "run":
        return _cmd_run(args)

    parser.error(f"Unknown command {args.command!r}")
