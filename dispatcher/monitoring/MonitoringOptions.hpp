#pragma once

#include <optional>
#include <string>

/**
 * \brief Dispatcher monitoring option accessors.
 *
 * Options are registered through the shared provider mechanism and then read
 * by DispatcherApp/MonitoringService during startup.
 */
namespace monitoring_opts {

/** \brief Register monitoring CLI + JSON options (idempotent). */
void register_options();

/** \brief Whether monitoring HTTP service should run. */
std::optional<bool> get_enabled();

/** \brief Host/interface used by monitoring listener. */
std::optional<std::string> get_listen_host();

/** \brief TCP port used by monitoring listener. */
std::optional<int> get_listen_port();

/** \brief Snapshot cadence hint for consumers (currently informational). */
std::optional<int> get_snapshot_interval_ms();

} // namespace monitoring_opts
