#pragma once

#include <array>
#include <complex>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "cpu_state_backend.hpp"
#include "noise.hpp"
#include "vm/isa.hpp"
#include "vm/measurement_record.types.hpp"

// Statevector-based execution engine for the Neutral Atom ISA.
// This is a concrete runtime backend, not the hardware VM itself.

struct StatevectorState {
    int n_qubits = 0;
    HardwareConfig hw;
    double logical_time = 0.0;
    std::vector<PulseInstruction> pulse_log;
    std::vector<MeasurementRecord> measurements;
    std::vector<ExecutionLog> logs;
    int shot_index = 0;
    std::vector<double> last_measurement_time;
};

class StatevectorEngine {
  public:
    explicit StatevectorEngine(
        HardwareConfig cfg,
        std::unique_ptr<StateBackend> backend = nullptr,
        std::uint64_t seed = std::numeric_limits<std::uint64_t>::max()
    );

    // Attach a shared noise model instance. If nullptr, the engine
    // evolves without adding additional noise beyond ideal gates.
    void set_noise_model(std::shared_ptr<const NoiseEngine> noise);

    // Set the random seed used for stochastic processes such as
    // measurement sampling and noise application.
    void set_random_seed(std::uint64_t seed);

    void run(const std::vector<Instruction>& program);
    void set_shot_index(int shot);
    const std::vector<ExecutionLog>& logs() const { return state_.logs; }

    std::vector<std::complex<double>>& state_vector();
    const std::vector<std::complex<double>>& state_vector() const;

    const StatevectorState& state() const { return state_; }

  private:
    StatevectorState state_;

    std::shared_ptr<const NoiseEngine> noise_;
    std::mt19937_64 rng_{};
    std::unique_ptr<StateBackend> backend_;

    void log_event(const std::string& category, const std::string& message);
    void alloc_array(int n);
    void apply_gate(const Gate& g);
    void measure(const std::vector<int>& targets);
    void move_atom(const MoveAtomInstruction& move);
    void wait_duration(const WaitInstruction& wait_instr);
    void apply_pulse(const PulseInstruction& pulse);
    void enforce_blockade(int q0, int q1) const;
};
