#pragma once

#include "service/job.hpp"

#include "progress_reporter.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class JobProgressReporter final : public neutral_atom_vm::ProgressReporter {
  public:
    JobProgressReporter() = default;

    void set_total_steps(std::size_t total_steps) override {
        std::lock_guard<std::mutex> lock(mutex_);
        total_steps_ = total_steps;
    }

    void increment_completed_steps(std::size_t delta = 1) override {
        completed_steps_.fetch_add(delta, std::memory_order_relaxed);
    }

    void record_log(const ExecutionLog& log) override {
        std::lock_guard<std::mutex> lock(mutex_);
        logs_.push_back(log);
        if (logs_.size() > kMaxLogs) {
            logs_.erase(logs_.begin());
        }
    }

    std::size_t total_steps() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return total_steps_;
    }

    std::size_t completed_steps() const {
        return completed_steps_.load(std::memory_order_relaxed);
    }

    std::vector<ExecutionLog> recent_logs() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return logs_;
    }

  private:
    static constexpr std::size_t kMaxLogs = 8;

    mutable std::mutex mutex_;
    std::vector<ExecutionLog> logs_;
    std::size_t total_steps_ = 0;
    std::atomic<std::size_t> completed_steps_{0};
};

namespace service {

struct JobStatusSnapshot {
    JobStatus status = JobStatus::Pending;
    double percent_complete = 0.0;
    std::string message;
    std::vector<ExecutionLog> recent_logs;
};

class JobService {
  public:
    JobService();
    ~JobService();

    // Submit a job for asynchronous execution. Returns the generated job ID.
    std::string submit(JobRequest job, std::size_t max_threads = 0);

    // Poll for the final result if the job is complete.
    std::optional<JobResult> poll_result(const std::string& job_id) const;

    // Query the current status snapshot for the given job.
    JobStatusSnapshot status(const std::string& job_id) const;

  private:
    struct JobEntry {
        JobRequest request;
        JobResult result;
        std::shared_ptr<JobProgressReporter> reporter;
        std::atomic<JobStatus> status{JobStatus::Pending};
        mutable std::mutex result_mutex;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<JobEntry>> jobs_;
    std::atomic<std::uint64_t> id_counter_{0};
    JobRunner runner_;
};

}  // namespace service
