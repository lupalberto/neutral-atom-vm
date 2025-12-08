#!/usr/bin/env python3
"""Lightweight HTTP service that submits JSON jobs to the Neutral Atom VM."""

from __future__ import annotations

import argparse
import json
import logging
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Mapping

from neutral_atom_vm.job import submit_job


logger = logging.getLogger("neutral_atom_vm.vm_service")


class VMJobRequestHandler(BaseHTTPRequestHandler):
    job_endpoint = "/job"
    default_profile: Mapping[str, Any] | None = None

    def log_message(self, format: str, *args: object) -> None:  # pragma: no cover - coverage not needed for service stub
        logger.info("%s - - %s", self.client_address[0], format % args)

    def do_GET(self) -> None:  # pragma: no cover - coverage not needed for service stub
        if self.path == "/healthz":
            self._send_json({"status": "ok"})
        else:
            self.send_error(404, "not found")

    def do_POST(self) -> None:
        if self.path != self.job_endpoint:
            self.send_error(404, "job endpoint is %s" % self.job_endpoint)
            return

        length = int(self.headers.get("Content-Length", 0))
        if length <= 0:
            self.send_error(411, "Content-Length required")
            return

        body = self.rfile.read(length)
        try:
            payload = json.loads(body)
        except json.JSONDecodeError as exc:
            self.send_error(400, f"invalid json: {exc}")
            return

        if not isinstance(payload, Mapping):
            self.send_error(400, "job payload must be a JSON object")
            return

        if self.default_profile:
            payload = dict(self.default_profile, **payload)

        try:
            result = submit_job(payload)
        except Exception as exc:  # pragma: no cover - defensive guard
            logger.exception("Job submission failed")
            self.send_error(500, f"job submission failed: {exc}")
            return

        self._send_json(result)

    def _send_json(self, body: Any, status: int = 200) -> None:
        payload = json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


def main() -> None:
    parser = argparse.ArgumentParser(description="Run a Neutral Atom VM HTTP service")
    parser.add_argument("--host", default="0.0.0.0", help="Listen address")
    parser.add_argument("--port", type=int, default=8080, help="Listen port")
    parser.add_argument(
        "--job-endpoint",
        default="/job",
        help="HTTP path that accepts job JSON payloads",
    )
    parser.add_argument(
        "--profile",
        default=None,
        help="Profile config JSON path that will be merged into every job",
    )
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s %(message)s",
        stream=sys.stdout,
    )

    if args.profile:
        logger.info("Service always enqueues jobs with custom profile %s", args.profile)
        try:
            with open(args.profile, encoding="utf-8") as fh:
                profile_data = json.load(fh)
        except OSError as exc:
            raise SystemExit(f"unable to read profile config: {exc}") from exc
        except json.JSONDecodeError as exc:
            raise SystemExit(f"invalid profile JSON: {exc}") from exc

        if not isinstance(profile_data, Mapping):
            raise SystemExit("profile config must be a JSON object")

        VMJobRequestHandler.default_profile = profile_data

    VMJobRequestHandler.job_endpoint = args.job_endpoint

    server = ThreadingHTTPServer((args.host, args.port), VMJobRequestHandler)
    logger.info("Serving Neutral Atom VM on http://%s:%s%s", args.host, args.port, args.job_endpoint)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        logger.info("Shutting down service")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
