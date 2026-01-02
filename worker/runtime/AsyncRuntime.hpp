/**
 * \file worker/runtime/AsyncRuntime.hpp
 * \brief Coroutine-based implementation of \c IRuntimeMode.
 */
#pragma once

#include "IRuntimeMode.hpp"
#include "logger.hpp"
#include "transport/coro/CoroTask.hpp"
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <cstdint>

class Logger;
namespace transport { class CoroSocketAdapter; }
class TaskProcessor;

/** \brief Runtime implementation backed by coroutine-enabled transport primitives. */
class AsyncRuntime : public IRuntimeMode {
public:
    /**
     * \brief Create an async runtime bound to a target host and port.
     * \param host Manager host to connect to.
     * \param port Manager port to connect to.
     * \param logger Logger shared across worker components.
     */
    AsyncRuntime(const std::string& host, int port, std::shared_ptr<Logger> logger);

    // IRuntimeMode interface
    /** \copydoc IRuntimeMode::connect */
    bool connect() override;
    /** \copydoc IRuntimeMode::disconnect */
    void disconnect() override;
    /** \copydoc IRuntimeMode::release */
    void release() override;
    /** \copydoc IRuntimeMode::shutdown */
    void shutdown() override;
    /** \copydoc IRuntimeMode::is_connected */
    bool is_connected() const override;
    /** \copydoc IRuntimeMode::get_local_endpoint */
    std::string get_local_endpoint() const override;
    /** \copydoc IRuntimeMode::run_loop */
    bool run_loop(TaskProcessor& processor) override;
    /** \copydoc IRuntimeMode::pause */
    void pause() override;
    /** \copydoc IRuntimeMode::get_task_count */
    int get_task_count() const override;
    /** \copydoc IRuntimeMode::get_bytes_sent */
    std::uint64_t get_bytes_sent() const override;
    /** \copydoc IRuntimeMode::get_bytes_received */
    std::uint64_t get_bytes_received() const override;

private:
    /** \brief Internal coroutine that drives the async loop. */
    Task<bool> run_loop_coro(TaskProcessor& processor);

    std::string host_;
    int port_;
    std::shared_ptr<Logger> logger_;

    std::shared_ptr<transport::CoroSocketAdapter> socket_;
    mutable std::mutex socket_mtx_;

    std::atomic<std::uint64_t> tasks_completed_{0};
    std::atomic<std::uint64_t> bytes_sent_{0};
    std::atomic<std::uint64_t> bytes_received_{0};

    std::atomic<bool> pause_requested_{false};
};
