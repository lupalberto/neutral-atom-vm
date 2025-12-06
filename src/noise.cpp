#include "noise.hpp"

#include <array>
#include <cmath>
#include <random>
#include <stdexcept>
#include <utility>

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

namespace {

void apply_pauli_x(
    std::vector<std::complex<double>>& state,
    int n_qubits,
    int target
) {
    const std::size_t dim = state.size();
    const std::size_t bit = static_cast<std::size_t>(1) << target;
    for (std::size_t i = 0; i < dim; ++i) {
        if ((i & bit) == 0) {
            const std::size_t j = i | bit;
            std::swap(state[i], state[j]);
        }
    }
}

void apply_pauli_z(
    std::vector<std::complex<double>>& state,
    int /*n_qubits*/,
    int target
) {
    const std::size_t dim = state.size();
    const std::size_t bit = static_cast<std::size_t>(1) << target;
    for (std::size_t i = 0; i < dim; ++i) {
        if ((i & bit) != 0) {
            state[i] = -state[i];
        }
    }
}

void apply_pauli_y(
    std::vector<std::complex<double>>& state,
    int /*n_qubits*/,
    int target
) {
    const std::size_t dim = state.size();
    const std::size_t bit = static_cast<std::size_t>(1) << target;

    const std::complex<double> imag(0.0, 1.0);
    const std::complex<double> minus_imag(0.0, -1.0);

    for (std::size_t i = 0; i < dim; ++i) {
        if ((i & bit) == 0) {
            const std::size_t j = i | bit;
            const auto a0 = state[i];
            const auto a1 = state[j];
            // Y = [[0, -i], [i, 0]]
            state[i] = minus_imag * a1;
            state[j] = imag * a0;
        }
    }
}

char sample_pauli(
    const SingleQubitPauliConfig& cfg,
    RandomStream& rng
) {
    const double px = cfg.px;
    const double py = cfg.py;
    const double pz = cfg.pz;
    const double sum = px + py + pz;
    if (sum <= 0.0) {
        return 'I';
    }
    const double r = rng.uniform(0.0, 1.0);

    if (r < px) {
        return 'X';
    }
    if (r < px + py) {
        return 'Y';
    }
    if (r < px + py + pz) {
        return 'Z';
    }
    return 'I';
}

void apply_single_qubit_pauli(
    char pauli,
    std::vector<std::complex<double>>& state,
    int n_qubits,
    int target
) {
    switch (pauli) {
        case 'X':
            apply_pauli_x(state, n_qubits, target);
            break;
        case 'Y':
            apply_pauli_y(state, n_qubits, target);
            break;
        case 'Z':
            apply_pauli_z(state, n_qubits, target);
            break;
        default:
            break;
    }
}

double sample_phase_angle(double magnitude, RandomStream& rng) {
    if (magnitude <= 0.0) {
        return 0.0;
    }
    const double r = rng.uniform(0.0, 1.0);
    return (2.0 * r - 1.0) * magnitude;
}

void apply_phase_rotation(
    std::vector<std::complex<double>>& state,
    int /*n_qubits*/,
    int target,
    double theta
) {
    if (theta == 0.0) {
        return;
    }
    const std::size_t dim = state.size();
    const std::size_t bit = static_cast<std::size_t>(1) << target;
    const double half = 0.5 * theta;
    const std::complex<double> phase0(std::cos(-half), std::sin(-half));
    const std::complex<double> phase1(std::cos(half), std::sin(half));
    for (std::size_t i = 0; i < dim; ++i) {
        if ((i & bit) == 0) {
            state[i] *= phase0;
        } else {
            state[i] *= phase1;
        }
    }
}

class MeasurementNoiseSource : public NoiseEngine {
  public:
    MeasurementNoiseSource(
        double p_quantum_flip,
        MeasurementNoiseConfig readout
    )
        : p_quantum_flip_(p_quantum_flip)
        , readout_(readout) {}

    std::shared_ptr<const NoiseEngine> clone() const override {
        return std::make_shared<MeasurementNoiseSource>(*this);
    }

    void apply_measurement_noise(
        MeasurementRecord& record,
        RandomStream& rng
    ) const override {
        const bool has_quantum = p_quantum_flip_ > 0.0;
        const bool has_readout =
            readout_.p_flip0_to_1 > 0.0 || readout_.p_flip1_to_0 > 0.0;

        if (!has_quantum && !has_readout) {
            return;
        }

        for (int& bit : record.bits) {
            if (bit == -1) {
                continue;
            }

            if (has_quantum) {
                const double r = rng.uniform(0.0, 1.0);
                if (r < p_quantum_flip_) {
                    bit = (bit == 0) ? 1 : 0;
                }
            }

            if (has_readout) {
                const double r = rng.uniform(0.0, 1.0);
                if (bit == 0) {
                    if (r < readout_.p_flip0_to_1) {
                        bit = 1;
                    }
                } else if (bit == 1) {
                    if (r < readout_.p_flip1_to_0) {
                        bit = 0;
                    }
                }
            }
        }
    }

  private:
    double p_quantum_flip_;
    MeasurementNoiseConfig readout_;
};

class SingleQubitPauliSource : public NoiseEngine {
  public:
    explicit SingleQubitPauliSource(SingleQubitPauliConfig cfg) : cfg_(cfg) {}

    std::shared_ptr<const NoiseEngine> clone() const override {
        return std::make_shared<SingleQubitPauliSource>(*this);
    }

    void apply_single_qubit_gate_noise(
        int target,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        RandomStream& rng
    ) const override {
        const double sum = cfg_.px + cfg_.py + cfg_.pz;
        if (sum <= 0.0) {
            return;
        }
        const char pauli = sample_pauli(cfg_, rng);
        apply_single_qubit_pauli(pauli, amplitudes, n_qubits, target);
    }

  private:
    SingleQubitPauliConfig cfg_;
};

class CorrelatedPauliSource : public NoiseEngine {
  public:
    explicit CorrelatedPauliSource(TwoQubitCorrelatedPauliConfig cfg)
        : cfg_(cfg) {}

    std::shared_ptr<const NoiseEngine> clone() const override {
        return std::make_shared<CorrelatedPauliSource>(*this);
    }

    void apply_two_qubit_gate_noise(
        int q0,
        int q1,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        RandomStream& rng
    ) const override {
        const double total = total_probability();
        if (total <= 0.0) {
            return;
        }
        const double r = rng.uniform(0.0, 1.0);
        double cumulative = 0.0;
        static constexpr char paulis[4] = {'I', 'X', 'Y', 'Z'};
        for (int ctrl = 0; ctrl < 4; ++ctrl) {
            for (int tgt = 0; tgt < 4; ++tgt) {
                const double p = cfg_.matrix[4 * ctrl + tgt];
                if (p <= 0.0) {
                    continue;
                }
                cumulative += p;
                if (r < cumulative) {
                    apply_single_qubit_pauli(paulis[ctrl], amplitudes, n_qubits, q0);
                    apply_single_qubit_pauli(paulis[tgt], amplitudes, n_qubits, q1);
                    return;
                }
            }
        }
    }

  private:
    TwoQubitCorrelatedPauliConfig cfg_;

    double total_probability() const {
        double sum = 0.0;
        for (double p : cfg_.matrix) {
            sum += p;
        }
        return sum;
    }
};

class TwoQubitPauliSource : public NoiseEngine {
  public:
    TwoQubitPauliSource(
        SingleQubitPauliConfig control,
        SingleQubitPauliConfig target
    )
        : control_(control)
        , target_(target) {}

    std::shared_ptr<const NoiseEngine> clone() const override {
        return std::make_shared<TwoQubitPauliSource>(*this);
    }

    void apply_two_qubit_gate_noise(
        int q0,
        int q1,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        RandomStream& rng
    ) const override {
        if (control_.px + control_.py + control_.pz > 0.0) {
            const char p0 = sample_pauli(control_, rng);
            apply_single_qubit_pauli(p0, amplitudes, n_qubits, q0);
        }
        if (target_.px + target_.py + target_.pz > 0.0) {
            const char p1 = sample_pauli(target_, rng);
            apply_single_qubit_pauli(p1, amplitudes, n_qubits, q1);
        }
    }

  private:
    SingleQubitPauliConfig control_;
    SingleQubitPauliConfig target_;
};

class IdleDephasingSource : public NoiseEngine {
  public:
    explicit IdleDephasingSource(double idle_rate) : idle_rate_(idle_rate) {}

    std::shared_ptr<const NoiseEngine> clone() const override {
        return std::make_shared<IdleDephasingSource>(*this);
    }

    void apply_idle_noise(
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        double duration,
        RandomStream& rng
    ) const override {
        if (idle_rate_ <= 0.0 || duration <= 0.0) {
            return;
        }
        const double probability = 1.0 - std::exp(-idle_rate_ * duration);
        if (probability <= 0.0) {
            return;
        }
        for (int q = 0; q < n_qubits; ++q) {
            if (rng.uniform(0.0, 1.0) < probability) {
                apply_pauli_z(amplitudes, n_qubits, q);
            }
        }
    }

  private:
    double idle_rate_;
};

class PhaseKickNoiseSource : public NoiseEngine {
  public:
    explicit PhaseKickNoiseSource(PhaseNoiseConfig cfg) : cfg_(cfg) {}

    std::shared_ptr<const NoiseEngine> clone() const override {
        return std::make_shared<PhaseKickNoiseSource>(*this);
    }

    void apply_single_qubit_gate_noise(
        int target,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        RandomStream& rng
    ) const override {
        apply_phase_if_needed(cfg_.single_qubit, target, n_qubits, amplitudes, rng);
    }

    void apply_two_qubit_gate_noise(
        int q0,
        int q1,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        RandomStream& rng
    ) const override {
        apply_phase_if_needed(cfg_.two_qubit_control, q0, n_qubits, amplitudes, rng);
        apply_phase_if_needed(cfg_.two_qubit_target, q1, n_qubits, amplitudes, rng);
    }

  private:
    PhaseNoiseConfig cfg_;

    static void apply_phase_if_needed(
        double magnitude,
        int target,
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        RandomStream& rng
    ) {
        if (magnitude <= 0.0) {
            return;
        }
        const double theta = sample_phase_angle(magnitude, rng);
        apply_phase_rotation(amplitudes, n_qubits, target, theta);
    }
};

class IdlePhaseDriftSource : public NoiseEngine {
  public:
    explicit IdlePhaseDriftSource(double rate) : rate_(rate) {}

    std::shared_ptr<const NoiseEngine> clone() const override {
        return std::make_shared<IdlePhaseDriftSource>(*this);
    }

    void apply_idle_noise(
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        double duration,
        RandomStream& rng
    ) const override {
        if (rate_ <= 0.0 || duration <= 0.0) {
            return;
        }
        const double magnitude = rate_ * duration;
        for (int q = 0; q < n_qubits; ++q) {
            const double theta = sample_phase_angle(magnitude, rng);
            apply_phase_rotation(amplitudes, n_qubits, q, theta);
        }
    }

  private:
    double rate_;
};

class LossTrackingSource : public NoiseEngine {
  public:
    LossTrackingSource(double measurement_loss, LossRuntimeConfig cfg)
        : measurement_loss_(measurement_loss)
        , cfg_(cfg) {}

    std::shared_ptr<const NoiseEngine> clone() const override {
        return std::make_shared<LossTrackingSource>(*this);
    }

    void apply_single_qubit_gate_noise(
        int target,
        int n_qubits,
        std::vector<std::complex<double>>& /*amplitudes*/,
        RandomStream& rng
    ) const override {
        ensure_size(n_qubits);
        maybe_mark_loss(target, cfg_.per_gate, rng);
    }

    void apply_two_qubit_gate_noise(
        int q0,
        int q1,
        int n_qubits,
        std::vector<std::complex<double>>& /*amplitudes*/,
        RandomStream& rng
    ) const override {
        ensure_size(n_qubits);
        maybe_mark_loss(q0, cfg_.per_gate, rng);
        maybe_mark_loss(q1, cfg_.per_gate, rng);
    }

    void apply_idle_noise(
        int n_qubits,
        std::vector<std::complex<double>>& /*amplitudes*/,
        double duration,
        RandomStream& rng
    ) const override {
        ensure_size(n_qubits);
        if (cfg_.idle_rate <= 0.0 || duration <= 0.0) {
            return;
        }
        const double probability = 1.0 - std::exp(-cfg_.idle_rate * duration);
        for (int q = 0; q < n_qubits; ++q) {
            maybe_mark_loss(q, probability, rng);
        }
    }

    void apply_measurement_noise(
        MeasurementRecord& record,
        RandomStream& rng
    ) const override {
        for (std::size_t idx = 0; idx < record.targets.size(); ++idx) {
            const int q = record.targets[idx];
            ensure_target(q);
            if (lost_[q]) {
                record.bits[idx] = -1;
                continue;
            }
            if (measurement_loss_ > 0.0) {
                const double r = rng.uniform(0.0, 1.0);
                if (r < measurement_loss_) {
                    lost_[q] = true;
                    record.bits[idx] = -1;
                }
            }
        }
    }

  private:
    double measurement_loss_;
    LossRuntimeConfig cfg_;
    mutable std::vector<bool> lost_;

    void ensure_size(int n_qubits) const {
        if (n_qubits <= 0) {
            return;
        }
        if (static_cast<int>(lost_.size()) < n_qubits) {
            lost_.resize(static_cast<std::size_t>(n_qubits), false);
        }
    }

    void ensure_target(int q) const {
        if (q < 0) {
            return;
        }
        if (q >= static_cast<int>(lost_.size())) {
            lost_.resize(static_cast<std::size_t>(q + 1), false);
        }
    }

    void maybe_mark_loss(int q, double probability, RandomStream& rng) const {
        if (q < 0) {
            return;
        }
        if (q >= static_cast<int>(lost_.size())) {
            lost_.resize(static_cast<std::size_t>(q + 1), false);
        }
        if (lost_[q] || probability <= 0.0) {
            return;
        }
        const double r = rng.uniform(0.0, 1.0);
        if (r < probability) {
            lost_[q] = true;
        }
    }
};

}  // namespace

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
            throw std::invalid_argument("Correlated Pauli probabilities must be in [0, 1]");
        }
        correlated_sum += p;
    }
    if (correlated_sum > 1.0 + 1e-12) {
        throw std::invalid_argument("Sum of correlated Pauli probabilities must be <= 1");
    }

    if (config.loss_runtime.per_gate < 0.0 ||
        config.loss_runtime.per_gate > 1.0 + 1e-12 ||
        config.loss_runtime.idle_rate < 0.0) {
        throw std::invalid_argument("Loss runtime probabilities must be non-negative and <= 1 per gate");
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
