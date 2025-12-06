#pragma once

#include <cstdint>
#include <cstdint>
#include <random>
#include <vector>

#include "vm/measurement_record.types.hpp"

// Configuration for classical readout noise on measurement outcomes.
// Probabilities are per bit and must lie in [0, 1].
struct MeasurementNoiseConfig {
    double p_flip0_to_1 = 0.0;
    double p_flip1_to_0 = 0.0;
};

// Per-qubit Pauli error probabilities. The probability of applying the
// identity is 1 - (px + py + pz).
struct SingleQubitPauliConfig {
    double px = 0.0;
    double py = 0.0;
    double pz = 0.0;
};

// Gate-level noise configuration. For simplicity, the prototype uses
// global per-gate-type Pauli channels rather than per-gate-instance tables.
struct GateNoiseConfig {
    // Noise applied to the target of single-qubit gates.
    SingleQubitPauliConfig single_qubit;

    // Noise applied independently to control and target of two-qubit gates.
    SingleQubitPauliConfig two_qubit_control;
    SingleQubitPauliConfig two_qubit_target;
};

// Aggregated measurement-time noise model, combining:
// - Quantum bit-flip-like effects accumulated before measurement.
// - Classical readout noise.
// - Atom loss / erasure at measurement.
struct SimpleNoiseConfig {
    // Symmetric effective bit-flip probability capturing upstream quantum
    // errors that manifest as X/Y-like flips in the measurement basis.
    double p_quantum_flip = 0.0;

    // Probability that a measured qubit is lost / erased. When triggered,
    // the corresponding measurement bit is set to -1.
    double p_loss = 0.0;

    // Classical readout noise on top of the quantum and loss contributions.
    MeasurementNoiseConfig readout{};

    // Effective Pauli channels applied after gates.
    GateNoiseConfig gate{};

    // Idle/dephasing rate per second for `Wait`/idle periods.
    double idle_rate = 0.0;
};

class NoiseEngine {
  public:
    virtual ~NoiseEngine() = default;

    virtual void apply_measurement_noise(
        MeasurementRecord& record,
        std::mt19937_64& rng
    ) const = 0;

    // Optional hooks for gate and idle noise. Default implementations
    // are no-ops to keep engines composable.
    virtual void apply_single_qubit_gate_noise(
        int /*target*/,
        int /*n_qubits*/,
        std::vector<std::complex<double>>& /*amplitudes*/,
        std::mt19937_64& /*rng*/
    ) const {}

    virtual void apply_two_qubit_gate_noise(
        int /*q0*/,
        int /*q1*/,
        int /*n_qubits*/,
        std::vector<std::complex<double>>& /*amplitudes*/,
        std::mt19937_64& /*rng*/
    ) const {}

    virtual void apply_idle_noise(
        int /*n_qubits*/,
        std::vector<std::complex<double>>& /*amplitudes*/,
        double /*duration*/,
        std::mt19937_64& /*rng*/
    ) const {}
};

// Simple engine that realizes SimpleNoiseConfig as a measurement-time
// stochastic channel. It does not modify the underlying statevector, only
// the recorded classical bits.
class SimpleNoiseEngine : public NoiseEngine {
  public:
    explicit SimpleNoiseEngine(SimpleNoiseConfig config);

    void apply_measurement_noise(
        MeasurementRecord& record,
        std::mt19937_64& rng
    ) const override;

    void apply_single_qubit_gate_noise(
        int target,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        std::mt19937_64& rng
    ) const override;

    void apply_two_qubit_gate_noise(
        int q0,
        int q1,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        std::mt19937_64& rng
    ) const override;

    void apply_idle_noise(
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        double duration,
        std::mt19937_64& rng
    ) const override;

  private:
    SimpleNoiseConfig config_;

    static void validate_config(const SimpleNoiseConfig& config);
};
