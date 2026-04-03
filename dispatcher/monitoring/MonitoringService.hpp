#pragma once

#include "HttpRequestHandler.hpp"
#include "MonitoringSnapshotBuilder.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

class AsyncTransportServer;
namespace httplib { class Server; }

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
                      MonitoringSnapshotBuilder::UptimeProvider uptime_provider);
    ~MonitoringService();

    /** \brief Start listening and spawn acceptor thread. */
    bool start();

    /** \brief Stop accept loop and release listener resources. */
    void stop() noexcept;

    /** \brief Whether the service currently considers itself running. */
    bool is_running() const noexcept;

private:
    /** \brief Blocking cpp-httplib listen loop run on the acceptor thread. */
    void accept_loop();

    /** \brief Register /healthz and /api/monitor handlers on cpp-httplib server. */
    void register_routes();

    std::shared_ptr<Logger> logger_;
    AsyncTransportServer& server_;
    MonitoringSnapshotBuilder snapshot_builder_;

    std::atomic<bool> running_{false};
    std::unique_ptr<httplib::Server> http_server_;
    std::thread acceptor_thread_;
    std::string listen_host_;
    int listen_port_{0};
};

} // namespace monitoring
