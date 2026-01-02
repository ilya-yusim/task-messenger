/**
 * \file worker/WorkerOptions.hpp
 * \brief Shared option types and accessors for the worker process.
 */
#pragma once

#include <string>
#include <optional>

/** \brief Runtime execution strategy selected for the worker. */
enum class WorkerMode { Blocking, Async };

/** \brief Aggregated worker connection and runtime configuration. */
struct WorkerOptions {
    WorkerMode mode{WorkerMode::Blocking}; ///< Coroutine or blocking runtime selection.
    std::string host{"localhost"};       ///< Manager host name or IP.
    int port{8080};                       ///< Manager control port.
};

/** \brief Helper API for accessing worker-specific CLI and config options. */
namespace transport { namespace worker_opts {
    std::optional<std::string> get_manager_host();
    std::optional<int> get_manager_port();
    std::optional<std::string> get_identity_dir_override();
    std::optional<std::string> get_worker_mode();
    std::optional<bool> get_ui_enabled();
    void register_options();
} }
