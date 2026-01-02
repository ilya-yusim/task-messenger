/**
 * \file coroIoContext.hpp
 * \brief Coroutine-aware I/O/event loop context and metrics.
 * \details Pending operations register non-blocking `try_complete()` functors.
 * Event threads poll and resume associated coroutine handles when ready. Uses
 * `notify_one()` wakeups with a short poll fallback and collects per-category
 * completion histograms and per-thread resume counters. Use `WorkGuard` to
 * keep the loop alive while preparing work.
 */
// CoroIoContext.hpp — Coroutine-aware I/O/event loop context.
#pragma once

#include "logger.hpp"
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <coroutine>
#include <condition_variable>
#include <vector>
#include <functional>
#include <array>

namespace transport {

class CoroSocketAdapter; // fwd

/** \defgroup coro_context I/O Context
 *  \ingroup coro_module
 *  \brief Event-loop for coroutine scheduling and pending operation polling.
 */

/** \defgroup coro_stats Statistics
 *  \ingroup coro_context
 *  \brief Metrics and reporting for completion attempts and per-thread processing.
 */

/** \brief Coroutine-aware I/O/event loop context that polls pending operations and resumes coroutines.
 *  \details Registers non-blocking `try_complete()` functors and resumes the associated coroutine
 *  handle on readiness; gathers per-thread counters and completion attempt histograms.
 *  \ingroup coro_context
 */
class CoroIoContext : public std::enable_shared_from_this<CoroIoContext> {
public:
	/** \brief Construct an event loop; call `start()` or `run()` to begin processing. */
	CoroIoContext();
	~CoroIoContext();

	// --- Types ---
	/** \brief Classification for per-category completion histograms. */
	enum class PendingOpCategory : uint8_t { Generic = 0, Read, ReadHeader, Write, Timer, Count };
	/** \brief Number of categories as a compile-time constant for array sizing. */
	static constexpr size_t category_count_ = static_cast<size_t>(PendingOpCategory::Count);

	// --- Lifecycle ---
	/** \brief Start the loop with one worker thread. */
	void start();
	/** \brief Start the loop with `threads` worker threads (minimum 1). */
	void start(size_t threads);
	/** \brief Run on current thread (single-thread convenience, index 0). */
	void run();
	/** \brief Run loop body for the given worker thread index. */
	void run(size_t thread_index);
	/** \brief Request shutdown and join worker threads. */
	void stop();
	/** \brief True if loop is running and not yet stopped. */
	bool is_running() const;

	// --- Logger ---
	/** \brief Set optional logger for info/error messages. */
	void set_logger(std::shared_ptr<Logger> logger);
	/** \brief Get the currently configured logger (may be null). */
	std::shared_ptr<Logger> get_logger() const;

	// --- Pending operations registration ---
	/** \brief Register a pending operation; resumes `handle` when predicate returns true.
	 *  \see transport::CoroSocketAdapter::async_read
	 *  \see transport::CoroSocketAdapter::async_write
	 */
	void register_pending(std::function<bool()> try_complete, std::coroutine_handle<> handle);
	/** \brief Register a categorized pending operation for metrics.
	 *  \see transport::CoroSocketAdapter::async_read_header
	 */
	void register_pending(PendingOpCategory category, std::function<bool()> try_complete, std::coroutine_handle<> handle);

	// --- Work guard ---
	/** \brief RAII object that increments outstanding work to keep the loop alive. */
	class WorkGuard {
	public:
		explicit WorkGuard(std::shared_ptr<CoroIoContext> loop);
		WorkGuard(const WorkGuard&) = delete;
		WorkGuard& operator=(const WorkGuard&) = delete;
		WorkGuard(WorkGuard&& other) noexcept;
		WorkGuard& operator=(WorkGuard&& other) noexcept;
		~WorkGuard();
		/** \brief True while this guard contributes to outstanding work. */
		bool active() const noexcept { return active_; }
	private:
		void increment_();
		void decrement_();
		std::shared_ptr<CoroIoContext> loop_;
		bool active_{true};
	};
	/** \brief Create a new work guard for this context. */
	WorkGuard make_work_guard() { return WorkGuard(shared_from_this()); }

	// --- Statistics & metrics ---
	/** \brief Aggregated histogram across all categories.
	 *  \ingroup coro_stats
	 */
	std::vector<size_t> get_completion_attempt_histogram() const;
	/** \brief Per-category completion attempt histograms (copy).
	 *  \ingroup coro_stats
	 */
	std::array<std::vector<size_t>, category_count_> get_completion_attempt_histograms_by_category() const;
	/** \brief Human-readable multi-line summary of detailed statistics.
	 *  \ingroup coro_stats
	 */
	std::string format_detailed_statistics() const;
	/** \brief Log detailed statistics via logger (if set).
	 *  \ingroup coro_stats
	 */
	void log_detailed_statistics() const;
	/** \brief Failure attempt aggregate statistics.
	 *  \ingroup coro_stats
	 */
	struct FailureAttemptStats { size_t min; size_t max; double average; unsigned long long samples; };
	/** \brief Retrieve failure attempt aggregate statistics.
	 *  \ingroup coro_stats
	 */
	FailureAttemptStats get_failure_attempt_stats() const;
	/** \brief Total operations processed across all threads.
	 *  \ingroup coro_stats
	 */
	size_t get_total_operations_processed() const;
	/** \brief Reset all statistics (histograms preserved sizing).
	 *  \ingroup coro_stats
	 */
	void reset_statistics();
	/** \brief Operations processed by given thread index.
	 *  \ingroup coro_stats
	 */
	size_t get_thread_operations_processed(size_t thread_index) const;
	/** \brief Vector of per-thread processed operation counts.
	 *  \ingroup coro_stats
	 */
	std::vector<size_t> get_operations_processed_per_thread() const;

private:
	/** \brief Process all currently pending operations for a worker thread; requeues unfinished. */
	void process_pending_ops(size_t thread_index);

	/** \brief Internal representation of a pending operation awaiting readiness. */
	struct PendingOp {
		std::function<bool()> try_complete;      ///< Readiness predicate/work attempt
		std::coroutine_handle<> handle;          ///< Coroutine to resume on success
		uint16_t attempts{0};                    ///< Failed attempts before success
		PendingOpCategory category{PendingOpCategory::Generic}; ///< Metrics category
	};
	std::vector<PendingOp> pending_ops_;
	std::mutex pending_mutex_;
	std::condition_variable pending_cv_;
	/** \brief Wake a single waiting loop thread (avoid stampede). */
	void wake_() { pending_cv_.notify_one(); }

	std::atomic<bool> running_{false};
	std::vector<std::thread> event_threads_;
	std::shared_ptr<Logger> logger_;
	/** \brief Maximum sleep interval before re-checking state even without notification. */
	std::chrono::milliseconds poll_interval_{std::chrono::milliseconds(10)};

	// Statistics state
	mutable std::mutex stats_mutex_;
	size_t total_operations_processed_{0};
	std::unique_ptr<std::atomic<size_t>[]> per_thread_operations_processed_;
	std::atomic<size_t> thread_count_{0};
	std::atomic<size_t> outstanding_work_{0};
	static constexpr size_t max_tracked_attempts_ = 1024; ///< Buckets 0..1023 (1023 aggregates 1023+)
	mutable std::array<std::vector<size_t>, category_count_> completion_attempt_histograms_{}; ///< One histogram per category
	mutable size_t min_failures_before_success_{std::numeric_limits<size_t>::max()};
	mutable size_t max_failures_before_success_{0};
	mutable unsigned long long sum_failures_before_success_{0};
	mutable unsigned long long completed_ops_for_avg_{0};
};

namespace detail {
inline std::shared_ptr<CoroIoContext> get_default_context() {
	static std::weak_ptr<CoroIoContext> weak;
	static std::mutex m;
	std::lock_guard<std::mutex> lk(m);
	auto s = weak.lock();
	if (!s) {
		s = std::make_shared<CoroIoContext>();
		// Start with single thread; higher-level code (AsyncTransportServer, etc.) decides if multi-threading is needed.
		s->start();
		weak = s;
	}
	return s;
}
}

inline std::shared_ptr<CoroIoContext> default_loop() { return detail::get_default_context(); }

} // namespace transport
