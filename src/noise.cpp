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

class MeasurementNoiseSource : public NoiseEngine {
  public:
    MeasurementNoiseSource(
        double p_quantum_flip,
        double p_loss,
        MeasurementNoiseConfig readout
    )
        : p_quantum_flip_(p_quantum_flip)
        , p_loss_(p_loss)
        , readout_(readout) {}

    void apply_measurement_noise(
        MeasurementRecord& record,
        RandomStream& rng
    ) const override {
        const bool has_quantum = p_quantum_flip_ > 0.0;
        const bool has_loss = p_loss_ > 0.0;
        const bool has_readout =
            readout_.p_flip0_to_1 > 0.0 || readout_.p_flip1_to_0 > 0.0;

        if (!has_quantum && !has_loss && !has_readout) {
            return;
        }

        for (int& bit : record.bits) {
            double r = rng.uniform(0.0, 1.0);

            if (has_loss && r < p_loss_) {
                bit = -1;
                continue;
            }

            if (has_quantum) {
                r = rng.uniform(0.0, 1.0);
                if (r < p_quantum_flip_) {
                    if (bit == 0) {
                        bit = 1;
                    } else if (bit == 1) {
                        bit = 0;
                    }
                }
            }

            if (has_readout) {
                r = rng.uniform(0.0, 1.0);
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
    double p_loss_;
    MeasurementNoiseConfig readout_;
};

class SingleQubitPauliSource : public NoiseEngine {
  public:
    explicit SingleQubitPauliSource(SingleQubitPauliConfig cfg) : cfg_(cfg) {}

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

class TwoQubitPauliSource : public NoiseEngine {
  public:
    TwoQubitPauliSource(
        SingleQubitPauliConfig control,
        SingleQubitPauliConfig target
    )
        : control_(control)
        , target_(target) {}

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

}  // namespace

SimpleNoiseEngine::SimpleNoiseEngine(SimpleNoiseConfig config)
    : CompositeNoiseEngine(build_sources(config)) {}

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
}

std::vector<std::shared_ptr<const NoiseEngine>> SimpleNoiseEngine::build_sources(
    const SimpleNoiseConfig& config
) {
    validate_config(config);

    std::vector<std::shared_ptr<const NoiseEngine>> sources;

    const bool has_measurement =
        config.p_quantum_flip > 0.0 || config.p_loss > 0.0 ||
        config.readout.p_flip0_to_1 > 0.0 || config.readout.p_flip1_to_0 > 0.0;
    if (has_measurement) {
        sources.push_back(std::make_shared<MeasurementNoiseSource>(
            config.p_quantum_flip,
            config.p_loss,
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

    if (config.idle_rate > 0.0) {
        sources.push_back(std::make_shared<IdleDephasingSource>(config.idle_rate));
    }

    return sources;
}
