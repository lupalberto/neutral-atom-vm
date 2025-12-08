import pytest
from typing import Any, Mapping
import requests

from neutral_atom_vm.service_client import RemoteServiceError, submit_job_to_service


class DummyResponse:
    def __init__(self, payload=None, status_code=200):
        self._payload = payload or {}
        self.status_code = status_code

    def json(self):
        return self._payload

    def raise_for_status(self):
        if not 200 <= self.status_code < 300:
            raise requests.HTTPError(response=self)

    @property
    def ok(self):
        return 200 <= self.status_code < 300


def test_submit_job_to_service_polls_until_result(monkeypatch):
    post_called = False
    job_payload = {"program": [], "hardware": {"positions": [0.0]}}

    def fake_post(url, json, timeout):
        nonlocal post_called
        assert url == "http://localhost:8080/job"
        assert json == job_payload
        post_called = True
        return DummyResponse(payload={"job_id": "job-0", "status": "pending"})

    status_payloads = [
        {"job_id": "job-0", "status": "pending", "percent_complete": 0.0},
        {"job_id": "job-0", "status": "running", "percent_complete": 0.3},
        {"job_id": "job-0", "status": "completed", "percent_complete": 1.0},
    ]
    status_iter = iter(status_payloads)

    def fake_get(url, timeout):
        if url.endswith("/status"):
            try:
                payload = next(status_iter)
            except StopIteration:
                payload = status_payloads[-1]
            return DummyResponse(payload=payload)
        if url.endswith("/result"):
            return DummyResponse(payload={"job_id": "job-0", "status": "completed", "measurements": []})
        raise AssertionError("unexpected URL")

    monkeypatch.setattr("requests.post", fake_post)
    monkeypatch.setattr("requests.get", fake_get)

    statuses: list[Mapping[str, Any]] = []

    def status_cb(status_payload: Mapping[str, Any]) -> None:
        statuses.append(status_payload)

    result = submit_job_to_service(
        job_payload,
        "http://localhost:8080/job",
        timeout=0.1,
        status_callback=status_cb,
    )
    assert result["status"] == "completed"
    assert post_called
    assert statuses


def test_submit_job_to_service_handles_errors(monkeypatch):
    def fake_post(*args, **kwargs):
        return DummyResponse(status_code=500)

    monkeypatch.setattr("requests.post", fake_post)

    with pytest.raises(RemoteServiceError):
        submit_job_to_service({"program": []}, "http://localhost:8080/job")
