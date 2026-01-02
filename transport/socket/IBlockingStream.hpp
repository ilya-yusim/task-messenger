/**
 * \file IBlockingStream.hpp
 * \brief Blocking stream interface (read/write + connect).
 * \ingroup socket_backend
 * \details Used for synchronous I/O without coroutine integration.
 * \see IClientSocket
 */
#pragma once

#include <cstddef>
#include <system_error>
#include "IClientSocket.hpp"

/** \brief Blocking stream role interface (client + read/write).
 *  \ingroup socket_backend
 */
struct IBlockingStream : public virtual IClientSocket {
    /** \brief Blocking read; fills `bytes_read`, sets `error` on failure. */
    virtual void read(void* buffer, size_t size, size_t& bytes_read, std::error_code& error) = 0;
    /** \brief Blocking write; fills `bytes_written`, sets `error` on failure. */
    virtual void write(const void* buffer, size_t size, size_t& bytes_written, std::error_code& error) = 0;
};
