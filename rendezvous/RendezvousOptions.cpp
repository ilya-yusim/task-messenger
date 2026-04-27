#include "RendezvousOptions.hpp"

#include "options/Options.hpp"

#include <atomic>
#include <mutex>

namespace {
std::mutex g_rendezvous_opts_mtx;

// Client-side (dispatcher/worker) — resolved endpoint of the rendezvous service.
std::optional<bool> g_rendezvous_enabled;
std::optional<std::string> g_rendezvous_host;
std::optional<int> g_rendezvous_port;
std::optional<int> g_rendezvous_snapshot_port;

// Server-side (tm-rendezvous) — local bind addresses/ports.
std::optional<std::string> g_rendezvous_vn_listen_host;
std::optional<int> g_rendezvous_vn_listen_port;
std::optional<std::string> g_rendezvous_snapshot_listen_host;
std::optional<int> g_rendezvous_snapshot_listen_port;
std::optional<std::string> g_rendezvous_dashboard_listen_host;
std::optional<int> g_rendezvous_dashboard_listen_port;

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
        int snapshot_port_default = 8089;

        std::string vn_listen_host_default = "0.0.0.0";
        int vn_listen_port_default = 8088;
        std::string snapshot_listen_host_default = "0.0.0.0";
        int snapshot_listen_port_default = 8089;
        std::string dashboard_listen_host_default = "127.0.0.1";
        int dashboard_listen_port_default = 8080;

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
            if (rj.contains("snapshot_port") && rj["snapshot_port"].is_number_integer()) {
                snapshot_port_default = rj["snapshot_port"].get<int>();
            }
            if (rj.contains("vn_listen_host") && rj["vn_listen_host"].is_string()) {
                vn_listen_host_default = rj["vn_listen_host"].get<std::string>();
            }
            if (rj.contains("vn_listen_port") && rj["vn_listen_port"].is_number_integer()) {
                vn_listen_port_default = rj["vn_listen_port"].get<int>();
            }
            if (rj.contains("snapshot_listen_host") && rj["snapshot_listen_host"].is_string()) {
                snapshot_listen_host_default = rj["snapshot_listen_host"].get<std::string>();
            }
            if (rj.contains("snapshot_listen_port") && rj["snapshot_listen_port"].is_number_integer()) {
                snapshot_listen_port_default = rj["snapshot_listen_port"].get<int>();
            }
            if (rj.contains("dashboard_listen_host") && rj["dashboard_listen_host"].is_string()) {
                dashboard_listen_host_default = rj["dashboard_listen_host"].get<std::string>();
            }
            if (rj.contains("dashboard_listen_port") && rj["dashboard_listen_port"].is_number_integer()) {
                dashboard_listen_port_default = rj["dashboard_listen_port"].get<int>();
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
            g_rendezvous_enabled = enabled_default;
            g_rendezvous_host = host_default;
            g_rendezvous_port = port_default;
            g_rendezvous_snapshot_port = snapshot_port_default;
            g_rendezvous_vn_listen_host = vn_listen_host_default;
            g_rendezvous_vn_listen_port = vn_listen_port_default;
            g_rendezvous_snapshot_listen_host = snapshot_listen_host_default;
            g_rendezvous_snapshot_listen_port = snapshot_listen_port_default;
            g_rendezvous_dashboard_listen_host = dashboard_listen_host_default;
            g_rendezvous_dashboard_listen_port = dashboard_listen_port_default;
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
        app.add_option("--rendezvous-snapshot-port", g_rendezvous_snapshot_port,
                       "Rendezvous service snapshot TCP port (default 8089)")
            ->check(CLI::Range(1, 65535))
            ->group("Rendezvous");

        app.add_option("--rendezvous-vn-listen-host", g_rendezvous_vn_listen_host,
                       "Server: VN listen host for register/discover (default 0.0.0.0)")
            ->group("Rendezvous Server");
        app.add_option("--rendezvous-vn-listen-port", g_rendezvous_vn_listen_port,
                       "Server: VN listen port for register/discover (default 8088)")
            ->check(CLI::Range(1, 65535))
            ->group("Rendezvous Server");
        app.add_option("--rendezvous-snapshot-listen-host", g_rendezvous_snapshot_listen_host,
                       "Server: VN listen host for snapshot reports (default 0.0.0.0)")
            ->group("Rendezvous Server");
        app.add_option("--rendezvous-snapshot-listen-port", g_rendezvous_snapshot_listen_port,
                       "Server: VN listen port for snapshot reports (default 8089)")
            ->check(CLI::Range(1, 65535))
            ->group("Rendezvous Server");
        app.add_option("--rendezvous-dashboard-listen-host", g_rendezvous_dashboard_listen_host,
                       "Server: HTTP dashboard listen host (default 127.0.0.1)")
            ->group("Rendezvous Server");
        app.add_option("--rendezvous-dashboard-listen-port", g_rendezvous_dashboard_listen_port,
                       "Server: HTTP dashboard listen port (default 8080)")
            ->check(CLI::Range(1, 65535))
            ->group("Rendezvous Server");
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

std::optional<int> get_snapshot_port() {
    std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
    return g_rendezvous_snapshot_port;
}

std::optional<std::string> get_vn_listen_host() {
    std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
    return g_rendezvous_vn_listen_host;
}

std::optional<int> get_vn_listen_port() {
    std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
    return g_rendezvous_vn_listen_port;
}

std::optional<std::string> get_snapshot_listen_host() {
    std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
    return g_rendezvous_snapshot_listen_host;
}

std::optional<int> get_snapshot_listen_port() {
    std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
    return g_rendezvous_snapshot_listen_port;
}

std::optional<std::string> get_dashboard_listen_host() {
    std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
    return g_rendezvous_dashboard_listen_host;
}

std::optional<int> get_dashboard_listen_port() {
    std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
    return g_rendezvous_dashboard_listen_port;
}

} // namespace rendezvous_opts

namespace {
struct RendezvousOptionsAutoRegistration {
    RendezvousOptionsAutoRegistration() { rendezvous_opts::register_options(); }
} g_rendezvous_options_auto_registration; // NOLINT(cert-err58-cpp)
} // namespace
