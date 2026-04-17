/**
 * \file IBlockingServerSocket.hpp
 * \brief Blocking server/acceptor interface that yields blocking streams.
 * \ingroup socket_backend
 * \details Server-role interface for code that uses blocking I/O exclusively
 * (e.g. the rendezvous service).  Unlike IServerSocket, whose accept methods
 * return IAsyncStream, this interface returns IBlockingStream so callers never
 * need to dynamic_cast accepted connections.
 * \see IServerSocket, IBlockingStream
 */
#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <system_error>

#include "ISocketLifecycle.hpp"

struct IBlockingStream; // fwd

/** \brief Blocking server role interface (listen + accept → IBlockingStream).
 *  \ingroup socket_backend
 */
struct IBlockingServerSocket : public virtual ISocketLifecycle {

    /** \brief Bind + listen convenience for server startup.
     *  \param host Interface/address to bind (e.g. "0.0.0.0").
     *  \param port Port number to listen on.
     *  \param backlog Hint for pending connection queue length.
     *  \return true on success, false on failure.
     */
    virtual bool start_listening(const std::string& host, int port, int backlog) = 0;

    /** \brief Timed blocking accept that returns a blocking stream.
     *  \details Blocks up to \p timeout waiting for a new client connection.
     *  - On success: returns a connected IBlockingStream (error cleared).
     *  - On timeout / transient condition: returns nullptr with error cleared.
     *  - On fatal error: returns nullptr with error set.
     *  \param error Receives non-transient errors; cleared otherwise.
     *  \param timeout Maximum blocking interval before returning nullptr.
     */
    virtual std::shared_ptr<IBlockingStream> accept_blocking(
        std::error_code& error,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) = 0;
};
