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
#include "noise/device_noise.hpp"
namespace neutral_atom_vm {
class ProgressReporter;
}
namespace noise = neutral_atom_vm::noise;
#ifdef NA_VM_WITH_ONEAPI
#include "oneapi_state_backend.hpp"
#endif

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

    void set_progress_reporter(neutral_atom_vm::ProgressReporter* reporter);

    // Set the random seed used for stochastic processes such as
    // measurement sampling and noise application.
    void set_random_seed(std::uint64_t seed);

    void run(const std::vector<Instruction>& program);
    void set_shot_index(int shot);
    const std::vector<ExecutionLog>& logs() const { return state_.logs; }

    std::vector<std::complex<double>>& state_vector();
    const std::vector<std::complex<double>>& state_vector() const;

    const StatevectorState& state() const { return state_; }
    void set_device_noise_engine(std::shared_ptr<const noise::DeviceNoiseEngine> noise);
#ifdef NA_VM_WITH_ONEAPI
    void run_batched(
        const std::vector<Instruction>& program,
        int shots,
        const std::vector<std::uint64_t>& shot_seeds
    );
    const std::vector<std::vector<MeasurementRecord>>& batched_measurements() const;
#endif

  private:
    StatevectorState state_;

    std::shared_ptr<const NoiseEngine> noise_;
    std::mt19937_64 rng_{};
    std::unique_ptr<StateBackend> backend_;
    bool use_batched_shots_ = false;
#ifdef NA_VM_WITH_ONEAPI
    std::shared_ptr<const noise::DeviceNoiseEngine> device_noise_engine_;
    noise::DeviceNoiseContext device_noise_context_;
    int batched_shots_ = 1;
    std::size_t batched_total_shots_ = 0;
    std::size_t batched_shot_offset_ = 0;
    std::vector<std::uint64_t> batched_seeds_;
    std::vector<std::vector<MeasurementRecord>> batched_measurements_;
    std::vector<std::mt19937_64> batched_measurement_rngs_;
    std::vector<noise::DeviceNoiseContext> batched_device_noise_contexts_;
#endif
    neutral_atom_vm::ProgressReporter* progress_reporter_ = nullptr;


    void log_event(const std::string& category, const std::string& message);
    void execute_program(const std::vector<Instruction>& program);
    bool should_emit_logs() const;
    void alloc_array(int n);
    void apply_gate(const Gate& g);
    void measure(const std::vector<int>& targets);
    void move_atom(const MoveAtomInstruction& move);
    void wait_duration(const WaitInstruction& wait_instr);
    void apply_pulse(const PulseInstruction& pulse);
    void enforce_blockade(int q0, int q1) const;
    double distance_between_qubits(int q0, int q1) const;
#ifdef NA_VM_WITH_ONEAPI
    void measure_on_device(
        OneApiStateBackend& backend,
        const std::vector<int>& targets,
        MeasurementRecord& record
    );
    void measure_on_device_batched(
        OneApiStateBackend& backend,
        const std::vector<int>& targets
    );
    void reset_device_noise_contexts();
#endif
};
#ifdef NA_VM_WITH_ONEAPI
class OneApiStateBackend;
#endif
