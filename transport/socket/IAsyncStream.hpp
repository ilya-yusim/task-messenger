/**
 * \file IAsyncStream.hpp
 * \brief Combined async client + server stream interface.
 * \ingroup socket_backend
 * \details Aggregates non-blocking read/write plus connect/accept roles.
 * \see IClientSocket \see IServerSocket
 */
#pragma once

#include <cstddef>
#include <system_error>
#include "IClientSocket.hpp"
#include "IServerSocket.hpp"

/** \brief Unified async stream interface implementing both client and server roles.
 *  \ingroup socket_backend
 */
struct IAsyncStream : public virtual IClientSocket, public virtual IServerSocket {
    /**
     * \brief Attempt non-blocking read.
     *
     * Returns true when the operation has fully completed (all requested bytes
     * transferred, or a terminal error/disconnect has occurred).  Returns false
     * when no progress is possible yet (would block) **or** when only partial
     * data was transferred; in the partial case bytes_read > 0 and the caller
     * must retry with an adjusted buffer pointer and remaining length.
     */
    virtual bool try_read(void* buffer, size_t size, size_t& bytes_read, std::error_code& error) = 0;
    /**
     * \brief Attempt non-blocking write.
     *
     * Returns true when the operation has fully completed (all requested bytes
     * transferred, or a terminal error has occurred).  Returns false when no
     * progress is possible yet (would block) **or** when only part of the buffer
     * was sent; in the partial case bytes_written > 0 and the caller must retry
     * with an adjusted buffer pointer and remaining length.
     */
    virtual bool try_write(const void* buffer, size_t size, size_t& bytes_written, std::error_code& error) = 0;

    /**
     * \brief Non-consuming connection liveness check.
     *
     * Uses MSG_PEEK to probe the socket without consuming data.
     *
     * \param[out] error  Set to the transport error when a disconnect or
     *                    error is detected; cleared otherwise.
     * \return \c true if the connection appears alive (would-block or data
     *         available); \c false if a disconnect or error was detected.
     */
    virtual bool check_alive(std::error_code& error) = 0;
};
