"""Example showing how to run a repetition-code QEC workflow."""

from neutral_atom_vm import qec


def repetition_code_example() -> None:
    """Execute a distance-3 repetition code and print logical error metrics."""

    result = qec.repetition_code_job(distance=3, rounds=2, shots=128)
    metrics = qec.compute_repetition_code_metrics(result, distance=3, rounds=2)
    print("Repetition code metrics:")
    print(f"  logical_x_error_rate = {metrics['logical_x_error_rate']:.6f}")
    print(f"  shots = {metrics['shots']}")
    print(f"  distance = {metrics['distance']}")
    print(f"  rounds = {metrics['rounds']}")


if __name__ == "__main__":
    repetition_code_example()
