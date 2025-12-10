#include "hardware_vm.hpp"
#include "noise/device_noise_builder.hpp"
#include "oneapi_state_backend.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>

namespace {

std::unique_ptr<StateBackend> make_state_backend(BackendKind backend) {
    switch (backend) {
        case BackendKind::kOneApi:
#ifdef NA_VM_WITH_ONEAPI
            return std::make_unique<OneApiStateBackend>();
#else
            throw std::runtime_error(
                "oneAPI backend unavailable; rebuild with NA_VM_WITH_ONEAPI=ON"
            );
#endif
        case BackendKind::kCpu:
        default:
            return std::make_unique<CpuStateBackend>();
    }
}

}  // namespace

HardwareVM::HardwareVM(DeviceProfile profile)
    : profile_(std::move(profile)) {
    if (!is_supported_isa_version(profile_.isa_version)) {
        throw std::runtime_error(
            "Unsupported ISA version " + to_string(profile_.isa_version) +
            " (supported: " + supported_versions_to_string() + ")"
        );
    }
}

void HardwareVM::set_progress_reporter(neutral_atom_vm::ProgressReporter* reporter) {
    progress_reporter_ = reporter;
}

HardwareVM::RunResult HardwareVM::run(
    const std::vector<Instruction>& program,
    int shots,
    const std::vector<std::uint64_t>& shot_seeds,
    const std::vector<neutral_atom_vm::InstructionTiming>* instruction_timings,
    std::size_t max_threads
) {
    if (!is_supported_isa_version(profile_.isa_version)) {
        throw std::runtime_error(
            "Unsupported ISA version " + to_string(profile_.isa_version) +
            " (supported: " + supported_versions_to_string() + ")"
        );
    }

    const int num_shots = std::max(1, shots);
    if (!shot_seeds.empty() && static_cast<int>(shot_seeds.size()) != num_shots) {
        throw std::invalid_argument("shot seeds must match the requested shots");
    }

    std::vector<std::uint64_t> seeds;
    seeds.reserve(num_shots);
    if (!shot_seeds.empty()) {
        seeds = shot_seeds;
    } else {
        std::mt19937_64 seed_rng(std::random_device{}());
        for (int i = 0; i < num_shots; ++i) {
            seeds.push_back(seed_rng());
        }
    }

    if (profile_.backend == BackendKind::kStabilizer) {
#ifdef NA_VM_WITH_STIM
        return run_stabilizer(program, num_shots, seeds);
#else
        throw std::runtime_error(
            "stabilizer backend unavailable; rebuild with NA_VM_WITH_STIM=ON"
        );
#endif
    }

    (void)instruction_timings;
    if (profile_.backend == BackendKind::kOneApi) {
#ifdef NA_VM_WITH_ONEAPI
        return run_oneapi_batched(program, num_shots, seeds);
#else
        throw std::runtime_error(
            "oneAPI backend unavailable; rebuild with NA_VM_WITH_ONEAPI=ON");
#endif
    }

    const std::size_t hardware_threads = std::thread::hardware_concurrency();
    const std::size_t default_threads = hardware_threads > 0 ? hardware_threads : 1;
    const std::size_t worker_limit = max_threads > 0 ? max_threads : default_threads;
    const std::size_t worker_count = std::min<std::size_t>(
        static_cast<std::size_t>(num_shots), worker_limit);

    std::vector<std::vector<MeasurementRecord>> per_shot_measurements(num_shots);
    std::vector<std::vector<ExecutionLog>> per_shot_logs(num_shots);
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    std::mutex failure_mutex;
    std::exception_ptr failure;

    const std::size_t base_shots = static_cast<std::size_t>(num_shots) / worker_count;
    const std::size_t remainder = static_cast<std::size_t>(num_shots) % worker_count;
    std::size_t shot_offset = 0;

    for (std::size_t worker_idx = 0; worker_idx < worker_count; ++worker_idx) {
        std::size_t shots_for_worker = base_shots + (worker_idx < remainder ? 1 : 0);
        if (shots_for_worker == 0) {
            continue;
        }
        const std::size_t start = shot_offset;
        const std::size_t end = start + shots_for_worker;
        workers.emplace_back([
            this,
            &program,
            &per_shot_measurements,
            &per_shot_logs,
            &seeds,
            start,
            end,
            &failure_mutex,
            &failure
        ]() {
            for (std::size_t shot = start; shot < end; ++shot) {
                try {
                    HardwareConfig hw = profile_.hardware;
                    StatevectorEngine engine(hw, make_state_backend(profile_.backend), seeds[shot]);
                    if (progress_reporter_) {
                        engine.set_progress_reporter(progress_reporter_);
                    }
                    engine.set_shot_index(static_cast<int>(shot));
                    if (profile_.noise_engine) {
                        engine.set_noise_model(profile_.noise_engine);
                    }
                    if (profile_.device_noise_engine) {
                        engine.set_device_noise_engine(profile_.device_noise_engine);
                    }
                    engine.run(program);
                    per_shot_measurements[shot] = engine.state().measurements;
                    per_shot_logs[shot] = engine.state().logs;
                } catch (...) {
                    std::lock_guard<std::mutex> lock(failure_mutex);
                    if (!failure) {
                        failure = std::current_exception();
                    }
                    return;
                }
            }
        });
        shot_offset = end;
    }

    for (auto& worker : workers) {
        worker.join();
    }
    if (failure) {
        std::rethrow_exception(failure);
    }

    RunResult result;
    std::vector<ExecutionLog>& all_logs = result.logs;
    for (const auto& shot_records : per_shot_measurements) {
        result.measurements.insert(
            result.measurements.end(), shot_records.begin(), shot_records.end());
    }
    for (const auto& shot_logs : per_shot_logs) {
        all_logs.insert(all_logs.end(), shot_logs.begin(), shot_logs.end());
    }

    return result;
}

#ifdef NA_VM_WITH_ONEAPI
HardwareVM::RunResult HardwareVM::run_oneapi_batched(
    const std::vector<Instruction>& program,
    int num_shots,
    const std::vector<std::uint64_t>& shot_seeds
) {
    HardwareConfig hw = profile_.hardware;
    StatevectorEngine engine(hw, make_state_backend(BackendKind::kOneApi));
    if (progress_reporter_) {
        engine.set_progress_reporter(progress_reporter_);
    }
    if (profile_.noise_engine) {
        engine.set_noise_model(profile_.noise_engine);
    }
    if (profile_.device_noise_engine) {
        engine.set_device_noise_engine(profile_.device_noise_engine);
    }
    engine.run_batched(program, num_shots, shot_seeds);

    RunResult result;
    const auto& batched_measurements = engine.batched_measurements();
    for (int shot = 0; shot < num_shots; ++shot) {
        if (static_cast<std::size_t>(shot) < batched_measurements.size()) {
            for (const auto& record : batched_measurements[static_cast<std::size_t>(shot)]) {
                result.measurements.push_back(record);
            }
        }
    }
    result.logs = engine.logs();
    return result;
}
#endif
