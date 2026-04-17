#pragma once

#include "HttpRequestHandler.hpp"
#include "MonitoringSnapshotBuilder.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class AsyncTransportServer;
namespace httplib { class Server; }
namespace rendezvous { class RendezvousClient; }

namespace monitoring {

/**
 * \brief In-process HTTP monitoring endpoint service for dispatcher.
 *
 * The service owns a dedicated acceptor thread and serves read-only monitoring
 * data assembled from existing dispatcher/session runtime state.
 */
class MonitoringService {
public:
    /** \brief Construct service with runtime dependencies injected. */
    MonitoringService(std::shared_ptr<Logger> logger,
                      AsyncTransportServer& server,
                      MonitoringSnapshotBuilder::UptimeProvider uptime_provider,
                      MonitoringSnapshotBuilder::DispatcherStateProvider dispatcher_state_provider);
    ~MonitoringService();

    /** \brief Start listening and spawn acceptor thread. */
    bool start();

    /** \brief Stop accept loop and release listener resources. */
    void stop() noexcept;

    /** rief Whether the service currently considers itself running. */
    bool is_running() const noexcept;

    /** \brief Set rendezvous client for snapshot relay (thread-safe, nullable). */
    void set_rendezvous_client(std::shared_ptr<rendezvous::RendezvousClient> client);

private:
    /** \brief Blocking cpp-httplib listen loop run on the acceptor thread. */
    void accept_loop();

    /** \brief Register /healthz and /api/monitor handlers on cpp-httplib server. */
    void register_routes();
    /**
     * \brief Resolve the filesystem path to the dashboard static asset directory.
     *
     * Probes candidate locations in priority order:
     * 1. DASHBOARD_DIR compile-time define (set via Meson -DDASHBOARD_DIR=...).
    * 2. Repository-relative dashboard/ (dev/builddir layout).
     * 3. Executable-relative ./dashboard (installed layout).
     *
     * Returns empty string if no candidate directory is found.
     */
    static std::string resolve_dashboard_dir();
    std::shared_ptr<Logger> logger_;
    AsyncTransportServer& server_;
    MonitoringSnapshotBuilder snapshot_builder_;

    std::atomic<bool> running_{false};
    std::unique_ptr<httplib::Server> http_server_;
    std::thread acceptor_thread_;
    std::string listen_host_;
    int listen_port_{0};

    // Optional rendezvous client for snapshot relay (set after start).
    mutable std::mutex rv_mtx_;
    std::shared_ptr<rendezvous::RendezvousClient> rendezvous_client_;
};

} // namespace monitoring
