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
    /** \brief Attempt non-blocking read; returns true when completed (success or error). */
    virtual bool try_read(void* buffer, size_t size, size_t& bytes_read, std::error_code& error) = 0;
    /** \brief Attempt non-blocking write; returns true when completed (success or error). */
    virtual bool try_write(const void* buffer, size_t size, size_t& bytes_written, std::error_code& error) = 0;
};
