/**
 * \file ZeroTierNodeService.hpp
 * \brief Shared ZeroTier node lifecycle and network join manager.
 * \ingroup socket_backend
 * \details Provides a singleton service to start/stop the libzt node, manage
 *  network join reference counts, expose address queries, and integrate with
 *  application options. Acquires and releases network membership via RAII leases.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <atomic>

#define ADD_EXPORTS // Ensure headers use the same export/import decoration as the built libzt
#include <ZeroTierSockets.h>
#include "logger.hpp"

namespace transport {

/** \brief Manages a shared ZeroTier node and joined networks.
 *  \details Centralizes node startup and network join/leave reference counting.
 *  Use \ref NetworkLease to keep a network joined while in scope.
 */
class ZeroTierNodeService {
public:
    /** \brief RAII lease that keeps a ZeroTier network joined while alive. */
    class NetworkLease {
    public:
        NetworkLease() = default;
        NetworkLease(ZeroTierNodeService* svc, uint64_t netId)
            : svc_(svc), netId_(netId) {}

        NetworkLease(const NetworkLease&) = delete;
        NetworkLease& operator=(const NetworkLease&) = delete;

        NetworkLease(NetworkLease&& other) noexcept { *this = std::move(other); }
        NetworkLease& operator=(NetworkLease&& other) noexcept {
            if (this != &other) {
                release();
                svc_ = other.svc_;
                netId_ = other.netId_;
                other.svc_ = nullptr;
                other.netId_ = 0;
            }
            return *this;
        }

        ~NetworkLease() { release(); }

        /** \brief True if this lease currently owns a network reference. */
        bool valid() const { return svc_ != nullptr; }
        /** \brief Joined network identifier (ZeroTier 64-bit ID). */
        uint64_t network_id() const { return netId_; }

        /** \brief Release the lease early, potentially leaving the network. */
        void release() {
            if (svc_) {
                svc_->release_network(netId_);
                svc_ = nullptr;
                netId_ = 0;
            }
        }

    private:
        ZeroTierNodeService* svc_ = nullptr;
        uint64_t netId_ = 0;
    };

    /** \brief Access the global singleton instance. */
    static ZeroTierNodeService& instance();

    /** \brief Inject an optional logger for diagnostics. */
    void set_logger(std::shared_ptr<Logger> logger);

    /** \brief Register CLI/JSON providers for ZeroTier-specific options. */
    static void register_options();

    /** \brief Get configured identity storage path, if any. */
    static std::optional<std::string> get_configured_identity_path();
    /** \brief Get configured default network id (hex), if any. */
    static std::optional<std::string> get_configured_default_network_hex();

    /** \brief Join a network and return an RAII lease token. */
    NetworkLease acquire(uint64_t net_id);

    /** \brief Join the configured default network and return a lease token. */
    NetworkLease acquire_default();

    /** \brief Get assigned IPv4 address for a joined network, if available. */
    std::optional<std::string> get_ip_v4(uint64_t net_id);
    /** \brief Get assigned IPv6 address for a joined network, if available. */
    std::optional<std::string> get_ip_v6(uint64_t net_id);

    /** \brief Leave all networks and stop the node (explicit teardown). */
    void stop();
    
    /** \brief Request shutdown - interrupts blocking network join/start operations. */
    void shutdown();

private:
    ZeroTierNodeService() = default;
    ~ZeroTierNodeService() = default;

    ZeroTierNodeService(const ZeroTierNodeService&) = delete;
    ZeroTierNodeService& operator=(const ZeroTierNodeService&) = delete;

    void ensure_node_started_locked();
    void join_network_locked(uint64_t net_id);
    void leave_network_locked(uint64_t net_id);
    void release_network(uint64_t net_id);
    /** \brief Resolve/validate identity_path_ to a usable directory (no initialization). */
    bool resolve_identity_path();

    static constexpr const char *DEFAULT_ZEROTIER_NETWORK = "159924d6303c474a";

    std::mutex mtx_;
    std::unordered_map<uint64_t, int> join_counts_;
    bool node_started_ = false;
    std::optional<std::string> identity_path_;
    std::shared_ptr<Logger> logger_;
    std::atomic<bool> shutdown_requested_{false};
};

} // namespace transport
