#include "service/job_service.hpp"

#include "progress_reporter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace service {

namespace {

std::size_t clamp_product(std::size_t a, std::size_t b) {
    if (a == 0 || b == 0) {
        return 0;
    }
    const std::size_t max_val = std::numeric_limits<std::size_t>::max();
    if (a > max_val / b) {
        return max_val;
    }
    return a * b;
}

std::size_t compute_total_steps(const JobRequest& job) {
    const std::size_t program_steps = job.program.size();
    const std::size_t shot_count = static_cast<std::size_t>(std::max(1, job.shots));
    return clamp_product(program_steps, shot_count);
}

}  // namespace

JobService::JobService()
    : id_counter_(0) {}

JobService::~JobService() = default;
std::string JobService::submit(JobRequest job, std::size_t max_threads) {
    const std::size_t seq = id_counter_.fetch_add(1, std::memory_order_relaxed);
    const std::string job_id = "job-" + std::to_string(seq);
    job.job_id = job_id;

    auto entry = std::make_shared<JobEntry>();
    entry->request = std::move(job);
    entry->reporter = std::make_shared<JobProgressReporter>();
    entry->result.job_id = job_id;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.emplace(job_id, entry);
    }

    std::thread worker([this, entry, max_threads]() {
        entry->status.store(JobStatus::Running, std::memory_order_relaxed);
        const std::size_t total_steps = compute_total_steps(entry->request);
        entry->reporter->set_total_steps(total_steps);
        const auto start = std::chrono::steady_clock::now();
        try {
            const std::size_t threads =
                max_threads > 0 ? max_threads : entry->request.max_threads;
            auto result = runner_.run(entry->request, threads, entry->reporter.get());
            auto end_result = std::chrono::steady_clock::now();
            result.elapsed_time =
                std::chrono::duration<double>(end_result - start).count();
            std::lock_guard<std::mutex> guard(entry->result_mutex);
            entry->result = std::move(result);
            entry->status.store(entry->result.status, std::memory_order_relaxed);
        } catch (const std::exception& ex) {
            const auto end = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> guard(entry->result_mutex);
            entry->result.status = JobStatus::Failed;
            entry->result.message = ex.what();
            entry->result.elapsed_time =
                std::chrono::duration<double>(end - start).count();
            entry->status.store(JobStatus::Failed, std::memory_order_relaxed);
        }
    });
    worker.detach();

    return job_id;
}

std::optional<JobResult> JobService::poll_result(const std::string& job_id) const {
    std::shared_ptr<JobEntry> entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = jobs_.find(job_id);
        if (it == jobs_.end()) {
            return std::nullopt;
        }
        entry = it->second;
    }
    const JobStatus status = entry->status.load(std::memory_order_relaxed);
    if (status != JobStatus::Completed && status != JobStatus::Failed) {
        return std::nullopt;
    }
    std::lock_guard<std::mutex> guard(entry->result_mutex);
    return entry->result;
}

JobStatusSnapshot JobService::status(const std::string& job_id) const {
    JobStatusSnapshot snapshot;
    std::shared_ptr<JobEntry> entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = jobs_.find(job_id);
        if (it == jobs_.end()) {
            snapshot.status = JobStatus::Failed;
            snapshot.message = std::string("job_id not found");
            return snapshot;
        }
        entry = it->second;
    }
    snapshot.status = entry->status.load(std::memory_order_relaxed);
    const std::size_t total = entry->reporter->total_steps();
    const std::size_t completed = entry->reporter->completed_steps();
    snapshot.percent_complete = total == 0 ? 0.0
        : std::min(1.0, static_cast<double>(completed) / static_cast<double>(total));
    snapshot.recent_logs = entry->reporter->recent_logs();
    {
        std::lock_guard<std::mutex> guard(entry->result_mutex);
        snapshot.message = entry->result.message;
    }
    return snapshot;
}

}  // namespace service
