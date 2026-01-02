/**
 * \file SocketFactory.hpp
 * \brief Factory helpers for creating role-based socket implementations.
 * \ingroup socket_backend
 * \details Centralizes backend resolution and construction.
 * \see IAsyncStream \see IBlockingStream \see transport::socket_opts::get_socket_type_raw
 */
#pragma once

#include <memory>

// Forward declarations of new role-based interfaces
struct IAsyncStream;
struct IServerSocket;
struct IBlockingStream;

#include <memory>
#include "logger.hpp"

namespace transport {

/** \brief Supported socket backend types for factory resolution. */
enum class SocketType
{
    ZeroTier
};

/** \brief Static factory for creating role-based socket implementations.
 *  \ingroup socket_backend
 *  \details Ensures consistent backend selection and optional logger propagation.
 *  \see transport::socket_opts::get_socket_type_raw
 */
class SocketFactory {
public:
    /** \brief Set default backend type used when options not provided. */
    static void set_default_socket_type(SocketType type) noexcept;
    /** \brief Retrieve current default backend type. */
    static SocketType get_default_socket_type() noexcept;

    /** \brief Create an async server stream with optional logger injection. */
    static std::shared_ptr<IAsyncStream> create_async_server(std::shared_ptr<Logger> logger);
    /** \brief Create an async client stream with optional logger injection. */
    static std::shared_ptr<IAsyncStream> create_async_client(std::shared_ptr<Logger> logger);
    /** \brief Create a blocking client stream with optional logger injection. */
    static std::shared_ptr<IBlockingStream> create_blocking_client(std::shared_ptr<Logger> logger);

private:
    // Static-only: prevent instantiation
    SocketFactory() = delete;

    static inline SocketType default_type_ = SocketType::ZeroTier;
};

} // namespace transport
