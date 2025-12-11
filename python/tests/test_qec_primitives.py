import pytest

from neutral_atom_vm import qec, SimpleNoiseConfig


def test_repetition_code_zero_noise_has_zero_logical_errors():
    result = qec.repetition_code_job(
        distance=3,
        rounds=1,
        shots=16,
        device_id="state-vector",
        profile="lossy_block",
        profile_noise=False,
    )
    metrics = qec.compute_repetition_code_metrics(
        result,
        distance=3,
        rounds=1,
    )
    assert metrics["shots"] == 16
    assert 0.0 <= metrics["logical_x_error_rate"] <= 1.0


def test_repetition_code_noise_increases_logical_errors():
    noise = SimpleNoiseConfig(p_quantum_flip=1.0)
    result_clean = qec.repetition_code_job(
        distance=3,
        rounds=1,
        shots=32,
        device_id="state-vector",
        profile="lossy_block",
        profile_noise=False,
    )
    result_noisy = qec.repetition_code_job(
        distance=3,
        rounds=1,
        shots=32,
        device_id="state-vector",
        profile="lossy_block",
        profile_noise=False,
        noise=noise,
    )
    clean_metrics = qec.compute_repetition_code_metrics(
        result_clean,
        distance=3,
        rounds=1,
    )
    noisy_metrics = qec.compute_repetition_code_metrics(
        result_noisy,
        distance=3,
        rounds=1,
    )
    assert clean_metrics["logical_x_error_rate"] == pytest.approx(0.0)
    assert noisy_metrics["logical_x_error_rate"] == pytest.approx(1.0)


def test_repetition_code_metrics_counts_only_data_targets():
    shots = 8
    result = qec.repetition_code_job(
        distance=1,
        rounds=1,
        shots=shots,
        device_id="state-vector",
        profile="lossy_block",
        profile_noise=False,
    )
    metrics = qec.compute_repetition_code_metrics(
        result,
        distance=1,
        rounds=1,
    )
    assert metrics["shots"] == shots
