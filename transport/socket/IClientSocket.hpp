/**
 * \file IClientSocket.hpp
 * \brief Client connection interface.
 * \ingroup socket_backend
 * \details Provides synchronous connect semantics used in contexts where
 * coroutine or event loop integration is not required.
 */
#pragma once

#include <string>
#include <system_error>
#include "ISocketLifecycle.hpp"

/** \brief Client socket role interface.
 *  \ingroup socket_backend
 */
struct IClientSocket : public virtual ISocketLifecycle {
    /** \brief Establish a blocking connection; sets `error` on failure (non-throwing). */
    virtual void connect(const std::string& host, int port, std::error_code& error) = 0;
};
