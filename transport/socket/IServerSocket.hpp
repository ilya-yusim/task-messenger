/**
 * \file IServerSocket.hpp
 * \brief Server/acceptor interface.
 * \ingroup socket_backend
 * \details Defines server startup and a unified accept primitive.  The factory
 * controls what kind of child socket accept() returns (non-blocking vs blocking)
 * so callers simply downcast to IAsyncStream or IBlockingStream as appropriate.
 * \see IClientSocket
 */
#pragma once

#include <memory>
#include <system_error>
#include <string>
#include "ISocketLifecycle.hpp"

struct IClientSocket; // fwd

/** \brief Server role interface (listen + accept).
 *  \ingroup socket_backend
 *
 *  A single `accept()` method replaces the former `try_accept`,
 *  `blocking_accept`, and `accept_blocking` variants.  The concrete type of
 *  the returned `IClientSocket` (non-blocking `IAsyncStream` or blocking
 *  `IBlockingStream`) is determined by the factory that created this server
 *  socket—callers use `static_pointer_cast` when they need the concrete role.
 */
struct IServerSocket : public virtual ISocketLifecycle {

    /** \brief Bind + listen convenience for server startup.
     *  \details Performs the platform-specific bind + listen sequence and applies any
     *  backend socket options (normalization, permissions) required. Prefer this over
     *  calling low-level steps directly so backend behavior stays encapsulated.
     *  Implementations should avoid throwing and instead log failures internally.
     *  \param host Interface/address to bind (e.g. "0.0.0.0").
     *  \param port Port number to listen on.
     *  \param backlog Hint for pending connection queue length (platform may clamp).
     *  \return true on success, false on failure (with errors logged internally).
     */
    virtual bool start_listening(const std::string& host, int port, int backlog) = 0;

    /** \brief Timed blocking accept supporting responsive shutdown.
     *  \details Rationale :
     *  We considered building a custom accept loop around a non-blocking accept +
     *  carefully tuned sleeps. That approach becomes a balancing act between latency
     *  (how fast we accept a new client) and CPU efficiency (avoiding busy-wait). By
     *  delegating blocking behavior to the underlying TCP/IP stack with a recv timeout
     *  we leverage its existing, well-tested scheduling and wake-up logic instead of
     *  reinventing it. A moderately long timeout keeps the acceptor thread almost idle
     *  under no load while still bounding shutdown latency.
     *
     *  Default Timeout Choice :
     *  500ms strikes a practical balance: negligible CPU usage in idle periods and
     *  sub-second responsiveness when initiating shutdown. Platforms exposing finer
     *  control (e.g. SO_RCVTIMEO) handle the timing internally; if unsupported, backend
     *  implementations should approximate.
     *
     *  Behavior :
     *  - Returns a new connected client on success (error cleared).
     *  - Returns nullptr with error cleared when the timeout elapses or transient
     *    conditions (e.g. would-block, aborted) occur; caller can loop and check its own
     *    shutdown flag.
     *  - Returns nullptr with error set on non-transient failures.
     *
     *  The returned IClientSocket is either an IAsyncStream or IBlockingStream depending
     *  on how the server was constructed (factory controls child socket mode).
     *
     *  \param error Receives non-transient errors; cleared on success or timeout.
     */
    virtual std::shared_ptr<IClientSocket> accept(std::error_code& error) = 0;
};
