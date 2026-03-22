// ManagerApp.hpp - Application harness for manager infrastructure startup
#pragma once

#include "transport/AsyncTransportServer.hpp"
#include "message/TaskSubmitAwaitable.hpp"
#include "logger.hpp"

#include <atomic>
#include <cstddef>
#include <memory>

/**
 * \brief Application harness that encapsulates common manager startup.
 *
 * Handles logger setup, CLI/JSON option parsing, AsyncTransportServer lifecycle,
 * and signal handler installation. Generators create a ManagerApp, call start(),
 * run their own loop using submit_task(), then call stop() for clean shutdown.
 */
class ManagerApp {
public:
    ManagerApp();
    ~ManagerApp();

    ManagerApp(const ManagerApp&) = delete;
    ManagerApp& operator=(const ManagerApp&) = delete;

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
    size_t task_pool_size() const;

    /** \brief Print transport server statistics to the logger. */
    void print_statistics() const;

    /** \brief Get the logger. */
    std::shared_ptr<Logger> logger() const;

private:
    static void install_signal_handlers();

    std::shared_ptr<Logger> logger_;
    std::unique_ptr<AsyncTransportServer> server_;

    // Global shutdown flag — static so signal handler can access it
    static std::atomic<bool> s_shutdown_requested;
    static void signal_handler(int signum);
};
