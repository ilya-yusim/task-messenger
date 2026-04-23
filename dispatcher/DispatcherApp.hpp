// DispatcherApp.hpp - Application harness for dispatcher infrastructure startup
#pragma once

#include "transport/AsyncTransportServer.hpp"
#include "monitoring/MonitoringService.hpp"
#include "message/TaskSubmitAwaitable.hpp"
#include "logger.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>

namespace rendezvous { class RendezvousClient; }
namespace monitoring { class SnapshotReporter; }

/**
 * \brief Application harness that encapsulates common dispatcher startup.
 *
 * Handles logger setup, CLI/JSON option parsing, AsyncTransportServer lifecycle,
 * and signal handler installation. Generators create a DispatcherApp, call start(),
 * run their own loop using submit_task(), then call stop() for clean shutdown.
 */
class DispatcherApp {
public:
    enum class LifecycleState {
        Starting,
        Running,
        NoTasks,
        NoWorkers,
        Stopping,
        Stopped,
        Error,
    };

    DispatcherApp();
    ~DispatcherApp();

    DispatcherApp(const DispatcherApp&) = delete;
    DispatcherApp& operator=(const DispatcherApp&) = delete;

    /**
     * \brief Initialize logging, parse options, and start the transport server.
     * \param argc Argument count from main()
     * \param argv Argument vector from main()
     * \return 0 on success, non-zero exit code on failure (help/version/error)
     *
     * A non-zero return means the caller should exit with that code.
     */
    int start(int argc, char* argv[]);

    /**
     * \brief Gracefully stop the transport server and all subsystems.
     */
    void stop();

    /**
     * \brief Check if shutdown has been requested (via signal or explicit call).
     */
    bool shutdown_requested() const;

    /**
     * \brief Request shutdown (callable from any thread).
     */
    void request_shutdown();

    /**
     * \brief Submit a task and return an awaitable that suspends until the response arrives.
     * \param task_id Unique task identifier
     * \param request Request payload buffer (ownership transferred)
     * \param response_buffer Optional pre-allocated response buffer
     * \return Awaitable that yields a TaskCompletionSource& on co_await
     */
    TaskSubmitAwaitable submit_task(
        uint32_t task_id,
        std::unique_ptr<TaskMessenger::Skills::PayloadBufferBase> request,
        std::unique_ptr<TaskMessenger::Skills::PayloadBufferBase> response_buffer = nullptr);

    /** \brief Get the current number of tasks in the pool. */
    size_t task_queue_size() const;

    /** \brief Print transport server statistics to the logger. */
    void print_statistics() const;

    /** \brief Get the logger. */
    std::shared_ptr<Logger> logger() const;

    /** \brief Dispatcher uptime in whole seconds. */
    uint64_t uptime_seconds() const;

    /** \brief Get current dispatcher lifecycle state. */
    LifecycleState lifecycle_state() const;

    /** \brief Get lifecycle state as API-safe lowercase string. */
    std::string lifecycle_state_string() const;

private:
    static void install_signal_handlers();

    /** \brief Background loop that retries rendezvous registration until success or shutdown. */
    void registration_loop(std::string rv_host, int rv_port,
                           std::string vn_ip, int listen_port);

    std::shared_ptr<Logger> logger_;
    std::unique_ptr<AsyncTransportServer> server_;
    std::unique_ptr<monitoring::MonitoringService> monitoring_service_;
    std::shared_ptr<rendezvous::RendezvousClient> rendezvous_client_;
    std::shared_ptr<monitoring::SnapshotReporter> snapshot_reporter_;
    std::thread registration_thread_;
    std::atomic<bool> registered_{false};
    std::chrono::steady_clock::time_point start_time_{};
    std::atomic<LifecycleState> lifecycle_state_{LifecycleState::Stopped};

    // Global shutdown flag — static so signal handler can access it
    static std::atomic<bool> s_shutdown_requested;
    static void signal_handler(int signum);
};
