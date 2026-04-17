#include "RendezvousOptions.hpp"

#include "options/Options.hpp"

#include <atomic>
#include <mutex>

namespace {
std::mutex g_rendezvous_opts_mtx;
std::optional<bool> g_rendezvous_enabled;
std::optional<std::string> g_rendezvous_host;
std::optional<int> g_rendezvous_port;
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
        }

        {
            std::lock_guard<std::mutex> lk(g_rendezvous_opts_mtx);
            g_rendezvous_enabled = enabled_default;
            g_rendezvous_host = host_default;
            g_rendezvous_port = port_default;
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

} // namespace rendezvous_opts

namespace {
struct RendezvousOptionsAutoRegistration {
    RendezvousOptionsAutoRegistration() { rendezvous_opts::register_options(); }
} g_rendezvous_options_auto_registration; // NOLINT(cert-err58-cpp)
} // namespace
