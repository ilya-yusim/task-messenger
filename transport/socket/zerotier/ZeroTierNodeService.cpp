/**
 * \file ZeroTierNodeService.cpp
 * \brief Implementation of ZeroTier node lifecycle and network management.
 * \ingroup socket_backend
 */
#include "ZeroTierNodeService.hpp"

#include <chrono>
#include <sstream>
#include <thread>
#include <optional>
#include <mutex>
#include <filesystem>
#include <atomic>

#include "processUtils.hpp" // For executable directory fallback

#include "options/Options.hpp"

namespace transport {

ZeroTierNodeService& ZeroTierNodeService::instance() {
    static ZeroTierNodeService svc;
    return svc;
}

// Static storage for parsed options
namespace {
    std::mutex g_zt_opts_mtx;
    std::optional<std::string> g_identity_path;
    std::optional<std::string> g_default_network_hex;
    std::atomic<bool> g_registered{false};
}

void ZeroTierNodeService::register_options() {
    bool expected = false;
    if (!g_registered.compare_exchange_strong(expected, true)) {
        return; // already registered
    }
    shared_opts::Options::add_provider([](CLI::App& app, const nlohmann::json& j){
        // Prepare defaults from JSON then bind directly to global option storage.
        std::string ident_default;
        std::string defnet_default;
        if (j.contains("zerotier")) {
            const auto& zt = j["zerotier"];
            if (zt.contains("identity_path") && zt["identity_path"].is_string()) {
                ident_default = zt["identity_path"].get<std::string>();
            }
            if (zt.contains("default_network") && zt["default_network"].is_string()) {
                defnet_default = zt["default_network"].get<std::string>();
            }
        }
        {
            std::lock_guard<std::mutex> lk(g_zt_opts_mtx);
            if (!ident_default.empty()) g_identity_path = ident_default;
            if (!defnet_default.empty()) g_default_network_hex = defnet_default;
        }
        // Bind options directly to the underlying global option storage.
    app.add_option("-Z,--zerotier-identity", g_identity_path, "ZeroTier node identity storage path (absolute or relative to config file)")->group("ZeroTier");
    app.add_option("--zerotier-default-network", g_default_network_hex, "Default ZeroTier network id (hex)")->group("ZeroTier");
    });
}

// Static auto-registration object
namespace {
    struct ZTServiceAutoReg {
        ZTServiceAutoReg() { ZeroTierNodeService::register_options(); }
    } zt_service_auto_reg_instance; // NOLINT(cert-err58-cpp)
}

std::optional<std::string> ZeroTierNodeService::get_configured_identity_path() {
    std::lock_guard<std::mutex> lk(g_zt_opts_mtx);
    return g_identity_path;
}

std::optional<std::string> ZeroTierNodeService::get_configured_default_network_hex() {
    std::lock_guard<std::mutex> lk(g_zt_opts_mtx);
    return g_default_network_hex;
}

void ZeroTierNodeService::set_logger(std::shared_ptr<Logger> logger) { logger_ = std::move(logger); }

ZeroTierNodeService::NetworkLease ZeroTierNodeService::acquire(uint64_t net_id) {
    std::scoped_lock lk(mtx_);
    ensure_node_started_locked();
    join_network_locked(net_id);
    return NetworkLease(this, net_id);
}

ZeroTierNodeService::NetworkLease ZeroTierNodeService::acquire_default() {
    // Determine hex network id: option override -> compile-time default
    std::string hex;
    if (auto opt = get_configured_default_network_hex(); opt && !opt->empty()) {
        hex = *opt;
    } else {
        hex = transport::ZeroTierNodeService::DEFAULT_ZEROTIER_NETWORK; // constant C-string
    }
    // Parse hex (ignore invalid -> will throw if entirely invalid and result is zero while string not representing zero length maybe)
    uint64_t netId = 0;
    try {
        netId = std::strtoull(hex.c_str(), nullptr, 16);
    } catch (...) {
        if (logger_) logger_->error("Invalid ZeroTier network id hex: " + hex);
        throw;
    }
    return acquire(netId);
}

std::optional<std::string> ZeroTierNodeService::get_ip_v4(uint64_t net_id) {
    char ip[ZTS_IP_MAX_STR_LEN] = {0};
    if (zts_addr_is_assigned(net_id, ZTS_AF_INET) == 0) return std::nullopt;
    if (zts_addr_get_str(net_id, ZTS_AF_INET, ip, ZTS_IP_MAX_STR_LEN) == ZTS_ERR_OK) {
        return std::string(ip);
    }
    return std::nullopt;
}

std::optional<std::string> ZeroTierNodeService::get_ip_v6(uint64_t net_id) {
    char ip[ZTS_IP_MAX_STR_LEN] = {0};
    if (zts_addr_is_assigned(net_id, ZTS_AF_INET6) == 0) return std::nullopt;
    if (zts_addr_get_str(net_id, ZTS_AF_INET6, ip, ZTS_IP_MAX_STR_LEN) == ZTS_ERR_OK) {
        return std::string(ip);
    }
    return std::nullopt;
}

void ZeroTierNodeService::stop() {
    std::scoped_lock lk(mtx_);
    for (auto it = join_counts_.begin(); it != join_counts_.end(); ++it) {
        if (logger_) logger_->info("Leaving ZeroTier network: " + std::to_string(it->first));
        zts_net_leave(it->first);
    }
    join_counts_.clear();
    if (node_started_) {
        if (logger_) logger_->info("Stopping ZeroTier node");
        zts_node_stop();
        // wait briefly for node to go offline
        for (int i = 0; i < 100; ++i) {
            if (!zts_node_is_online()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        node_started_ = false;
    }
}

void ZeroTierNodeService::ensure_node_started_locked() {
    if (node_started_) return;
    // If node is already online (e.g., started by legacy VN), adopt it
    if (zts_node_is_online()) {
        node_started_ = true;
        if (logger_) {
            std::stringstream ss; ss << std::hex << zts_node_get_id();
            logger_->info("ZeroTier node already online. ID=" + ss.str());
        }
        return;
    }
    if (logger_) logger_->info("Starting ZeroTier node");
    // Auto-apply identity path: if not explicitly set via API, use configured option
    if (!identity_path_) {
        // Acquire configured option (already set if provided) under global mutex
        {
            std::lock_guard<std::mutex> g(g_zt_opts_mtx);
            if (g_identity_path && !g_identity_path->empty()) {
                identity_path_ = *g_identity_path;
            }
        }
    }
    bool have_identity = false;
    if (identity_path_) {
        have_identity = resolve_identity_path();
        if (have_identity) {
            zts_init_from_storage(identity_path_->c_str());
        }
    }
    int err = zts_node_start();
    if (err != ZTS_ERR_OK) {
        if (logger_) logger_->error("zts_node_start failed: " + std::to_string(err));
        throw std::runtime_error("zts_node_start failed");
    }
    // Wait until the node is online
    while (!zts_node_is_online()) {
        if (shutdown_requested_.load(std::memory_order_relaxed)) {
            if (logger_) logger_->info("ZeroTier node start interrupted by shutdown");
            throw std::runtime_error("ZeroTier node start interrupted");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (logger_) {
        std::stringstream ss; ss << std::hex << zts_node_get_id();
        logger_->info("ZeroTier node online. ID=" + ss.str());
    }
    node_started_ = true;
}

// Helper moved here so it's seen before potential tooling parses usage; declared in header.
bool ZeroTierNodeService::resolve_identity_path() {
    try {
        if (!identity_path_) return false;
        // Identity path can be absolute or relative to the config file location.
        // Relative paths are resolved against the config directory if available.
        std::filesystem::path p = *identity_path_;
        if (p.is_relative()) {
            if (auto cfgDir = shared_opts::Options::get_config_dir(); cfgDir) {
                p = *cfgDir / p;
            }
        }
        std::error_code ec_abs;
        auto abs_p = std::filesystem::absolute(p, ec_abs);
        if (ec_abs) {
            if (logger_) logger_->warning(std::string{"Failed to resolve identity path to absolute: "} + ec_abs.message());
            identity_path_.reset();
            return false;
        }
        bool usable = false;
        std::error_code ec_stat;
        if (std::filesystem::exists(abs_p, ec_stat)) {
            if (std::filesystem::is_directory(abs_p, ec_stat)) {
                try { abs_p = std::filesystem::weakly_canonical(abs_p); } catch(...) {}
                usable = true;
            } else {
                if (logger_) logger_->warning("Identity path exists but is not a directory: " + abs_p.string() + ". Using ephemeral identity.");
            }
        } else {
            if (logger_) logger_->warning("Identity path does not exist: " + abs_p.string() + ". Using ephemeral identity (will NOT be created).");
        }
        if (usable) {
            identity_path_ = abs_p.string();
            if (logger_) logger_->info("Using identity path: " + *identity_path_);
            return true;
        }
        identity_path_.reset();
        return false;
    } catch(const std::exception& e) {
        if (logger_) logger_->error(std::string{"Failed to prepare identity path: "} + e.what());
        identity_path_.reset();
        return false;
    }
}

void ZeroTierNodeService::join_network_locked(uint64_t net_id) {
    auto& count = join_counts_[net_id];
    if (count == 0) {
        // If already joined and OK, skip explicit join and just refcount
        zts_network_status_t status = (zts_network_status_t)zts_net_get_status(net_id);
        if (status != ZTS_NETWORK_STATUS_OK) {
            int rc = zts_net_join(net_id);
            if (rc != ZTS_ERR_OK) {
                if (logger_) logger_->error("zts_net_join failed: " + std::to_string(rc));
                throw std::runtime_error("zts_net_join failed");
            }
            // Wait until network is ready
            for (;;) {
                if (shutdown_requested_.load(std::memory_order_relaxed)) {
                    if (logger_) logger_->info("ZeroTier network join interrupted by shutdown");
                    throw std::runtime_error("ZeroTier network join interrupted");
                }
                zts_network_status_t s = (zts_network_status_t)zts_net_get_status(net_id);
                if (s == ZTS_NETWORK_STATUS_OK) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        // Ensure an IP address is assigned before proceeding (prefer IPv4 since sockets use IPv4 path)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (zts_addr_is_assigned(net_id, ZTS_AF_INET) == 0) {
                if (shutdown_requested_.load(std::memory_order_relaxed)) {
                    if (logger_) logger_->info("ZeroTier IP assignment wait interrupted by shutdown");
                    throw std::runtime_error("ZeroTier IP assignment interrupted");
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    // Timed out waiting for IPv4; continue anyway (may have IPv6 or be assigned later)
                    if (logger_) logger_->warning("ZeroTier IPv4 not assigned within timeout; proceeding");
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        // Log IP assignment whether we just joined or were already joined

        if (logger_) {
            if (auto ip4 = get_ip_v4(net_id); ip4) {
                logger_->info("ZeroTier IPv4 assigned: " + *ip4);
            } else {
                logger_->debug("ZeroTier IPv4 not yet assigned (may still be pending)");
            }
            if (auto ip6 = get_ip_v6(net_id); ip6) {
                logger_->info("ZeroTier IPv6 assigned: " + *ip6);
            }
        }
    }
    ++count;
}

void ZeroTierNodeService::leave_network_locked(uint64_t net_id) {
    if (logger_) logger_->info("Leaving ZeroTier network: " + std::to_string(net_id));
    zts_net_leave(net_id);
}

void ZeroTierNodeService::release_network(uint64_t net_id) {
    std::scoped_lock lk(mtx_);
    auto it = join_counts_.find(net_id);
    if (it == join_counts_.end()) return;
    if (--(it->second) == 0) {
        leave_network_locked(net_id);
        join_counts_.erase(it);
    }
}

void ZeroTierNodeService::shutdown() {
    shutdown_requested_.store(true, std::memory_order_relaxed);
}

} // namespace transport
