"""Stabilizer-friendly kernel that exercises Stim via chained parity checks.

Example:
    quera-vm run --device stabilizer --profile ideal_square_grid \
        python.examples.stabilizer_syndrome_cycle:stabilizer_syndrome_cycle
"""

from bloqade import squin


@squin.kernel
def stabilizer_syndrome_cycle():
    # Four data qubits plus two ancilla qubits used for X- and Z-parity checks.
    data = squin.qalloc(4)
    anc_x = squin.qalloc(1)
    anc_z = squin.qalloc(1)

    # Prepare data register in a pattern mixing |+> and |1>, forcing Stim to
    # track both X- and Z-type correlations.
    for idx in (0, 2):
        squin.h(data[idx])
    squin.x(data[3])

    # Build two short Bell pairs to provide entanglement structure.
    squin.cx(data[0], data[1])
    squin.cx(data[2], data[3])

    # X stabilizer check spanning qubits 0 and 1 via ancilla anc_x.
    squin.h(data[0])
    squin.h(data[1])
    squin.cx(data[0], anc_x[0])
    squin.cx(data[1], anc_x[0])
    squin.h(data[0])
    squin.h(data[1])
    squin.measure(anc_x)

    # Z stabilizer check across qubits 1 and 2 (adjacent under the 2D grid).
    squin.cx(data[1], anc_z[0])
    squin.cx(data[2], anc_z[0])
    squin.measure(anc_z)

    # Final readout of all data qubits to correlate with the stabilizer outcomes.
    squin.measure(data)
