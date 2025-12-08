from bloqade import squin


@squin.kernel
def ghz():
    q = squin.qalloc(3)
    squin.h(q[0])
    squin.cx(q[0], q[1])
    squin.cx(q[0], q[2])
    squin.measure(q)
