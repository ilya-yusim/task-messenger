/**
 * \defgroup transport_module Manager Transport Module
 * \ingroup task_messenger_manager
 * \brief Accepts inbound worker connections, spins up coroutine IO, and hands
 *        sockets to the session subsystem.
 *
 * AsyncTransportServer owns the coroutine-aware IO context, the listening
 * socket, and the housekeeping thread that keeps inactive connections tidy.
 * The module exposes statistics helpers plus the enqueue API used by the
 * manager's task generators.
 * @{ 
 */
#pragma once

#include "logger.hpp"
#include "message/TaskMessagePool.hpp"
#include "transport/coro/CoroTask.hpp"
#include "transport/coro/coroIoContext.hpp"
#include "transport/coro/CoroSocketAdapter.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <algorithm>
#include <string>
#include <system_error>
#include <vector>
#include <chrono>
#include <thread>

// Accessors provided by transporter options provider (auto-registered once)
namespace transport_server_opts { std::optional<std::string> get_listen_host(); std::optional<int> get_listen_port(); }

namespace session {  class SessionManager; } // forward declaration

/**
 * \ingroup transport_module
 * \brief Coroutine-friendly TCP acceptor that routes sockets into sessions.
 *
 * The server boots a configurable `transport::CoroIoContext`, launches a
 * blocking acceptor thread, and acts as the façade that other manager
 * components use to enqueue new work or inspect runtime statistics.
 */
class AsyncTransportServer {
public:
    explicit AsyncTransportServer(std::shared_ptr<Logger> logger);
    ~AsyncTransportServer();

    /**
     * \brief Start the IO context and listening socket with an explicit endpoint.
     * \param host IPv4/IPv6 string to bind.
     * \param port TCP port number to bind.
     * \param backlog Size of the kernel listen backlog.
     * \return true on success, false if binding/listening fails.
     */
    bool start(const std::string& host, int port, int backlog = 128);

    /**
     * \brief Convenience overload that pulls host/port from `transport_server_opts`.
     * \param backlog Listen backlog identical to the explicit overload.
     */
    bool start(int backlog = 128);

    /**
     * \brief Stop accepting new connections and tear down IO threads.
     *
     * Safe to call multiple times; it drains the acceptor thread before closing
     * the listening socket to avoid race conditions inside lwIP.
     */
    void stop() noexcept;

    /**
     * \brief Producer-facing API used by task generators to push new work.
     */
    void enqueue_tasks(std::vector<TaskMessage> tasks);

    /**
     * \brief Dump IO-thread counters plus session-level statistics.
     */
    void print_transporter_statistics() const noexcept;

private:
    // Dedicated acceptor thread replaces coroutine accept loop
    void start_acceptor_thread();
    // Opportunistic maintenance trigger based on elapsed time (no extra threads)
    void maybe_run_maintenance() noexcept;
    void cleanup_closed_connections() noexcept;

private:
    std::shared_ptr<Logger> logger_;
    std::unique_ptr<session::SessionManager> session_manager_;

    std::atomic<bool> running_;
    std::shared_ptr<transport::CoroIoContext> io_;
    std::optional<transport::CoroIoContext::WorkGuard> io_guard_;
    std::shared_ptr<transport::CoroSocketAdapter> server_socket_;
    std::thread acceptor_thread_{};
    std::chrono::steady_clock::time_point last_maintenance_run_{};

    // Remember the listen endpoint for wake-up connections during shutdown
    std::string listen_host_{};
    int listen_port_ = 0;

    mutable std::mutex connections_mutex_;
    std::vector<std::shared_ptr<transport::CoroSocketAdapter>> active_connections_;
};

/// @}

