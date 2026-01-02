/**
 * \file worker/runtime/BlockingRuntime.hpp
 * \brief Blocking implementation of \c IRuntimeMode.
 */
#pragma once

#include "IRuntimeMode.hpp"
#include "logger.hpp"
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <cstdint>

class Logger;
struct IBlockingStream;
class TaskProcessor;

/** \brief Runtime implementation that uses blocking socket operations. */
class BlockingRuntime : public IRuntimeMode {
public:
    /**
     * \brief Create a blocking runtime bound to a target host and port.
     * \param host Manager host to connect to.
     * \param port Manager port to connect to.
     * \param logger Logger shared across worker components.
     */
    BlockingRuntime(const std::string& host, int port, std::shared_ptr<Logger> logger);

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
    std::string host_;
    int port_;
    std::shared_ptr<Logger> logger_;

    std::shared_ptr<IBlockingStream> socket_;
    mutable std::mutex socket_mtx_;

    std::atomic<std::uint64_t> tasks_completed_{0};
    std::atomic<std::uint64_t> bytes_sent_{0};
    std::atomic<std::uint64_t> bytes_received_{0};

    std::atomic<bool> pause_requested_{false};
};
