/**
 * \file worker/WorkerOptions.cpp
 * \brief Implementation of worker CLI and configuration option helpers.
 */

#include "WorkerOptions.hpp"
#include <options/Options.hpp>
#include <nlohmann/json.hpp>
#include <CLI/CLI.hpp>
#include <optional>
#include <string>

namespace transport { namespace worker_opts {

/// Cached runtime mode string from CLI or config.
static std::optional<std::string> g_mode_str;
/// Cached UI toggle supplied by configuration providers.
static std::optional<bool> g_ui_enabled;
/// Cached manager host override.
static std::optional<std::string> g_manager_host;
/// Cached manager port override.
static std::optional<int> g_manager_port;
/// Cached identity directory override path.
static std::optional<std::string> g_identity_dir_override;

std::optional<std::string> get_worker_mode() { return g_mode_str; }
std::optional<bool> get_ui_enabled() { return g_ui_enabled; }
std::optional<std::string> get_manager_host() { return g_manager_host; }
std::optional<int> get_manager_port() { return g_manager_port; }
std::optional<std::string> get_identity_dir_override() { return g_identity_dir_override; }

void register_options() {
    // Provider for worker mode and UI flag, plus worker-specific options
    shared_opts::Options::add_provider([](CLI::App& app, const nlohmann::json& j){
        std::string mode_default = "blocking";
        if (j.contains("worker") && j["worker"].contains("mode") && j["worker"]["mode"].is_string()) {
            mode_default = j["worker"]["mode"].get<std::string>();
        }
        g_mode_str = mode_default;
        app.add_option("--mode", g_mode_str, "Worker runtime mode: blocking|async")
            ->check(CLI::IsMember({"blocking","async"}))
            ->group("Worker");

        bool ui_default = true;
        if (j.contains("worker") && j["worker"].contains("ui") && j["worker"]["ui"].is_boolean()) {
            ui_default = j["worker"]["ui"].get<bool>();
        }
        g_ui_enabled = ui_default;
        app.add_flag("--noui,!--ui", g_ui_enabled, "Disable interactive terminal UI (run headless)")
            ->group("Worker");

        // Worker-specific connection options
        std::string host_default = "localhost";
        int port_default = 8080;
        if (j.contains("worker") && j["worker"].is_object()) {
            const auto& w = j["worker"];
            if (w.contains("manager_host") && w["manager_host"].is_string()) host_default = w["manager_host"].get<std::string>();
            if (w.contains("manager_port") && w["manager_port"].is_number_integer()) port_default = w["manager_port"].get<int>();
            if (w.contains("identity_dir") && w["identity_dir"].is_string()) g_identity_dir_override = w["identity_dir"].get<std::string>();
        }
        g_manager_host = host_default;
        g_manager_port = port_default;
        app.add_option("--manager-host", g_manager_host, "Manager host")
            ->group("Worker");
        app.add_option("--manager-port", g_manager_port, "Manager port")
            ->group("Worker");
        app.add_option("--identity-dir", g_identity_dir_override, "Override identity directory")
            ->group("Worker");
    });
}

} } // namespace transport::worker_opts

namespace {
    struct WorkerOptsAutoReg {
        WorkerOptsAutoReg() { transport::worker_opts::register_options(); }
    } worker_opts_auto_reg_instance; // NOLINT(cert-err58-cpp)
}
