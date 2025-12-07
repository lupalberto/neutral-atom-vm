#include "hardware_vm.hpp"
#include "oneapi_state_backend.hpp"

#include <algorithm>
#include <cstdint>
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

std::vector<MeasurementRecord> HardwareVM::run(
    const std::vector<Instruction>& program,
    int shots,
    const std::vector<std::uint64_t>& shot_seeds
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

    const unsigned int concurrency_hint = std::thread::hardware_concurrency();
    const unsigned int worker_limit = concurrency_hint > 0 ? concurrency_hint : 1u;
    const std::size_t worker_count = std::min<std::size_t>(
        static_cast<std::size_t>(num_shots), static_cast<std::size_t>(worker_limit));

    std::vector<std::vector<MeasurementRecord>> per_shot_measurements(num_shots);
    std::vector<std::thread> workers;
    workers.reserve(worker_count);

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
            &seeds,
            start,
            end
        ]() {
            for (std::size_t shot = start; shot < end; ++shot) {
                HardwareConfig hw = profile_.hardware;
                StatevectorEngine engine(hw, make_state_backend(profile_.backend), seeds[shot]);
                if (profile_.noise_engine) {
                    engine.set_noise_model(profile_.noise_engine);
                }
                engine.run(program);
                per_shot_measurements[shot] = engine.state().measurements;
            }
        });
        shot_offset = end;
    }

    for (auto& worker : workers) {
        worker.join();
    }

    std::vector<MeasurementRecord> all_measurements;
    for (const auto& shot_records : per_shot_measurements) {
        all_measurements.insert(
            all_measurements.end(), shot_records.begin(), shot_records.end());
    }

    return all_measurements;
}
