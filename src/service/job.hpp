#pragma once

#include "vm.hpp"

#include <map>
#include <string>
#include <vector>

namespace service {

enum class JobStatus {
    Pending,
    Running,
    Completed,
    Failed,
};

struct JobRequest {
    std::string job_id;
    HardwareConfig hardware;
    std::vector<Instruction> program;
    int shots = 1;
    std::map<std::string, std::string> metadata;
};

struct JobResult {
    std::string job_id;
    JobStatus status = JobStatus::Pending;
    std::vector<MeasurementRecord> measurements;
    double elapsed_time = 0.0;
    std::string message;
};

std::string to_json(const JobRequest& job);
std::string status_to_string(JobStatus status);

class JobRunner {
  public:
    JobResult run(const JobRequest& job);
};

}  // namespace service
