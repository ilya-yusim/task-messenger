/**
 * \file ISocketLifecycle.hpp
 * \brief Common lifecycle and endpoint query interface for all socket roles.
 * \ingroup socket_backend
 * \details Provides handle, endpoint, and basic readiness queries shared by
 * async and blocking client/server abstractions. Higher layers (e.g. coroutine
 * adapters) depend on this for generic closure and endpoint reporting.
 */
#pragma once

#include <cstdint>
#include <string>

/** \brief Base interface for common socket lifecycle and endpoint methods.
 *  \ingroup socket_backend
 */
struct ISocketLifecycle {
    virtual ~ISocketLifecycle() = default;

    /** \brief Close the underlying transport; subsequent operations invalid. */
    virtual void close() = 0;
    /** \brief Request shutdown - interrupts blocking operations. */
    virtual void shutdown() {}
    /** \brief True if underlying transport is currently open. */
    virtual bool is_open() const = 0;
    /** \brief Backend/native handle (or -1 if not applicable). */
    virtual int get_handle() const = 0;
    /** \brief Local endpoint string representation. */
    virtual std::string local_endpoint() const = 0;
    /** \brief Remote endpoint string representation. */
    virtual std::string remote_endpoint() const = 0;
    /** \brief Transport/backend type identifier (e.g. zerotier). */
    virtual std::string socket_type() const = 0;
    /** \brief Backend node id (0 when unsupported/not available). */
    virtual std::uint64_t node_id() const { return 0; }
    /** \brief Backend node id formatted as fixed-width lowercase hex (empty when unsupported). */
    virtual std::string node_id_hex() const { return {}; }
    /** \brief Optional readability diagnostic (default false). */
    virtual bool is_readable() const { return false; }
    /** \brief Optional writability diagnostic (default false). */
    virtual bool is_writable() const { return false; }
};
