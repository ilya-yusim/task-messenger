/**
 * \file socket_groups.hpp
 * \brief Doxygen group definitions for the socket layer.
 * \details Centralizes group declarations so interfaces and backends can tag
 *  themselves with \ingroup socket_backend.
 */

/** \defgroup socket_backend Socket Backend
 *  \brief Role-based socket interfaces and concrete backends.
 *  \details This group contains the transport/socket interfaces (async/blocking
 *  stream, client, server, lifecycle), factories/options, and backend
 *  implementations (e.g., ZeroTier). See README.md in this directory for an
 *  architectural overview and examples.
 */
