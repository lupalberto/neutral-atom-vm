#pragma once

#include <array>
#include <cstdint>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>
#include <complex>

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

// Correlated two-qubit Pauli probabilities arranged as a 4x4 table in
// control-target order (I, X, Y, Z). Total probability should not exceed 1.
struct TwoQubitCorrelatedPauliConfig {
    std::array<double, 16> matrix{};
};

struct LossRuntimeConfig {
    double per_gate = 0.0;
    double idle_rate = 0.0;  // per second
};

// Phase-noise configuration describing random Z-rotations applied after
// gates or during idle periods. Magnitudes are interpreted as maximum
// absolute phase kicks in radians; sampling draws uniformly from
// [-magnitude, +magnitude]. The idle term is a per-second magnitude so that
// longer waits accumulate proportionally larger drifts.
struct PhaseNoiseConfig {
    double single_qubit = 0.0;
    double two_qubit_control = 0.0;
    double two_qubit_target = 0.0;
    double idle = 0.0;
};

struct AmplitudeDampingConfig {
    double per_gate = 0.0;
    double idle_rate = 0.0;
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

    // Correlated two-qubit Pauli channel applied after entangling gates.
    TwoQubitCorrelatedPauliConfig correlated_gate{};

    // Idle/dephasing rate per second for `Wait`/idle periods.
    double idle_rate = 0.0;

    // Random Z-phase kicks applied around gates and idles.
    PhaseNoiseConfig phase{};

    // Probabilistic amplitude damping applied after gates/idle periods.
    AmplitudeDampingConfig amplitude_damping{};

    // Runtime loss probabilities (per gate or per second during idle).
    LossRuntimeConfig loss_runtime{};
};

class RandomStream {
  public:
    virtual ~RandomStream() = default;
    virtual double uniform(double lo = 0.0, double hi = 1.0) = 0;
};

class StdRandomStream : public RandomStream {
  public:
    explicit StdRandomStream(std::mt19937_64& rng);

    double uniform(double lo, double hi) override;

  private:
    std::mt19937_64& rng_;
};

class NoiseEngine {
  public:
    virtual ~NoiseEngine() = default;

    virtual std::shared_ptr<const NoiseEngine> clone() const = 0;

    // Optional hooks for measurement, gate, and idle noise. Default
    // implementations are no-ops to keep engines composable.
    virtual void apply_measurement_noise(
        MeasurementRecord& /*record*/,
        RandomStream& /*rng*/
    ) const {}

    virtual void apply_single_qubit_gate_noise(
        int /*target*/,
        int /*n_qubits*/,
        std::vector<std::complex<double>>& /*amplitudes*/,
        RandomStream& /*rng*/
    ) const {}

    virtual void apply_two_qubit_gate_noise(
        int /*q0*/,
        int /*q1*/,
        int /*n_qubits*/,
        std::vector<std::complex<double>>& /*amplitudes*/,
        RandomStream& /*rng*/
    ) const {}

    virtual void apply_idle_noise(
        int /*n_qubits*/,
        std::vector<std::complex<double>>& /*amplitudes*/,
        double /*duration*/,
        RandomStream& /*rng*/
    ) const {}
};

class CompositeNoiseEngine : public NoiseEngine {
  public:
    CompositeNoiseEngine() = default;
    explicit CompositeNoiseEngine(
        std::vector<std::shared_ptr<const NoiseEngine>> sources
    );

    void add_source(std::shared_ptr<const NoiseEngine> source);

    std::shared_ptr<const NoiseEngine> clone() const override;

    void apply_measurement_noise(
        MeasurementRecord& record,
        RandomStream& rng
    ) const override;

    void apply_single_qubit_gate_noise(
        int target,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        RandomStream& rng
    ) const override;

    void apply_two_qubit_gate_noise(
        int q0,
        int q1,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        RandomStream& rng
    ) const override;

    void apply_idle_noise(
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        double duration,
        RandomStream& rng
    ) const override;

  protected:
    const std::vector<std::shared_ptr<const NoiseEngine>>& sources() const {
        return sources_;
    }

  private:
    std::vector<std::shared_ptr<const NoiseEngine>> sources_;
};

// Simple engine realized as a composition of smaller noise sources.
class SimpleNoiseEngine : public CompositeNoiseEngine {
  public:
    explicit SimpleNoiseEngine(SimpleNoiseConfig config);

    std::shared_ptr<const NoiseEngine> clone() const override;

  private:
    static void validate_config(const SimpleNoiseConfig& config);
    static std::vector<std::shared_ptr<const NoiseEngine>> build_sources(
        const SimpleNoiseConfig& config
    );
};
