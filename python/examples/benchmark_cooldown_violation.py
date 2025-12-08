from bloqade import squin


@squin.kernel
def reuse_measured_qubit():
    q = squin.qalloc(3)
    squin.h(q[0])
    squin.measure(q[0])
    squin.h(q[0])  # violates benchmark_chain cooldown without Wait
