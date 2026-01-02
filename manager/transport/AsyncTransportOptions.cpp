// AsyncTransportServer options provider with auto-registration
#include <optional>
#include "options/Options.hpp"
#include <mutex>
#include <atomic>
#include <thread> // retained (might be useful later even though default is fixed to 1)

namespace {
    std::mutex g_trans_opts_mtx;
    std::optional<std::string> g_trans_listen_host;
    std::optional<int> g_trans_listen_port;
    std::optional<int> g_trans_io_threads; // number of IO threads
    std::atomic<bool> g_trans_registered{false};
}

// Transporter options live alongside the transporter implementation
namespace transport_server_opts {

void register_options() {
    bool expected = false;
    if (!g_trans_registered.compare_exchange_strong(expected, true)) {
        return; // already registered
    }
    shared_opts::Options::add_provider([](CLI::App& app, const nlohmann::json& j){
        std::string host_default = "0.0.0.0";
        int port_default = 8080;
        int io_threads_default = 1; // DEFAULT: single thread unless explicitly configured

        // Prefer transporter section; fallback to manager section for backward compatibility
        if (j.contains("transport_server")) {
            const auto& tj = j["transport_server"];
            if (tj.contains("listen_host") && tj["listen_host"].is_string()) host_default = tj["listen_host"].get<std::string>();
            if (tj.contains("listen_port") && tj["listen_port"].is_number_integer()) port_default = tj["listen_port"].get<int>();
            if (tj.contains("io_threads") && tj["io_threads"].is_number_integer()) {
                int v = tj["io_threads"].get<int>();
                if (v > 0) io_threads_default = v; // override only if positive
            }
        } else if (j.contains("manager")) {
            const auto& mj = j["manager"];
            if (mj.contains("listen_host") && mj["listen_host"].is_string()) host_default = mj["listen_host"].get<std::string>();
            if (mj.contains("listen_port") && mj["listen_port"].is_number_integer()) port_default = mj["listen_port"].get<int>();
        }

        {
            std::lock_guard<std::mutex> lk(g_trans_opts_mtx);
            g_trans_listen_host = host_default;
            g_trans_listen_port = port_default;
            g_trans_io_threads = io_threads_default;
        }

        // Transporter-specific CLI flags
        app.add_option("--transporter-listen-host", g_trans_listen_host, "Transporter listen host (default 0.0.0.0)")->group("Transporter");
        app.add_option("--transporter-listen-port", g_trans_listen_port, "Transporter listen port (default 8080)")->group("Transporter");
        app.add_option("--transporter-io-threads", g_trans_io_threads,
            "Number of IO threads for CoroIoContext (default = 1)")
            ->check(CLI::Range(1, 512))
            ->group("Transporter");
    });
}

std::optional<std::string> get_listen_host() {
    std::lock_guard<std::mutex> lk(g_trans_opts_mtx);
    return g_trans_listen_host;
}

std::optional<int> get_listen_port() {
    std::lock_guard<std::mutex> lk(g_trans_opts_mtx);
    return g_trans_listen_port;
}

std::optional<int> get_io_threads() { // accessor
    std::lock_guard<std::mutex> lk(g_trans_opts_mtx);
    return g_trans_io_threads;
}

} // namespace transport_server_opts

// Static auto-registration object
namespace {
    struct TransportServerOptsAutoReg {
        TransportServerOptsAutoReg() { transport_server_opts::register_options(); }
    } transport_server_opts_auto_reg_instance; // NOLINT(cert-err58-cpp)
}
