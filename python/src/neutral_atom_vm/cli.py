from __future__ import annotations

import argparse
import importlib
import json
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

    if counts:
        print("Counts:")
        for bitstring, count in sorted(counts.items()):
            print(f"  {bitstring}: {count}")
    else:
        print("Counts: (no measurements)")


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
        "target",
        help="Kernel target to run, as MODULE:FUNC",
    )

    args = parser.parse_args(list(argv) if argv is not None else None)

    if args.command == "run":
        return _cmd_run(args)

    parser.error(f"Unknown command {args.command!r}")
