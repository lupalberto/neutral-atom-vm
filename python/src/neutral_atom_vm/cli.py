from __future__ import annotations

import argparse
import importlib
import json
import logging
import os
import runpy
import shlex
import subprocess
import sys
import time
from collections import Counter
from typing import Any, Callable, Mapping, Sequence

from .device import connect_device, build_device_from_config
from .job import JobRequest, job_result, job_status, submit_job_async
from .layouts import GridLayout, grid_layout_for_profile
from .service_client import RemoteServiceError, submit_job_to_service


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


def _display_bit(bit: int) -> str:
    if bit == -1:
        return "?"
    return str(bit)


def _grid_lines_for_bits(
    bits: Sequence[int],
    layout: GridLayout,
) -> list[str] | None:
    if len(bits) != layout.total_slots:
        return None
    lines: list[str] = []
    idx = 0
    for layer in range(layout.layers):
        if layout.layers > 1:
            lines.append(f"Layer {layer + 1}:")
        for _ in range(layout.rows):
            row_bits = bits[idx : idx + layout.cols]
            idx += layout.cols
            lines.append(" ".join(_display_bit(bit) for bit in row_bits))
    return lines


def _summarize_result(
    result: Mapping[str, Any],
    *,
    device: str,
    profile: str | None,
    shots: int,
) -> str:
    status = result.get("status", "unknown")
    elapsed = result.get("elapsed_time", None)
    measurements = result.get("measurements", [])
    lines: list[str] = []

    lines.append(f"Device: {device} (profile={profile!r})")
    lines.append(f"Shots: {shots}")
    lines.append(f"Status: {status}")
    if elapsed is not None:
        lines.append(f"Elapsed time: {elapsed:.6f} s")
    message = result.get("message")
    if message:
        lines.append(f"Message: {message}")

    counts: Counter[str] = Counter()
    for rec in measurements:
        bits = rec.get("bits", [])
        if not bits:
            continue
        key = "".join(str(int(b)) for b in bits)
        counts[key] += 1

    grid_layout = grid_layout_for_profile(profile)
    grid_examples: dict[str, list[int]] = {}

    for rec in measurements:
        bits = rec.get("bits", [])
        if not bits:
            continue
        key = "".join(str(int(b)) for b in bits)
        if grid_layout and len(bits) == grid_layout.total_slots:
            grid_examples.setdefault(key, list(int(b) for b in bits))

    if counts:
        lines.append("Counts:")
        for bitstring, count in sorted(counts.items()):
            lines.append(f"  {bitstring}: {count}")
            if grid_layout:
                grid_lines = _grid_lines_for_bits(
                    grid_examples.get(bitstring, []), grid_layout
                )
                if grid_lines:
                    lines.append("    Grid:")
                    for line in grid_lines:
                        lines.append(f"      {line}")
    else:
        lines.append("Counts: (no measurements)")

    return "\n".join(lines)


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


def _page_output(text: str) -> None:
    if not text.endswith("\n"):
        text += "\n"
    pager_cmd = os.environ.get("PAGER", "less -R")
    try:
        pager_args = shlex.split(pager_cmd)
    except ValueError:
        pager_args = pager_cmd.split()
    if not pager_args:
        print(text, end="")
        return
    try:
        subprocess.run(pager_args, input=text, encoding="utf-8", check=False)
    except OSError:
        print(text, end="")


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

    status_callback: Callable[[Mapping[str, Any]], None] | None = None
    status_line_active = False
    last_status_line: str | None = None
    bar_width = 20
    line_width = 0
    progress_enabled = args.service_url or sys.stderr.isatty()

    def log_status(payload: Mapping[str, Any]) -> None:
        nonlocal last_status_line
        nonlocal line_width
        nonlocal status_line_active
        job_id = payload.get("job_id", "<unknown>")
        status_name = payload.get("status", "unknown")
        percent = payload.get("percent_complete")
        if isinstance(percent, (int, float)):
            progress = max(0.0, min(1.0, float(percent)))
            filled = int(progress * bar_width)
            bar = "[" + "#" * filled + "-" * (bar_width - filled) + "]"
            status_line = (
                f"Job {job_id}: {status_name} {bar} {progress * 100:.0f}%"
            )
        else:
            status_line = f"Job {job_id}: {status_name}"
        if status_line != last_status_line:
            line_width = max(line_width, len(status_line))
            padded = status_line.ljust(line_width)
            print(f"\r{padded}", end="", file=sys.stderr, flush=True)
            status_line_active = True
            last_status_line = status_line

    if progress_enabled:
        status_callback = log_status

    try:
        job_request = device.build_job_request(
            kernel,
            shots=args.shots,
            max_threads=thread_limit,
        )
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 1
    try:
        if args.service_url:
            result = submit_job_to_service(
                job_request,
                args.service_url,
                timeout=args.service_timeout,
                status_callback=status_callback,
            )
        else:
            result = _run_local_job_with_progress(
                job_request,
                status_callback=status_callback,
            )
    except ValueError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    except RemoteServiceError as exc:
        print(f"Remote service error: {exc}", file=sys.stderr)
        return 1

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
        summary_text = _summarize_result(
            result,
            device=args.device,
            profile=device.profile,
            shots=args.shots,
        )
        use_pager = args.use_pager
        if use_pager is None:
            use_pager = sys.stdout.isatty()
        if use_pager and summary_text:
            _page_output(summary_text)
        else:
            print(summary_text)

    if status_line_active:
        print(file=sys.stderr)
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


def _run_local_job_with_progress(
    job_request: JobRequest,
    *,
    status_callback: Callable[[Mapping[str, Any]], None] | None = None,
) -> Mapping[str, Any]:
    submission = submit_job_async(job_request)
    job_id = submission.get("job_id")
    if not job_id:
        raise RemoteServiceError("async submission returned no job_id")
    while True:
        status_payload = job_status(job_id)
        if status_callback:
            status_callback(status_payload)
        status = status_payload.get("status", "").lower()
        if status in {"completed", "failed"}:
            break
        time.sleep(_LOCAL_POLL_INTERVAL)
    return job_result(job_id)


_LOCAL_POLL_INTERVAL = 0.1


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
        default="local-cpu",
        help="Device identifier (e.g. local-cpu, local-arc)",
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
        "--service-url",
        help="Post the job JSON to a remote Neutral Atom VM service instead of running locally.",
    )
    run_parser.add_argument(
        "--service-timeout",
        type=float,
        default=30.0,
        help="Seconds to wait when contacting the remote service (only used with --service-url).",
    )
    pager_group = run_parser.add_mutually_exclusive_group()
    pager_group.add_argument(
        "--pager",
        dest="use_pager",
        action="store_true",
        help="Display summary output through the configured pager (default when stdout is a TTY).",
    )
    pager_group.add_argument(
        "--no-pager",
        dest="use_pager",
        action="store_false",
        help="Disable pager output even if stdout is a TTY.",
    )
    run_parser.add_argument(
        "target",
        help="Kernel target to run, as MODULE:FUNC",
    )
    run_parser.set_defaults(use_pager=None)

    args = parser.parse_args(list(argv) if argv is not None else None)

    if args.command == "run":
        return _cmd_run(args)

    parser.error(f"Unknown command {args.command!r}")
