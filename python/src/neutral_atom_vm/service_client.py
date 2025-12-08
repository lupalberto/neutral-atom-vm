from __future__ import annotations

import logging
from typing import Any, Mapping

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

    try:
        data = response.json()
    except ValueError as exc:
        raise RemoteServiceError("remote service returned invalid JSON") from exc

    if not isinstance(data, Mapping):
        raise RemoteServiceError("remote service returned an unexpected payload")

    return data
