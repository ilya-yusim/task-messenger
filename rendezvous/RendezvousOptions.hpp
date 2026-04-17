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

} // namespace rendezvous_opts
