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


@squin.kernel
def loop_cx():
    q = squin.qalloc(2)
    for _ in range(3):
        squin.cx(q[0], q[1])
    squin.measure(q)


@squin.kernel
def loop_over_symbolic_n():
    """Kernel that loops over range(n) where n is a local symbol.

    This exercises the lowering path where the loop iterable is the result
    of a `range` call rather than a literal list.
    """
    n = 4
    q = squin.qalloc(n)
    for i in range(n):
        squin.x(q[i])
    squin.measure(q)


@squin.kernel
def nested_loops_over_range():
    """Kernel with nested for-loops over range to stress lowering."""

    n = 4
    q = squin.qalloc(n)
    for _ in range(2):
        for i in range(n):
            squin.x(q[i])
    squin.measure(q)


@squin.kernel
def grid_entangle_4x4():
    """Entangle a 4x4 grid of qubits using only nearest-neighbor CX."""

    n_rows = 4
    n_cols = 4
    n = n_rows * n_cols
    q = squin.qalloc(n)

    for i in range(n):
        squin.h(q[i])

    # Horizontal edges.
    for r in range(n_rows):
        for c in range(n_cols - 1):
            a = r * n_cols + c
            b = r * n_cols + (c + 1)
            squin.cx(q[a], q[b])

    # Vertical edges.
    for r in range(n_rows - 1):
        for c in range(n_cols):
            a = r * n_cols + c
            b = (r + 1) * n_cols + c
            squin.cx(q[a], q[b])

    squin.measure(q)


@squin.kernel
def benchmark_chain_complex():
    """More complex kernel that exercises multi-qubit gates on a 1D chain.

    This kernel is intended to be run on 1D chain profiles such as
    ``benchmark_chain`` to exercise nearest-neighbor CX gates, loops,
    and additional single-qubit gates under a realistic noise model.
    """

    q = squin.qalloc(8)

    # Prepare a GHZ-like backbone along the chain using nearest-neighbor CX,
    # respecting the native gate catalog for the benchmark_chain profile
    # (X, H, Z, CX).
    squin.h(q[0])
    for i in range(7):
        squin.cx(q[i], q[i + 1])

    # Apply additional Z rotations to expose more single-qubit gate activity
    # without introducing unsupported parametric gates.
    for i in range(8):
        squin.z(q[i])

    squin.measure(q)
