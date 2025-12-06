from bloqade import squin


@squin.kernel
def bell_pair():
    q = squin.qalloc(2)
    squin.h(q[0])
    squin.cx(q[0], q[1])
    squin.measure(q)


@squin.kernel
def single_qubit_rotations():
    q = squin.qalloc(1)
    squin.rx(0.125, q[0])
    squin.ry(0.25, q[0])
    squin.rz(0.5, q[0])
    squin.measure(q)
