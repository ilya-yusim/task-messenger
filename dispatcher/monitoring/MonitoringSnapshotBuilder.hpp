#pragma once

#include "MonitoringSnapshot.hpp"

#include "../transport/AsyncTransportServer.hpp"

#include <functional>
#include <memory>

namespace monitoring {

/**
 * \brief Assembles DispatcherMonitoringSnapshot from live dispatcher state.
 *
 * The builder centralizes API payload derivation so HTTP handling remains a
 * thin transport wrapper.
 */
class MonitoringSnapshotBuilder {
public:
    using UptimeProvider = std::function<uint64_t()>;

    /**
     * \brief Create snapshot builder bound to dispatcher runtime dependencies.
     * \param logger Shared logger.
     * \param server Dispatcher transport server (session/task data source).
     * \param uptime_provider Callback that returns dispatcher uptime in seconds.
     */
    MonitoringSnapshotBuilder(std::shared_ptr<Logger> logger,
                              AsyncTransportServer& server,
                              UptimeProvider uptime_provider);

    /** \brief Build a complete monitoring payload for a single request. */
    DispatcherMonitoringSnapshot build() const;

private:
    /**
     * \brief Derive generator aggregate status from per-worker dispatcher state.
     *
     * This is a v1 heuristic and intentionally conservative when freshness is stale.
     */
    std::string derive_generator_status(
        const std::vector<session::WorkerMonitoringSnapshot>& workers) const;

    std::shared_ptr<Logger> logger_;
    AsyncTransportServer& server_;
    UptimeProvider uptime_provider_;
};

} // namespace monitoring
