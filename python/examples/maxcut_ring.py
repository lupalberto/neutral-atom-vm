from bloqade import squin

@squin.kernel
def grid_entangle_4x4():
    """4x4 grid entangling program for the noisy_square_array profile.

    Uses H on all 16 qubits, then CX gates along all horizontal and
    vertical nearest-neighbor edges of a 4x4 grid.
    """
    q = squin.qalloc(16)

    # Put all qubits into |+>
    for i in range(16):
        squin.h(q[i])

    # --- Horizontal neighbors ---
    # Row 0: 0-1, 1-2, 2-3
    squin.cx(q[0], q[1])
    squin.cx(q[1], q[2])
    squin.cx(q[2], q[3])

    # Row 1: 4-5, 5-6, 6-7
    squin.cx(q[4], q[5])
    squin.cx(q[5], q[6])
    squin.cx(q[6], q[7])

    # Row 2: 8-9, 9-10, 10-11
    squin.cx(q[8], q[9])
    squin.cx(q[9], q[10])
    squin.cx(q[10], q[11])

    # Row 3: 12-13, 13-14, 14-15
    squin.cx(q[12], q[13])
    squin.cx(q[13], q[14])
    squin.cx(q[14], q[15])

    # --- Vertical neighbors ---
    # Col 0: 0-4, 4-8, 8-12
    squin.cx(q[0], q[4])
    squin.cx(q[4], q[8])
    squin.cx(q[8], q[12])

    # Col 1: 1-5, 5-9, 9-13
    squin.cx(q[1], q[5])
    squin.cx(q[5], q[9])
    squin.cx(q[9], q[13])

    # Col 2: 2-6, 6-10, 10-14
    squin.cx(q[2], q[6])
    squin.cx(q[6], q[10])
    squin.cx(q[10], q[14])

    # Col 3: 3-7, 7-11, 11-15
    squin.cx(q[3], q[7])
    squin.cx(q[7], q[11])
    squin.cx(q[11], q[15])

    squin.measure(q)