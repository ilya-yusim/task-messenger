/**
 * \file worker/runtime/AsyncRuntime.cpp
 * \brief Coroutine-enabled implementation of the worker runtime strategy.
 */
#include "AsyncRuntime.hpp"
#include "worker/processor/TaskProcessor.hpp"
#include "transport/coro/coroIoContext.hpp"
#include "transport/coro/CoroTask.hpp"
#include "transport/coro/CoroSocketAdapter.hpp"
#include "message/TaskMessage.hpp"
#include "logger.hpp"
#include <thread>
#include <chrono>
#include <utility>

using transport::CoroIoContext;

AsyncRuntime::AsyncRuntime(const std::string& host, int port, std::shared_ptr<Logger> logger)
    : host_(host), port_(port), logger_(std::move(logger)) {}

bool AsyncRuntime::connect() {
    std::error_code ec;
    try {
        std::shared_ptr<transport::CoroSocketAdapter> sock;
        // Prefer reusing existing socket if present; otherwise create and store one
        {
            std::lock_guard<std::mutex> lk(socket_mtx_);
            sock = socket_;
        }
        if (!sock) {
            auto new_socket = transport::CoroSocketAdapter::create_client(logger_);
            if (!new_socket) {
                if (logger_) logger_->error("Failed to create async client socket");
                return false;
            }
            // Store socket BEFORE calling connect so shutdown() can interrupt it
            {
                std::lock_guard<std::mutex> lk(socket_mtx_);
                if (!socket_) {
                    socket_ = new_socket;
                }
                sock = socket_;
            }
        }

        // If reconnecting and fd is still open, close it before connecting again
        if (sock && sock->is_open()) {
            sock->close();
        }

        sock->connect(host_, port_, ec);
        if (ec) {
            if (logger_) logger_->error(std::string{"Failed to connect: "} + ec.message());
            return false;
        }
        return true;
        
    } catch (const std::runtime_error& e) {
        // Shutdown interrupted network join
        if (logger_) logger_->info(std::string{"Connect interrupted: "} + e.what());
        return false;
    } catch (const std::exception& e) {
        if (logger_) logger_->error(std::string{"Connect failed with exception: "} + e.what());
        return false;
    }
}

void AsyncRuntime::disconnect() {
    std::shared_ptr<transport::CoroSocketAdapter> socket_to_close;
    {
        std::lock_guard<std::mutex> lk(socket_mtx_);
        socket_to_close = socket_;  // Keep shared_ptr alive for reconnect
    }
    // Close outside the lock
    if (socket_to_close) {
        socket_to_close->close();
    }
}

void AsyncRuntime::shutdown() {
    std::shared_ptr<transport::CoroSocketAdapter> socket_to_shutdown;
    {
        std::lock_guard<std::mutex> lk(socket_mtx_);
        socket_to_shutdown = socket_;
    }
    // Shutdown outside the lock to interrupt blocking operations and leave network
    if (socket_to_shutdown) {
        socket_to_shutdown->shutdown();
        socket_to_shutdown->close();
    }
}

void AsyncRuntime::release() {
    std::lock_guard<std::mutex> lk(socket_mtx_);
    socket_.reset();  // Destroy socket
}

bool AsyncRuntime::is_connected() const {
    std::lock_guard<std::mutex> lk(socket_mtx_);
    return socket_ && socket_->is_open();
}

std::string AsyncRuntime::get_local_endpoint() const {
    std::lock_guard<std::mutex> lk(socket_mtx_);
    return socket_ ? socket_->local_endpoint() : "";
}

bool AsyncRuntime::run_loop(TaskProcessor& processor) {
    // Get socket for this run
    std::shared_ptr<transport::CoroSocketAdapter> current_socket;
    {
        std::lock_guard<std::mutex> lk(socket_mtx_);
        current_socket = socket_;
    }
    
    if (!current_socket) {
        if (logger_) logger_->error("run_loop: no socket available");
        return false;
    }
    
    // Start coroutine and poll until complete
    auto coro = run_loop_coro(processor);
    while (!coro.done()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Get result - coro returns true on success, false on error
    return coro.get_result();
}

Task<bool> AsyncRuntime::run_loop_coro(TaskProcessor& processor) {
    // Get socket (already validated by run_loop)
    std::shared_ptr<transport::CoroSocketAdapter> current_socket;
    {
        std::lock_guard<std::mutex> lk(socket_mtx_);
        current_socket = socket_;
    }
    
    // Clear any pending pause request from previous state (e.g., multiple pause presses while paused)
    pause_requested_.store(false, std::memory_order_relaxed);
    
    for (;;) {
        if (pause_requested_.load(std::memory_order_relaxed)) {
            if (logger_) logger_->info("Runtime pause requested");
            pause_requested_.store(false, std::memory_order_relaxed);
            co_return true;  // Clean exit
        }
        
        // Read header
        TaskHeader header{};
        try {
            co_await current_socket->async_read(&header, sizeof(header));
            bytes_received_.fetch_add(static_cast<std::uint64_t>(sizeof(header)), std::memory_order_relaxed);
        } catch (const std::exception& e) {
            if (logger_) logger_->error(std::string{"async_read header failed: "} + e.what());
            co_return false;
        }

        // Read body
        std::string payload;
        payload.clear();
        if (header.body_size > 0) {
            if (payload.capacity() < header.body_size) {
                payload.reserve(header.body_size);
            }
            payload.resize(header.body_size);
            try {
                co_await current_socket->async_read(payload.data(), payload.size());
                bytes_received_.fetch_add(static_cast<std::uint64_t>(payload.size()), std::memory_order_relaxed);
            } catch (const std::exception& e) {
                if (logger_) logger_->error(std::string{"async_read body failed: "} + e.what());
                co_return false;
            }
        }

        // Process task
        auto result = processor.process(header.task_id, header.task_type, payload);
        
        TaskMessage response(header.task_id, header.task_type, std::move(result));
        const auto wire_bytes = response.wire_bytes();

        try {
            co_await current_socket->async_write(reinterpret_cast<const char*>(wire_bytes.data()), wire_bytes.size());
            bytes_sent_.fetch_add(static_cast<std::uint64_t>(wire_bytes.size()), std::memory_order_relaxed);
        } catch (const std::exception& e) {
            if (logger_) logger_->error(std::string{"async_write failed: "} + e.what());
            co_return false;
        }
        
        auto new_completed = tasks_completed_.fetch_add(1ULL, std::memory_order_relaxed) + 1ULL;
        if (logger_) {
            if ((new_completed % 10) == 0) {
                logger_->info("Worker: completed " + std::to_string(new_completed) + " tasks");
            }
        }
    }

    co_return true;
}

void AsyncRuntime::pause() {
    pause_requested_.store(true, std::memory_order_relaxed);
}

int AsyncRuntime::get_task_count() const {
    return static_cast<int>(tasks_completed_.load(std::memory_order_relaxed));
}

std::uint64_t AsyncRuntime::get_bytes_sent() const {
    return bytes_sent_.load(std::memory_order_relaxed);
}

std::uint64_t AsyncRuntime::get_bytes_received() const {
    return bytes_received_.load(std::memory_order_relaxed);
}
