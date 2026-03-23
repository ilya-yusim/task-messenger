// SessionStats.hpp - Statistics structure for sessions
#pragma once

#include <cstdint>
#include <chrono>
#include <string>

namespace session {

/**
 * @brief Session statistics and metrics
 * 
 * Tracks performance and operational metrics for a session
 */
struct SessionStats {
    std::chrono::steady_clock::time_point start_time;  // When session started
    uint32_t tasks_sent = 0;        // Number of tasks sent to client
    uint32_t tasks_completed = 0;   // Number of tasks completed successfully
    uint32_t tasks_failed = 0;      // Number of tasks that failed
    size_t bytes_sent = 0;          // Total bytes sent to client
    size_t bytes_received = 0;      // Total bytes received from client
    // Aggregate time spent on task round-trips (from send to full response),
    // explicitly excluding time waiting for next task from the pool
    std::chrono::nanoseconds total_task_roundtrip_time{0};
    std::chrono::nanoseconds last_task_roundtrip_time{0};
    uint32_t timed_tasks = 0;       // How many tasks contributed to timing stats
    
    /**
     * @brief Calculate session duration
     * @return Duration since session start
     */
    std::chrono::seconds get_duration() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - start_time);
    }
    
    /**
     * @brief Get success rate as percentage
     * @return Success rate (0.0 to 100.0)
     */
    double get_success_rate() const {
        uint32_t total_completed = tasks_completed + tasks_failed;
        if (total_completed == 0) return 0.0;
        return (static_cast<double>(tasks_completed) / total_completed) * 100.0;
    }

    /**
     * @brief Average roundtrip time in milliseconds for timed tasks
     */
    double get_avg_roundtrip_ms() const {
        if (timed_tasks == 0) return 0.0;
        auto avg_ns = total_task_roundtrip_time / timed_tasks;
        return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(avg_ns).count();
    }
};

} // namespace session