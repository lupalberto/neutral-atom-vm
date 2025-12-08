from __future__ import annotations

import logging
import time
from typing import Any, Callable, Mapping

import requests
from requests import RequestException

from .job import JobRequest

logger = logging.getLogger("neutral_atom_vm.service_client")


class RemoteServiceError(RuntimeError):
    pass


def submit_job_to_service(
    job: JobRequest | Mapping[str, Any],
    service_url: str,
    *,
    timeout: float | int | None = 30.0,
    status_callback: Callable[[Mapping[str, Any]], None] | None = None,
) -> Mapping[str, Any]:
    """Submit a job dict to a remote HTTP VM service and return the parsed response."""
    if isinstance(job, JobRequest):
        payload = job.to_dict()
    else:
        payload = dict(job)

    try:
        response = requests.post(service_url, json=payload, timeout=timeout)
        response.raise_for_status()
    except RequestException as exc:
        logger.exception("Failed to submit job to %s", service_url)
        raise RemoteServiceError(f"failed to contact remote service {service_url}: {exc}") from exc

    data = _decode_response(response)
    job_id = data.get("job_id")
    if not job_id:
        raise RemoteServiceError("remote service response missing job_id")

    status_url = _job_item_url(service_url, job_id, "status")
    result_url = _job_item_url(service_url, job_id, "result")

    while True:
        status_payload = _poll_status(status_url, timeout)
        if status_callback:
            try:
                status_callback(status_payload)
            except Exception:
                logger.exception("Status callback raised; continuing")
        status = status_payload.get("status", "").lower()
        if status in _TERMINAL_STATUSES:
            break
        time.sleep(_POLL_INTERVAL)

    return _poll_result(result_url, timeout)


def _poll_status(status_url: str, timeout: float | int | None) -> Mapping[str, Any]:
    while True:
        try:
            response = requests.get(status_url, timeout=timeout)
        except RequestException as exc:
            logger.exception("Failed to poll status at %s", status_url)
            raise RemoteServiceError(f"failed to poll remote service {status_url}: {exc}") from exc

        if response.status_code == 404:
            time.sleep(_POLL_INTERVAL)
            continue

        response.raise_for_status()
        return _decode_response(response)


def _poll_result(result_url: str, timeout: float | int | None) -> Mapping[str, Any]:
    while True:
        try:
            response = requests.get(result_url, timeout=timeout)
        except RequestException as exc:
            logger.exception("Failed to poll result at %s", result_url)
            raise RemoteServiceError(f"failed to poll remote service {result_url}: {exc}") from exc

        if response.status_code == 404:
            time.sleep(_POLL_INTERVAL)
            continue

        response.raise_for_status()
        return _decode_response(response)


def _decode_response(response: requests.Response) -> Mapping[str, Any]:
    try:
        payload = response.json()
    except ValueError as exc:
        raise RemoteServiceError("remote service returned invalid JSON") from exc
    if not isinstance(payload, Mapping):
        raise RemoteServiceError("remote service returned an unexpected payload")
    return payload


_TERMINAL_STATUSES = {"completed", "failed"}


def _job_item_url(service_url: str, job_id: str, suffix: str) -> str:
    base = service_url.rstrip("/")
    return f"{base}/{job_id}/{suffix}"


_POLL_INTERVAL = 0.1
