#pragma once

#include "noise.hpp"

#include <memory>
#include <vector>

class AmplitudeDampingSource : public NoiseEngine {
  public:
    explicit AmplitudeDampingSource(AmplitudeDampingConfig config);

    std::shared_ptr<const NoiseEngine> clone() const override;

    void apply_single_qubit_gate_noise(
        int target,
        int /*n_qubits*/,
        std::vector<std::complex<double>>& amplitudes,
        RandomStream& /*rng*/
    ) const override;

    void apply_idle_noise(
        int n_qubits,
        std::vector<std::complex<double>>& amplitudes,
        double duration,
        RandomStream& /*rng*/
    ) const override;

  private:
    static void apply_to_qubit(
        int target,
        std::vector<std::complex<double>>& amplitudes,
        double gamma
    );

    AmplitudeDampingConfig config_;
};
