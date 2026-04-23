#include "RendezvousOptions.hpp"

#include "options/Options.hpp"

#include <atomic>
#include <mutex>

namespace {
std::mutex g_rendezvous_opts_mtx;
std::optional<bool> g_rendezvous_enabled;
std::optional<std::string> g_rendezvous_host;
std::optional<int> g_rendezvous_port;
std::optional<std::string> g_rendezvous_dashboard_host;
std::optional<int> g_rendezvous_dashboard_port;
std::optional<std::string> g_rendezvous_snapshot_listen_host;
std::optional<int> g_rendezvous_snapshot_port;
std::atomic<bool> g_rendezvous_registered{false};
} // namespace

namespace rendezvous_opts {

void register_options() {
    bool expected = false;
    if (!g_rendezvous_registered.compare_exchange_strong(expected, true)) {
        return;
    }

    shared_opts::Options::add_provider([](CLI::App& app, const nlohmann::json& j) {
        bool enabled_default = false;
        std::string host_default;
        int port_default = 8088;
        std::string dashboard_host_default = "0.0.0.0";
        int dashboard_port_default = 9090;
        std::string snapshot_listen_host_default = "0.0.0.0";
        int snapshot_port_default = 8089;

        if (j.contains("rendezvous") && j["rendezvous"].is_object()) {
            const auto& rj = j["rendezvous"];
            if (rj.contains("enabled") && rj["enabled"].is_boolean()) {
                enabled_default = rj["enabled"].get<bool>();
            }
            if (rj.contains("host") && rj["host"].is_string()) {
                host_default = rj["host"].get<std::string>();
            }
            if (rj.contains("port") && rj["port"].is_number_integer()) {
                port_default = rj["port"].get<int>();
            }
            if (rj.contains("dashboard_host") && rj["dashboard_host"].is_string()) {
                dashboard_host_default = rj["dashboard_host"].get<std::string>();
            }
            if (rj.contains("dashboard_port") && rj["dashboard_port"].is_number_integer()) {
                dashboard_port_default = rj["dashboard_port"].get<int>();
            }
            if (rj.contains("snapshot_listen_host") && rj["snapshot_listen_host"].is_string()) {
                snapshot_listen_host_default = rj["snapshot_listen_host"].get<std::string>();
            }
            if (rj.contains("snapshot_port") && rj["snapshot_port"].is_number_integer()) {
                snapshot_port_default = rj["snapshot_port"].get<int>();
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
            g_rendezvous_enabled = enabled_default;
            g_rendezvous_host = host_default;
            g_rendezvous_port = port_default;
            g_rendezvous_dashboard_host = dashboard_host_default;
            g_rendezvous_dashboard_port = dashboard_port_default;
            g_rendezvous_snapshot_listen_host = snapshot_listen_host_default;
            g_rendezvous_snapshot_port = snapshot_port_default;
        }

        app.add_option("--rendezvous-enabled", g_rendezvous_enabled,
                       "Enable rendezvous registration/discovery (default false)")
            ->group("Rendezvous");
        app.add_option("--rendezvous-host", g_rendezvous_host,
                       "ZeroTier IP of the rendezvous service")
            ->group("Rendezvous");
        app.add_option("--rendezvous-port", g_rendezvous_port,
                       "Rendezvous service TCP port (default 8088)")
            ->check(CLI::Range(1, 65535))
            ->group("Rendezvous");
        app.add_option("--rendezvous-dashboard-host", g_rendezvous_dashboard_host,
                       "Rendezvous HTTP dashboard listen host (default 0.0.0.0)")
            ->group("Rendezvous");
        app.add_option("--rendezvous-dashboard-port", g_rendezvous_dashboard_port,
                       "Rendezvous HTTP dashboard listen port (default 9090)")
            ->check(CLI::Range(1, 65535))
            ->group("Rendezvous");
        app.add_option("--rendezvous-snapshot-listen-host", g_rendezvous_snapshot_listen_host,
                       "Rendezvous VN listen host for snapshot reports (default 0.0.0.0)")
            ->group("Rendezvous");
        app.add_option("--rendezvous-snapshot-port", g_rendezvous_snapshot_port,
                       "Rendezvous VN port for monitoring snapshot reports (default 8089)")
            ->check(CLI::Range(1, 65535))
            ->group("Rendezvous");
    });
}

std::optional<bool> get_enabled() {
    std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
    return g_rendezvous_enabled;
}

std::optional<std::string> get_host() {
    std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
    return g_rendezvous_host;
}

std::optional<int> get_port() {
    std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
    return g_rendezvous_port;
}

std::optional<std::string> get_dashboard_host() {
    std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
    return g_rendezvous_dashboard_host;
}

std::optional<int> get_dashboard_port() {
    std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
    return g_rendezvous_dashboard_port;
}

std::optional<std::string> get_snapshot_listen_host() {
    std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
    return g_rendezvous_snapshot_listen_host;
}

std::optional<int> get_snapshot_port() {
    std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
    return g_rendezvous_snapshot_port;
}

} // namespace rendezvous_opts

namespace {
struct RendezvousOptionsAutoRegistration {
    RendezvousOptionsAutoRegistration() { rendezvous_opts::register_options(); }
} g_rendezvous_options_auto_registration; // NOLINT(cert-err58-cpp)
} // namespace
