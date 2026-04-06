#include "MonitoringSnapshotBuilder.hpp"

#include <chrono>

namespace monitoring {

MonitoringSnapshotBuilder::MonitoringSnapshotBuilder(std::shared_ptr<Logger> logger,
                                                     AsyncTransportServer& server,
                                                     UptimeProvider uptime_provider)
    : logger_(std::move(logger)), server_(server), uptime_provider_(std::move(uptime_provider)) {}

DispatcherMonitoringSnapshot MonitoringSnapshotBuilder::build() const {
    DispatcherMonitoringSnapshot snapshot{};

    // Poll-driven monitor requests can be frequent; run maintenance via the
    // server's throttled gate to avoid redundant cleanup scans.
    server_.run_maintenance_if_due();

    // Phase A API is the canonical source for per-worker dispatcher-side state.
    auto* session_manager = server_.session_manager();
    if (session_manager) {
        snapshot.workers = session_manager->get_worker_monitoring_snapshot();
        snapshot.recent_disconnects = session_manager->get_recent_disconnects_snapshot();
    }

    const auto [task_pool_available, task_pool_waiting] = server_.get_task_pool_stats();
    snapshot.task_pool_available = task_pool_available;
    snapshot.task_pool_waiting = task_pool_waiting;
    snapshot.worker_count = snapshot.workers.size();

    const auto host = transport_server_opts::get_listen_host().value_or(std::string("0.0.0.0"));
    const auto port = transport_server_opts::get_listen_port().value_or(8080);
    snapshot.listen_host = host;
    snapshot.listen_port = port;

    snapshot.uptime_seconds = uptime_provider_ ? uptime_provider_() : 0;
    snapshot.snapshot_timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count();

    // Dispatcher node id is included in both generator_id and dispatcher_node_id fields.
    std::uint64_t dispatcher_node_id = 0;
    if (const auto server_stream = server_.server_stream()) {
        dispatcher_node_id = server_stream->node_id();
    }
    snapshot.dispatcher_node_id = format_node_id_hex(dispatcher_node_id);
    snapshot.generator_id = std::string("zt-") + snapshot.dispatcher_node_id;
    snapshot.generator_status = derive_generator_status(snapshot.workers);

    return snapshot;
}

std::string MonitoringSnapshotBuilder::derive_generator_status(
    const std::vector<session::WorkerMonitoringSnapshot>& workers) const {
    // No workers connected means dispatcher is reachable but idle.
    if (workers.empty()) {
        return "idle";
    }

    bool all_stale = true;
    bool any_degraded = false;
    bool any_active = false;
    bool any_assigned = false;

    for (const auto& worker : workers) {
        if (worker.dispatcher_fresh) {
            all_stale = false;
        } else {
            // Any stale worker downgrades aggregate health for v1 visibility.
            any_degraded = true;
        }

        switch (worker.dispatcher_state) {
        case session::DispatcherMonitoringState::AssignedActive:
            any_assigned = true;
            any_active = worker.dispatcher_fresh || any_active;
            break;
        case session::DispatcherMonitoringState::AssignedStalled:
            any_assigned = true;
            break;
        case session::DispatcherMonitoringState::Connecting:
        case session::DispatcherMonitoringState::Unknown:
            any_degraded = true;
            break;
        case session::DispatcherMonitoringState::Unassigned:
        default:
            break;
        }
    }

    if (all_stale) {
        return "offline";
    }
    if (any_degraded) {
        return "degraded";
    }
    if (any_active) {
        return "active";
    }
    if (!any_assigned) {
        return "idle";
    }
    return "active";
}

} // namespace monitoring
