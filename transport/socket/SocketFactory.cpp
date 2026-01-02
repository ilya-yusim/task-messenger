/**
 * \file SocketFactory.cpp
 * \brief Backend resolution and construction logic for role-based sockets.
 * \ingroup socket_backend
 * \details Resolves user-selected backend (via options) once, caches default
 * type, and constructs async or blocking sockets with optional logger injection.
 */
#include "SocketFactory.hpp"
// New role-based interfaces
#include "IAsyncStream.hpp"
#include "IServerSocket.hpp"
#include "zerotier/ZeroTierSocket.hpp" // kept private to implementation
#include "zerotier/ZeroTierNodeService.hpp" // for logger injection when provided
#include "SocketTypeOptions.hpp"
#include <mutex>

namespace transport {

void SocketFactory::set_default_socket_type(SocketType type) noexcept { default_type_ = type; }
SocketType SocketFactory::get_default_socket_type() noexcept { return default_type_; }

/** \brief Resolve and cache user-specified socket backend (idempotent). */
static void ensure_socket_type_resolved() {
    static std::once_flag resolved_flag;
    std::call_once(resolved_flag, [](){
        if (auto raw = transport::socket_opts::get_socket_type_raw(); raw && !raw->empty()) {
            // Map supported backends (currently only zerotier). Unknown -> keep default and warn.
            std::string lower; lower.reserve(raw->size());
            for (char c : *raw) lower.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(c))));
            if (lower == "zerotier" || lower == "zt") {
                SocketFactory::set_default_socket_type(SocketType::ZeroTier);
            } else {
                // Basic stderr warning (logger may not be initialized yet)
                fprintf(stderr, "[SocketFactory] Warning: Unknown --socket-type '%s'; using default.\n", raw->c_str());
            }
        }
    });
}

std::shared_ptr<IAsyncStream> SocketFactory::create_async_server(std::shared_ptr<Logger> logger) {
    ensure_socket_type_resolved();
    switch (default_type_) {
        case SocketType::ZeroTier: {
            if (logger) {
                transport::ZeroTierNodeService::instance().set_logger(logger);
                return ZeroTierSocket::create(std::move(logger));
            }
            return ZeroTierSocket::create();
        }
        default:
            throw std::invalid_argument("Unsupported socket type for SocketFactory async server (logger)");
    }
}

std::shared_ptr<IAsyncStream> SocketFactory::create_async_client(std::shared_ptr<Logger> logger) {
    ensure_socket_type_resolved();
    switch (default_type_) {
        case SocketType::ZeroTier:
            if (logger) {
                transport::ZeroTierNodeService::instance().set_logger(logger);
                return ZeroTierSocket::create(std::move(logger));
            }
            return ZeroTierSocket::create();
        default:
            throw std::invalid_argument("Unsupported socket type for SocketFactory async client (logger)");
    }
}

std::shared_ptr<IBlockingStream> SocketFactory::create_blocking_client(std::shared_ptr<Logger> logger) {
    ensure_socket_type_resolved();
    switch (default_type_) {
        case SocketType::ZeroTier: {
            if (logger) {
                transport::ZeroTierNodeService::instance().set_logger(logger);
                return ZeroTierSocket::create_blocking(std::move(logger));
            }
            return ZeroTierSocket::create_blocking();
        }
        default:
            throw std::invalid_argument("Unsupported socket type for SocketFactory blocking client (logger)");
    }
}

} // namespace transport
