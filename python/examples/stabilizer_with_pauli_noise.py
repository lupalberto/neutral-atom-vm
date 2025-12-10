from bloqade import squin

@squin.kernel
def stabilizer_with_pauli_noise():
    data = squin.qalloc(4)
    anc_x = squin.qalloc(1)
    anc_z = squin.qalloc(1)

    for idx in (0, 2):
        squin.h(data[idx])
    squin.x(data[3])

    for control in (0, 2):
        target = control + 1
        squin.cx(data[control], data[target])
        squin.single_qubit_pauli_channel(0.05, 0.0, 0.0, data[target])

    squin.h(data[0])
    squin.h(data[1])
    squin.cx(data[0], anc_x[0])
    squin.cx(data[1], anc_x[0])
    squin.h(data[0])
    squin.h(data[1])
    squin.single_qubit_pauli_channel(0.02, 0.0, 0.0, anc_x[0])
    squin.measure(anc_x)

    squin.cx(data[1], anc_z[0])
    squin.cx(data[2], anc_z[0])
    squin.single_qubit_pauli_channel(0.0, 0.0, 0.02, anc_z[0])
    squin.measure(anc_z)

    squin.measure(data)

