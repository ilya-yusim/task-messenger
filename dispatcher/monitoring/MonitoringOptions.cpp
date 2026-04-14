#include "MonitoringOptions.hpp"

#include "options/Options.hpp"

#include <atomic>
#include <mutex>

namespace {
// Shared option storage guarded by a single mutex because writes happen once
// during parse and reads can occur from startup/runtime code paths.
std::mutex g_monitoring_opts_mtx;
std::optional<bool> g_monitoring_enabled;
std::optional<std::string> g_monitoring_listen_host;
std::optional<int> g_monitoring_listen_port;
std::optional<int> g_monitoring_snapshot_interval_ms;
std::atomic<bool> g_monitoring_registered{false};
} // namespace

namespace monitoring_opts {

void register_options() {
    // Providers can be registered by multiple translation units; enforce one-time registration.
    bool expected = false;
    if (!g_monitoring_registered.compare_exchange_strong(expected, true)) {
        return;
    }

    shared_opts::Options::add_provider([](CLI::App& app, const nlohmann::json& j) {
        // Defaults target local browser + local dispatcher prototype topology.
        bool enabled_default = true;
        std::string host_default = "127.0.0.1";
        int port_default = 9090;
        int snapshot_interval_default = 1000;

        // JSON config overrides defaults when provided.
        if (j.contains("monitoring") && j["monitoring"].is_object()) {
            const auto& mj = j["monitoring"];
            if (mj.contains("enabled") && mj["enabled"].is_boolean()) {
                enabled_default = mj["enabled"].get<bool>();
            }
            if (mj.contains("listen_host") && mj["listen_host"].is_string()) {
                host_default = mj["listen_host"].get<std::string>();
            }
            if (mj.contains("listen_port") && mj["listen_port"].is_number_integer()) {
                port_default = mj["listen_port"].get<int>();
            }
            if (mj.contains("snapshot_interval_ms") && mj["snapshot_interval_ms"].is_number_integer()) {
                const int v = mj["snapshot_interval_ms"].get<int>();
                if (v > 0) {
                    snapshot_interval_default = v;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_monitoring_opts_mtx);
            g_monitoring_enabled = enabled_default;
            g_monitoring_listen_host = host_default;
            g_monitoring_listen_port = port_default;
            g_monitoring_snapshot_interval_ms = snapshot_interval_default;
        }

        // CLI arguments override parsed defaults via bound option targets.
        app.add_option("--monitoring-enabled", g_monitoring_enabled,
                       "Enable monitoring HTTP service (default true)")
            ->group("Monitoring");
        app.add_option("--monitoring-listen-host", g_monitoring_listen_host,
                       "Monitoring listen host (default 127.0.0.1)")
            ->group("Monitoring");
        app.add_option("--monitoring-listen-port", g_monitoring_listen_port,
                       "Monitoring listen port (default 9090)")
            ->check(CLI::Range(1, 65535))
            ->group("Monitoring");
        app.add_option("--monitoring-snapshot-interval-ms", g_monitoring_snapshot_interval_ms,
                       "Snapshot interval hint in milliseconds (default 1000)")
            ->check(CLI::Range(50, 60000))
            ->group("Monitoring");
    });
}

std::optional<bool> get_enabled() {
    std::lock_guard<std::mutex> lk(g_monitoring_opts_mtx);
    return g_monitoring_enabled;
}

std::optional<std::string> get_listen_host() {
    std::lock_guard<std::mutex> lk(g_monitoring_opts_mtx);
    return g_monitoring_listen_host;
}

std::optional<int> get_listen_port() {
    std::lock_guard<std::mutex> lk(g_monitoring_opts_mtx);
    return g_monitoring_listen_port;
}

std::optional<int> get_snapshot_interval_ms() {
    std::lock_guard<std::mutex> lk(g_monitoring_opts_mtx);
    return g_monitoring_snapshot_interval_ms;
}

} // namespace monitoring_opts

namespace {
struct MonitoringOptionsAutoRegistration {
    // Auto-register with shared options system during static initialization.
    MonitoringOptionsAutoRegistration() { monitoring_opts::register_options(); }
} g_monitoring_options_auto_registration; // NOLINT(cert-err58-cpp)
} // namespace
