#pragma once

#include "hardware_vm.hpp"
#include "noise.hpp"
#include "service/timeline.hpp"
#include "vm/isa.hpp"
#include "vm/measurement_record.types.hpp"
#include "progress_reporter.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>
#include <optional>

namespace service {

enum class JobStatus {
    Pending,
    Running,
    Completed,
    Failed,
};

struct JobRequest {
    std::string job_id;
    std::string device_id;
    std::string profile;
    HardwareConfig hardware;
    std::vector<Instruction> program;
    int shots = 1;
    std::size_t max_threads = 0;
    std::map<std::string, std::string> metadata;
    ISAVersion isa_version = kCurrentISAVersion;
    std::optional<SimpleNoiseConfig> noise_config;
    std::optional<std::string> stim_circuit;
};

struct JobResult {
    std::string job_id;
    JobStatus status = JobStatus::Pending;
    std::vector<MeasurementRecord> measurements;
    std::vector<ExecutionLog> logs;
    std::vector<TimelineEntry> timeline;
    std::vector<TimelineEntry> scheduler_timeline;
    std::string log_time_units = "ns";
    std::string timeline_units = "ns";
    std::string scheduler_timeline_units = "ns";
    double elapsed_time = 0.0;
    std::string message;
};

BackendKind backend_for_device(const std::string& device_id);

std::string to_json(const JobRequest& job);
std::string status_to_string(JobStatus status);

class JobRunner {
  public:
    JobResult run(
        const JobRequest& job,
        std::size_t max_threads = 0,
        neutral_atom_vm::ProgressReporter* reporter = nullptr
    );
};

}  // namespace service
