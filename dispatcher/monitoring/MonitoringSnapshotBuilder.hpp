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
    using DispatcherStateProvider = std::function<std::string()>;

    /**
     * \brief Create snapshot builder bound to dispatcher runtime dependencies.
     * \param logger Shared logger.
     * \param server Dispatcher transport server (session/task data source).
     * \param uptime_provider Callback that returns dispatcher uptime in seconds.
    * \param dispatcher_state_provider Callback that returns dispatcher lifecycle state.
     */
    MonitoringSnapshotBuilder(std::shared_ptr<Logger> logger,
                              AsyncTransportServer& server,
                        UptimeProvider uptime_provider,
                        DispatcherStateProvider dispatcher_state_provider);

    /** \brief Build a complete monitoring payload for a single request. */
    DispatcherMonitoringSnapshot build() const;

private:
    std::shared_ptr<Logger> logger_;
    AsyncTransportServer& server_;
    UptimeProvider uptime_provider_;
    DispatcherStateProvider dispatcher_state_provider_;
};

} // namespace monitoring
