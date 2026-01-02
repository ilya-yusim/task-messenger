/**
 * \file SocketTypeOptions.hpp
 * \brief Option registration helpers for selecting socket backend type.
 * \ingroup socket_backend
 * \details Provides access to user-specified socket backend choice (e.g. zerotier)
 * captured via CLI/config options. Factory uses `get_socket_type_raw()` to resolve
 * the effective backend at runtime.
 */
#pragma once
#include <optional>
#include <string>

namespace transport { namespace socket_opts {
/** \brief Register CLI/config options for socket backend type (idempotent). */
void register_options();
/** \brief Raw user-provided socket type string (if any). */
std::optional<std::string> get_socket_type_raw();
}} // namespace transport::socket_opts
