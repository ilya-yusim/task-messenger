#pragma once

#include <optional>
#include <string>

/**
 * \brief Rendezvous service client option accessors.
 *
 * Options are registered through the shared provider mechanism and then read
 * by generators and workers during startup to discover whether rendezvous
 * registration / discovery should be attempted.
 */
namespace rendezvous_opts {

/** \brief Register rendezvous CLI + JSON options (idempotent). */
void register_options();

/** \brief Whether rendezvous registration/discovery is enabled. */
std::optional<bool> get_enabled();

/** \brief ZeroTier IP or hostname of the rendezvous service. */
std::optional<std::string> get_host();

/** \brief TCP port of the rendezvous service. */
std::optional<int> get_port();

/** \brief VN listen host (rendezvous server only). */
std::optional<std::string> get_vn_listen_host();

/** \brief VN listen port for register/discover (rendezvous server only). */
std::optional<int> get_vn_listen_port();

/** \brief VN listen host for the snapshot relay listener (rendezvous server only). */
std::optional<std::string> get_snapshot_listen_host();

/** \brief VN listen port for monitoring snapshot reports (rendezvous server only). */
std::optional<int> get_snapshot_listen_port();

/** \brief HTTP dashboard listen host (rendezvous server only). */
std::optional<std::string> get_dashboard_listen_host();

/** \brief HTTP dashboard listen port (rendezvous server only). */
std::optional<int> get_dashboard_listen_port();

/** \brief Client-side: VN port of the rendezvous service monitoring snapshot listener. */
std::optional<int> get_snapshot_port();

} // namespace rendezvous_opts
