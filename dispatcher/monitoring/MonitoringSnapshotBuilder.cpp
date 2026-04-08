#include "MonitoringSnapshotBuilder.hpp"

#include <algorithm>
#include <chrono>

namespace monitoring {

MonitoringSnapshotBuilder::MonitoringSnapshotBuilder(std::shared_ptr<Logger> logger,
                                                     AsyncTransportServer& server,
                                                     UptimeProvider uptime_provider,
                                                     DispatcherStateProvider dispatcher_state_provider)
    : logger_(std::move(logger))
    , server_(server)
    , uptime_provider_(std::move(uptime_provider))
    , dispatcher_state_provider_(std::move(dispatcher_state_provider)) {}

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

    snapshot.task_queue_size = server_.get_task_queue_size();
    snapshot.workers_waiting = std::count_if(
        snapshot.workers.begin(),
        snapshot.workers.end(),
        [](const session::WorkerMonitoringSnapshot& worker) {
            return worker.worker_state == session::SessionState::WAITING_FOR_TASK;
        });
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
    snapshot.generator_status = dispatcher_state_provider_
                                  ? dispatcher_state_provider_()
                                  : std::string("unknown");

    return snapshot;
}

} // namespace monitoring
