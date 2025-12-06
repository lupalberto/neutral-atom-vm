#pragma once

#include <complex>
#include <vector>

#include "noise.hpp"

void apply_pauli_x(
    std::vector<std::complex<double>>& state,
    int n_qubits,
    int target
);

void apply_pauli_y(
    std::vector<std::complex<double>>& state,
    int n_qubits,
    int target
);

void apply_pauli_z(
    std::vector<std::complex<double>>& state,
    int n_qubits,
    int target
);

char sample_pauli(
    const SingleQubitPauliConfig& cfg,
    RandomStream& rng
);

void apply_single_qubit_pauli(
    char pauli,
    std::vector<std::complex<double>>& state,
    int n_qubits,
    int target
);

double sample_phase_angle(double magnitude, RandomStream& rng);

void apply_phase_rotation(
    std::vector<std::complex<double>>& state,
    int n_qubits,
    int target,
    double theta
);
