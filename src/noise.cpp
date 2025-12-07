#include "noise.hpp"

#include <cmath>
#include <memory>
#include <random>
#include <stdexcept>
#include <utility>

#include "noise/correlated_pauli_source.hpp"
#include "noise/idle_dephasing_source.hpp"
#include "noise/idle_phase_drift_source.hpp"
#include "noise/loss_tracking_source.hpp"
#include "noise/amplitude_damping_source.hpp"
#include "noise/measurement_noise_source.hpp"
#include "noise/phase_kick_noise_source.hpp"
#include "noise/single_qubit_pauli_source.hpp"
#include "noise/two_qubit_pauli_source.hpp"

StdRandomStream::StdRandomStream(std::mt19937_64& rng) : rng_(rng) {}

double StdRandomStream::uniform(double lo, double hi) {
    if (hi <= lo) {
        return lo;
    }
    std::uniform_real_distribution<double> dist(lo, hi);
    return dist(rng_);
}

CompositeNoiseEngine::CompositeNoiseEngine(
    std::vector<std::shared_ptr<const NoiseEngine>> sources
)
    : sources_(std::move(sources)) {}

std::shared_ptr<const NoiseEngine> CompositeNoiseEngine::clone() const {
    std::vector<std::shared_ptr<const NoiseEngine>> clones;
    clones.reserve(sources_.size());
    for (const auto& source : sources_) {
        if (source) {
            clones.push_back(source->clone());
        }
    }
    return std::make_shared<CompositeNoiseEngine>(std::move(clones));
}

void CompositeNoiseEngine::add_source(std::shared_ptr<const NoiseEngine> source) {
    if (source) {
        sources_.push_back(std::move(source));
    }
}

void CompositeNoiseEngine::apply_measurement_noise(
    MeasurementRecord& record,
    RandomStream& rng
) const {
    for (const auto& source : sources_) {
        source->apply_measurement_noise(record, rng);
    }
}

void CompositeNoiseEngine::apply_single_qubit_gate_noise(
    int target,
    int n_qubits,
    std::vector<std::complex<double>>& amplitudes,
    RandomStream& rng
) const {
    for (const auto& source : sources_) {
        source->apply_single_qubit_gate_noise(target, n_qubits, amplitudes, rng);
    }
}

void CompositeNoiseEngine::apply_two_qubit_gate_noise(
    int q0,
    int q1,
    int n_qubits,
    std::vector<std::complex<double>>& amplitudes,
    RandomStream& rng
) const {
    for (const auto& source : sources_) {
        source->apply_two_qubit_gate_noise(q0, q1, n_qubits, amplitudes, rng);
    }
}

void CompositeNoiseEngine::apply_idle_noise(
    int n_qubits,
    std::vector<std::complex<double>>& amplitudes,
    double duration,
    RandomStream& rng
) const {
    for (const auto& source : sources_) {
        source->apply_idle_noise(n_qubits, amplitudes, duration, rng);
    }
}

SimpleNoiseEngine::SimpleNoiseEngine(SimpleNoiseConfig config)
    : CompositeNoiseEngine(build_sources(config)) {}

std::shared_ptr<const NoiseEngine> SimpleNoiseEngine::clone() const {
    return CompositeNoiseEngine::clone();
}

void SimpleNoiseEngine::validate_config(const SimpleNoiseConfig& config) {
    if (config.p_quantum_flip < 0.0 || config.p_quantum_flip > 1.0 ||
        config.p_loss < 0.0 || config.p_loss > 1.0 ||
        config.readout.p_flip0_to_1 < 0.0 || config.readout.p_flip0_to_1 > 1.0 ||
        config.readout.p_flip1_to_0 < 0.0 || config.readout.p_flip1_to_0 > 1.0) {
        throw std::invalid_argument("Noise probabilities must be in [0, 1]");
    }

    const auto check_pauli = [](const SingleQubitPauliConfig& c) {
        const double sum = c.px + c.py + c.pz;
        if (c.px < 0.0 || c.px > 1.0 ||
            c.py < 0.0 || c.py > 1.0 ||
            c.pz < 0.0 || c.pz > 1.0 ||
            sum > 1.0 + 1e-12) {
            throw std::invalid_argument(
                "Pauli channel probabilities must be in [0, 1] "
                "and px + py + pz <= 1");
        }
    };

    check_pauli(config.gate.single_qubit);
    check_pauli(config.gate.two_qubit_control);
    check_pauli(config.gate.two_qubit_target);

    if (config.phase.single_qubit < 0.0 ||
        config.phase.two_qubit_control < 0.0 ||
        config.phase.two_qubit_target < 0.0 ||
        config.phase.idle < 0.0) {
        throw std::invalid_argument("Phase noise magnitudes must be non-negative");
    }

    double correlated_sum = 0.0;
    for (double p : config.correlated_gate.matrix) {
        if (p < 0.0 || p > 1.0) {
            throw std::invalid_argument(
                "Correlated Pauli probabilities must be in [0, 1]");
        }
        correlated_sum += p;
    }
    if (correlated_sum > 1.0 + 1e-12) {
        throw std::invalid_argument(
            "Sum of correlated Pauli probabilities must be <= 1");
    }

    if (config.loss_runtime.per_gate < 0.0 ||
        config.loss_runtime.per_gate > 1.0 + 1e-12 ||
        config.loss_runtime.idle_rate < 0.0) {
        throw std::invalid_argument(
            "Loss runtime probabilities must be non-negative and <= 1 per gate");
    }

    if (config.amplitude_damping.per_gate < 0.0 ||
        config.amplitude_damping.per_gate > 1.0 ||
        config.amplitude_damping.idle_rate < 0.0) {
        throw std::invalid_argument(
            "Amplitude damping parameters must be in [0, 1] for per-gate and non-negative for idle");
    }
}

std::vector<std::shared_ptr<const NoiseEngine>> SimpleNoiseEngine::build_sources(
    const SimpleNoiseConfig& config
) {
    validate_config(config);

    std::vector<std::shared_ptr<const NoiseEngine>> sources;

    const bool has_runtime_loss =
        config.p_loss > 0.0 ||
        config.loss_runtime.per_gate > 0.0 ||
        config.loss_runtime.idle_rate > 0.0;
    if (has_runtime_loss) {
        sources.push_back(std::make_shared<LossTrackingSource>(
            config.p_loss,
            config.loss_runtime
        ));
    }

    const bool has_measurement =
        config.p_quantum_flip > 0.0 ||
        config.readout.p_flip0_to_1 > 0.0 || config.readout.p_flip1_to_0 > 0.0;
    if (has_measurement) {
        sources.push_back(std::make_shared<MeasurementNoiseSource>(
            config.p_quantum_flip,
            config.readout
        ));
    }

    const bool has_amplitude =
        config.amplitude_damping.per_gate > 0.0 ||
        config.amplitude_damping.idle_rate > 0.0;
    if (has_amplitude) {
        sources.push_back(std::make_shared<AmplitudeDampingSource>(
            config.amplitude_damping
        ));
    }

    const double single_sum = config.gate.single_qubit.px +
                              config.gate.single_qubit.py +
                              config.gate.single_qubit.pz;
    if (single_sum > 0.0) {
        sources.push_back(std::make_shared<SingleQubitPauliSource>(
            config.gate.single_qubit
        ));
    }

    const double ctrl_sum = config.gate.two_qubit_control.px +
                            config.gate.two_qubit_control.py +
                            config.gate.two_qubit_control.pz;
    const double tgt_sum = config.gate.two_qubit_target.px +
                           config.gate.two_qubit_target.py +
                           config.gate.two_qubit_target.pz;
    if (ctrl_sum > 0.0 || tgt_sum > 0.0) {
        sources.push_back(std::make_shared<TwoQubitPauliSource>(
            config.gate.two_qubit_control,
            config.gate.two_qubit_target
        ));
    }

    double correlated_sum = 0.0;
    for (double p : config.correlated_gate.matrix) {
        correlated_sum += p;
    }
    if (correlated_sum > 0.0) {
        sources.push_back(std::make_shared<CorrelatedPauliSource>(
            config.correlated_gate
        ));
    }

    const bool has_phase =
        config.phase.single_qubit > 0.0 ||
        config.phase.two_qubit_control > 0.0 ||
        config.phase.two_qubit_target > 0.0;
    if (has_phase) {
        sources.push_back(std::make_shared<PhaseKickNoiseSource>(config.phase));
    }

    if (config.idle_rate > 0.0) {
        sources.push_back(std::make_shared<IdleDephasingSource>(config.idle_rate));
    }

    if (config.phase.idle > 0.0) {
        sources.push_back(std::make_shared<IdlePhaseDriftSource>(config.phase.idle));
    }

    return sources;
}
