#pragma once

#include "../session/SessionManager.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace monitoring {

/**
 * \brief Top-level Part B monitoring payload returned by /api/monitor.
 *
 * This shape is intentionally stable and extraction-ready so a future
 * out-of-process monitoring service can preserve browser API compatibility.
 */
struct DispatcherMonitoringSnapshot {
    std::string schema_version{"v1"};
    std::string generator_id;
    std::string dispatcher_node_id;
    std::string listen_host;
    int listen_port = 0;
    uint64_t uptime_seconds = 0;
    int64_t snapshot_timestamp_ms = 0;
    std::string generator_status;
    size_t worker_count = 0;
    size_t task_pool_available = 0;
    size_t task_pool_waiting = 0;
    std::vector<session::WorkerMonitoringSnapshot> workers;
};

/** \brief Convert a ZeroTier node id to fixed-width lowercase hex. */
inline std::string format_node_id_hex(uint64_t node_id) {
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << node_id;
    return oss.str();
}

/** \brief Normalize dispatcher state enum values to API-safe strings. */
inline std::string to_string(session::DispatcherMonitoringState state) {
    switch (state) {
    case session::DispatcherMonitoringState::Connecting:
        return "connecting";
    case session::DispatcherMonitoringState::AssignedActive:
        return "assigned_active";
    case session::DispatcherMonitoringState::AssignedIdle:
        return "assigned_idle";
    case session::DispatcherMonitoringState::AssignedStalled:
        return "assigned_stalled";
    case session::DispatcherMonitoringState::Unassigned:
        return "unassigned";
    case session::DispatcherMonitoringState::Unknown:
    default:
        return "unknown";
    }
}

} // namespace monitoring

namespace session {

/** \brief JSON serializer for session stats embedded in worker rows. */
inline void to_json(nlohmann::json& j, const SessionStats& stats) {
    j = nlohmann::json{
        {"tasks_sent", stats.tasks_sent},
        {"tasks_completed", stats.tasks_completed},
        {"tasks_failed", stats.tasks_failed},
        {"bytes_sent", stats.bytes_sent},
        {"bytes_received", stats.bytes_received},
        {"avg_roundtrip_ms", stats.get_avg_roundtrip_ms()},
        {"session_duration_s", stats.get_duration().count()},
    };
}

/** \brief JSON serializer for per-worker monitoring rows. */
inline void to_json(nlohmann::json& j, const WorkerMonitoringSnapshot& worker) {
    j = nlohmann::json{
        {"worker_node_id", monitoring::format_node_id_hex(worker.worker_node_id)},
        {"session_id", worker.session_id},
        {"remote_endpoint", worker.remote_endpoint},
        {"dispatcher_state", monitoring::to_string(worker.dispatcher_state)},
        {"tasks_sent", worker.stats.tasks_sent},
        {"tasks_completed", worker.stats.tasks_completed},
        {"tasks_failed", worker.stats.tasks_failed},
        {"bytes_sent", worker.stats.bytes_sent},
        {"bytes_received", worker.stats.bytes_received},
        {"avg_roundtrip_ms", worker.stats.get_avg_roundtrip_ms()},
        {"session_duration_s", worker.stats.get_duration().count()},
        {"last_seen_dispatcher_ts_ms", worker.last_seen_dispatcher_ts_ms},
        {"dispatcher_fresh", worker.dispatcher_fresh},
    };
}

} // namespace session

namespace monitoring {

/** \brief JSON serializer for top-level monitoring snapshot envelope. */
inline void to_json(nlohmann::json& j, const DispatcherMonitoringSnapshot& snapshot) {
    j = nlohmann::json{
        {"schema_version", snapshot.schema_version},
        {"generator_id", snapshot.generator_id},
        {"dispatcher_node_id", snapshot.dispatcher_node_id},
        {"listen_host", snapshot.listen_host},
        {"listen_port", snapshot.listen_port},
        {"uptime_seconds", snapshot.uptime_seconds},
        {"snapshot_timestamp_ms", snapshot.snapshot_timestamp_ms},
        {"generator_status", snapshot.generator_status},
        {"worker_count", snapshot.worker_count},
        {"task_pool_available", snapshot.task_pool_available},
        {"task_pool_waiting", snapshot.task_pool_waiting},
        {"workers", snapshot.workers},
    };
}

} // namespace monitoring
