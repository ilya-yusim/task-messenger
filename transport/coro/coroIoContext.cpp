/**
 * \file coroIoContext.cpp
 * \brief Operational implementation for `transport::CoroIoContext`.
 * \details Implements the worker thread loop, pending operation processing, histogram
 * updates, and RAII work guard mechanics. Public API semantics reside in the header;
 * this file documents runtime behavior and design choices:
 * - Pending operations are stolen in batches (swap with local vector) to minimize lock contention.
 * - Per-thread counters and attempt histograms updated only after successful completion.
 * - A short timed wait (`poll_interval_`) backs the notification path to avoid lost wakeups and ensure shutdown responsiveness.
 */
#include "coroIoContext.hpp"
#include <algorithm>
#include <condition_variable>
#include <coroutine>
#include <string>
#include "processUtils.hpp"

namespace transport {

namespace {
} // namespace

CoroIoContext::CoroIoContext() {
    // Pre-size histograms (logger may be set later)
    std::lock_guard<std::mutex> lock(stats_mutex_);
    for (auto &hist : completion_attempt_histograms_) {
        hist.assign(max_tracked_attempts_, 0);
    }
}

CoroIoContext::~CoroIoContext() { stop(); }

void CoroIoContext::start() { start(1); }

void CoroIoContext::start(size_t threads) {
    if (threads == 0) threads = 1;
    bool expected = false;
    if (running_.compare_exchange_strong(expected, true)) {
        // (Re)initialize per-thread statistics
        thread_count_.store(threads, std::memory_order_release);
        per_thread_operations_processed_ = std::make_unique<std::atomic<size_t>[]>(threads);
        for (size_t i = 0; i < threads; ++i) {
            per_thread_operations_processed_[i].store(0, std::memory_order_relaxed);
        }
        event_threads_.reserve(threads);
        for (size_t i = 0; i < threads; ++i) {
            // name each thread so it's easier to identify in debuggers / profilers
            event_threads_.emplace_back([this, i] {
                try {
                    ProcessUtils::set_current_thread_name(std::string("CoroIoContext-") + std::to_string(i));
                } catch (...) {
                    // best-effort; do not propagate
                }
                run(i);
            });
        }
        if (logger_) logger_->info("CoroIoContext started with " + std::to_string(threads) + " thread(s)");
    }
}

void CoroIoContext::stop() {
    if (running_.exchange(false)) {
        wake_();
        for (auto& t : event_threads_) {
            if (t.joinable()) t.join();
        }
        event_threads_.clear();
        if (logger_) logger_->info("CoroIoContext stopped");
    }
}

bool CoroIoContext::is_running() const { return running_; }

void CoroIoContext::set_logger(std::shared_ptr<Logger> logger) { logger_ = std::move(logger); }
std::shared_ptr<Logger> CoroIoContext::get_logger() const { return logger_; }

void CoroIoContext::run() {
    // Backward-compatible single-thread run uses index 0
    run(0);
}

void CoroIoContext::run(size_t thread_index) {
    if (logger_) logger_->info("CoroIoContext main loop started");

    while (running_ || outstanding_work_.load(std::memory_order_acquire) > 0) {
        try {
            process_pending_ops(thread_index);
        } catch (const std::exception& e) {
            if (logger_) logger_->error("Exception in event loop: " + std::string(e.what()));
        }
        // Timed wait: predicate prevents lost wakeups (work queued prior to sleep)
        // and ensures prompt shutdown via !running_.
        std::unique_lock<std::mutex> lk(pending_mutex_);
        pending_cv_.wait_for(lk, poll_interval_, [this]() {
            return !running_ || !pending_ops_.empty();
        });
    }
    if (logger_) logger_->info("CoroIoContext main loop finished");
}

void CoroIoContext::process_pending_ops(size_t thread_index) {
    // Thread-local scratch buffers to minimize allocations & allocator churn.
    thread_local std::vector<PendingOp> fetched;
    thread_local std::vector<PendingOp> requeue; // unfinished

    // Steal all pending ops with one lock; minimizes contention and allows
    // processing outside the critical section.
    {
        std::lock_guard<std::mutex> lk(pending_mutex_);
        if (pending_ops_.empty()) {
            return; // nothing to do
        }
        fetched.swap(pending_ops_); // pending_ops_ now empty
    }

    for (auto &op : fetched) {
        bool completed = false;
        try {
            if (op.try_complete) completed = op.try_complete();
        } catch (const std::exception &e) {
            if (logger_) logger_->error(std::string("Error in try_complete: ") + e.what());
            completed = true; // drop on exception
        }
        if (completed) {
            auto h = op.handle;
            if (h && !h.done()) {
                try {
                    h.resume();
                    // if(logger_) logger_->trace("TID", "Resumed coroutine after operation completion");
                    {
                        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                        total_operations_processed_++;
                        // attempts = number of prior failures
                        size_t failures = op.attempts;
                        size_t idx = std::min<size_t>(failures, max_tracked_attempts_ - 1);
                        size_t cat_idx = static_cast<size_t>(op.category);
                        if (cat_idx >= category_count_) cat_idx = 0;
                        if (completion_attempt_histograms_[cat_idx].size() < max_tracked_attempts_) {
                            completion_attempt_histograms_[cat_idx].assign(max_tracked_attempts_, 0); // safety re-init if not sized
                        }
                        completion_attempt_histograms_[cat_idx][idx]++;
                        // Aggregate stats
                        if (failures < min_failures_before_success_) min_failures_before_success_ = failures;
                        if (failures > max_failures_before_success_) max_failures_before_success_ = failures;
                        sum_failures_before_success_ += failures;
                        completed_ops_for_avg_++;
                    }
                    size_t n = thread_count_.load(std::memory_order_acquire);
                    if (thread_index < n && per_thread_operations_processed_) {
                        per_thread_operations_processed_[thread_index].fetch_add(1, std::memory_order_relaxed);
                    }
                } catch (const std::exception &e) {
                    if (logger_) logger_->error(std::string("Error resuming pending op: ") + e.what());
                }
            }
        } else {
            // Increment attempts before requeueing
            #undef max
            if (op.attempts < std::numeric_limits<uint16_t>::max()) ++op.attempts;
            requeue.push_back(std::move(op));
        }
    }

    fetched.clear(); // keep capacity

    if (!requeue.empty()) {
        std::lock_guard<std::mutex> lk(pending_mutex_);
        // Reserve combined capacity to avoid incremental growth inside loop.
        if (pending_ops_.capacity() < pending_ops_.size() + requeue.size()) {
            pending_ops_.reserve(pending_ops_.size() + requeue.size());
        }
        for (auto &op : requeue) {
            pending_ops_.push_back(std::move(op));
        }
        requeue.clear();
    }
}

void CoroIoContext::register_pending(std::function<bool()> try_complete, std::coroutine_handle<> handle) {
    register_pending(PendingOpCategory::Generic, std::move(try_complete), handle);
}

void CoroIoContext::register_pending(PendingOpCategory category, std::function<bool()> try_complete, std::coroutine_handle<> handle) {
    {
        std::lock_guard<std::mutex> lk(pending_mutex_);
        PendingOp op{};
        op.try_complete = std::move(try_complete);
        op.handle = handle;
        op.category = category;
        pending_ops_.push_back(std::move(op));
    }
    wake_();
}

std::vector<size_t> CoroIoContext::get_completion_attempt_histogram() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    std::vector<size_t> agg(max_tracked_attempts_, 0);
    for (const auto &hist : completion_attempt_histograms_) {
        if (hist.size() < max_tracked_attempts_) continue;
        for (size_t i = 0; i < max_tracked_attempts_; ++i) {
            agg[i] += hist[i];
        }
    }
    return agg;
}

std::array<std::vector<size_t>, CoroIoContext::category_count_> CoroIoContext::get_completion_attempt_histograms_by_category() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return completion_attempt_histograms_;
}

std::string CoroIoContext::format_detailed_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    std::string out;
    out += "CoroIoContext Detailed Statistics\n";
    out += "Total operations processed: " + std::to_string(total_operations_processed_) + "\n";
    if (completed_ops_for_avg_ > 0) {
        double avg = static_cast<double>(sum_failures_before_success_) / static_cast<double>(completed_ops_for_avg_);
        out += "Failures before success (min/avg/max): " + std::to_string(min_failures_before_success_ == std::numeric_limits<size_t>::max() ? 0 : min_failures_before_success_) + "/" +
               std::to_string(avg) + "/" + std::to_string(max_failures_before_success_) + "\n";
    } else {
        out += "Failures before success: (no completed ops)\n";
    }
    // Emit per-category histograms
    auto cat_name = [](PendingOpCategory c) -> const char* {
        switch (c) {
            case PendingOpCategory::Generic: return "Generic";
            case PendingOpCategory::Read: return "Read";
            case PendingOpCategory::ReadHeader: return "ReadHeader";
            case PendingOpCategory::Write: return "Write";
            default: return "Unknown";
        }
    };
    bool any_hist = false;
    for (size_t cat = 0; cat < category_count_; ++cat) {
        const auto &hist = completion_attempt_histograms_[cat];
        if (hist.empty()) continue;
        bool has_data = std::any_of(hist.begin(), hist.end(), [](size_t v){ return v != 0; });
        if (!has_data) continue;
        any_hist = true;
        out += std::string("Completion attempt distribution [") + cat_name(static_cast<PendingOpCategory>(cat)) + "]:\n";
        for (size_t i = 0; i < hist.size(); ++i) {
            size_t count = hist[i];
            if (count == 0) continue; // skip empty buckets for brevity
            if (i < hist.size() - 1) {
                out += "  " + std::to_string(i) + " : " + std::to_string(count) + "\n";
            } else {
                out += "  >=" + std::to_string(i) + " : " + std::to_string(count) + "\n";
            }
        }
    }
    if (!any_hist) {
        out += "(no histogram data)\n";
    }
    return out;
}

void CoroIoContext::log_detailed_statistics() const {
    if (!logger_) return;
    logger_->info(format_detailed_statistics());
}

CoroIoContext::FailureAttemptStats CoroIoContext::get_failure_attempt_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    FailureAttemptStats s{};
    s.samples = completed_ops_for_avg_;
    if (completed_ops_for_avg_ == 0) {
        s.min = 0;
        s.max = 0;
        s.average = 0.0;
    } else {
        s.min = (min_failures_before_success_ == std::numeric_limits<size_t>::max()) ? 0 : min_failures_before_success_;
        s.max = max_failures_before_success_;
        s.average = static_cast<double>(sum_failures_before_success_) / static_cast<double>(completed_ops_for_avg_);
    }
    return s;
}

// WorkGuard
CoroIoContext::WorkGuard::WorkGuard(std::shared_ptr<CoroIoContext> loop) : loop_(std::move(loop)) { increment_(); }
CoroIoContext::WorkGuard::WorkGuard(WorkGuard&& other) noexcept : loop_(std::move(other.loop_)), active_(other.active_) { other.active_ = false; }
CoroIoContext::WorkGuard& CoroIoContext::WorkGuard::operator=(WorkGuard&& other) noexcept {
    if (this != &other) {
        decrement_();
        loop_ = std::move(other.loop_);
        active_ = other.active_;
        other.active_ = false;
    }
    return *this;
}
CoroIoContext::WorkGuard::~WorkGuard() { decrement_(); }
void CoroIoContext::WorkGuard::increment_() { if (loop_ && active_) { loop_->outstanding_work_.fetch_add(1, std::memory_order_relaxed); loop_->wake_(); } }
void CoroIoContext::WorkGuard::decrement_() { if (loop_ && active_) { active_ = false; if (loop_->outstanding_work_.fetch_sub(1, std::memory_order_acq_rel) == 1) loop_->wake_(); } }

size_t CoroIoContext::get_total_operations_processed() const { std::lock_guard<std::mutex> lock(stats_mutex_); return total_operations_processed_; }

size_t CoroIoContext::get_thread_operations_processed(size_t thread_index) const {
    size_t n = thread_count_.load(std::memory_order_acquire);
    if (!per_thread_operations_processed_ || thread_index >= n) return 0;
    return per_thread_operations_processed_[thread_index].load(std::memory_order_acquire);
}

std::vector<size_t> CoroIoContext::get_operations_processed_per_thread() const {
    size_t n = thread_count_.load(std::memory_order_acquire);
    std::vector<size_t> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(per_thread_operations_processed_ ? per_thread_operations_processed_[i].load(std::memory_order_acquire) : 0);
    }
    return out;
}

void CoroIoContext::reset_statistics() {
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        total_operations_processed_ = 0;
    }
    size_t n = thread_count_.load(std::memory_order_acquire);
    for (size_t i = 0; i < n; ++i) {
        if (per_thread_operations_processed_) {
            per_thread_operations_processed_[i].store(0, std::memory_order_release);
        }
    }
}

} // namespace transport
