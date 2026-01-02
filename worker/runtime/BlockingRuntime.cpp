/**
 * \file worker/runtime/BlockingRuntime.cpp
 * \brief Blocking \c IRuntimeMode implementation backed by \c IBlockingStream.
 */
#include "BlockingRuntime.hpp"
#include "worker/processor/TaskProcessor.hpp"
#include "transport/socket/IBlockingStream.hpp"
#include "transport/socket/SocketFactory.hpp"
#include "message/TaskMessage.hpp"
#include "logger.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace {

bool read_task(IBlockingStream& s, TaskHeader& header, std::string& payload, std::error_code& ec,
               std::uint64_t& bytes_read,
               size_t max_frame_size = 16 * 1024 * 1024) {
    payload.clear();
    ec.clear();
    bytes_read = 0;

    // Read header
    size_t br = 0;
    s.read(&header, sizeof(TaskHeader), br, ec);
    if (ec) throw std::system_error(ec, "read_task: failed to read header");
    if (br != sizeof(TaskHeader))
        throw std::system_error(std::make_error_code(std::errc::protocol_error), "read_task: short header read");
    bytes_read += static_cast<std::uint64_t>(sizeof(TaskHeader));

    if (header.body_size > max_frame_size)
        throw std::system_error(std::make_error_code(std::errc::message_size),
                                "read_task: body_size exceeds max_frame_size");

    if (header.body_size > 0) {
        payload.resize(header.body_size);
        br = 0;
        s.read(payload.data(), payload.size(), br, ec);
        if (ec) throw std::system_error(ec, "read_task: failed to read body");
        if (br != payload.size())
            throw std::system_error(std::make_error_code(std::errc::protocol_error), "read_task: short body read");
        bytes_read += static_cast<std::uint64_t>(payload.size());
    }
    return true;
}

/// Single call blocking write for a response; throws on error or short write.
bool write_response(IBlockingStream& s, uint32_t task_id, uint32_t task_type, std::string_view payload,
                    std::error_code& ec, std::uint64_t& bytes_written) {
    ec.clear();
    TaskMessage response(task_id, task_type, std::string(payload));
    const auto buffer = response.wire_bytes();

    size_t bw = 0;
    s.write(buffer.data(), buffer.size(), bw, ec);
    if (ec) 
        throw std::system_error(ec, "write_response: failed to write combined frame");
    if (bw != buffer.size())
        throw std::system_error(std::make_error_code(std::errc::protocol_error), "write_response: short combined write");

    bytes_written = static_cast<std::uint64_t>(buffer.size());
    return true;
}
} // namespace

BlockingRuntime::BlockingRuntime(const std::string& host, int port, std::shared_ptr<Logger> logger)
    : host_(host), port_(port), logger_(std::move(logger)) {}

bool BlockingRuntime::connect() {
    std::error_code ec;
    try {
        std::shared_ptr<IBlockingStream> sock;
        // Prefer reusing existing socket if present; otherwise create and store one
        {
            std::lock_guard<std::mutex> lk(socket_mtx_);
            sock = socket_;
        }
        if (!sock) {
            auto new_socket = ::transport::SocketFactory::create_blocking_client(logger_);
            if (!new_socket) {
                if (logger_) logger_->error("Failed to create blocking client socket");
                return false;
            }
            // Store socket BEFORE calling connect so shutdown() can interrupt it
            {
                std::lock_guard<std::mutex> lk(socket_mtx_);
                // Double-check in case another thread set it
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

void BlockingRuntime::disconnect() {
    std::shared_ptr<IBlockingStream> socket_to_close;
    {
        std::lock_guard<std::mutex> lk(socket_mtx_);
        socket_to_close = socket_;  // Keep shared_ptr alive for reconnect
    }
    // Close outside the lock to avoid holding mutex during potentially blocking operation
    if (socket_to_close) {
        socket_to_close->close();
    }
}

void BlockingRuntime::shutdown() {
    std::shared_ptr<IBlockingStream> socket_to_shutdown;
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

void BlockingRuntime::release() {
    std::lock_guard<std::mutex> lk(socket_mtx_);
    socket_.reset();  // Destroy socket, releasing ZeroTier network lease
}

bool BlockingRuntime::is_connected() const {
    std::lock_guard<std::mutex> lk(socket_mtx_);
    return socket_ && socket_->is_open();
}

std::string BlockingRuntime::get_local_endpoint() const {
    std::lock_guard<std::mutex> lk(socket_mtx_);
    return socket_ ? socket_->local_endpoint() : "";
}

bool BlockingRuntime::run_loop(TaskProcessor& processor) {
    // Get socket for this run
    std::shared_ptr<IBlockingStream> current_socket;
    {
        std::lock_guard<std::mutex> lk(socket_mtx_);
        current_socket = socket_;
    }
    
    if (!current_socket) {
        if (logger_) logger_->error("run_loop: no socket available");
        return false;
    }
    
    // Clear any pending pause request from previous state (e.g., multiple pause presses while paused)
    pause_requested_.store(false, std::memory_order_relaxed);
    
    for (;;) {
        if (pause_requested_.load(std::memory_order_relaxed)) {
            if (logger_) logger_->info("Runtime pause requested");
            pause_requested_.store(false, std::memory_order_relaxed);
            break;
        }
        
        TaskHeader header{};
        std::string payload;
        std::error_code ec;
        std::uint64_t frame_bytes_read = 0;
        
        try {
            if (!read_task(*current_socket, header, payload, ec, frame_bytes_read)) {
                if (logger_) logger_->error("read_task failed: " + ec.message());
                return false;
            }
        } catch (const std::system_error& se) {
            if (logger_) logger_->error("read_task exception: " + std::string(se.what()));
            return false;
        }
        bytes_received_.fetch_add(frame_bytes_read, std::memory_order_relaxed);
        
        auto result = processor.process(header.task_id, header.task_type, payload);
        ec.clear();
        std::uint64_t frame_bytes_written = 0;
        
        try {
            if (!write_response(*current_socket, header.task_id, header.task_type, result, ec, frame_bytes_written)) {
                if (logger_) logger_->error("write_response failed: " + ec.message());
                return false;
            }
        } catch (const std::system_error& se) {
            if (logger_) logger_->error("write_response exception: " + std::string(se.what()));
            return false;
        }
        bytes_sent_.fetch_add(frame_bytes_written, std::memory_order_relaxed);
        
        auto new_completed = tasks_completed_.fetch_add(1ULL, std::memory_order_relaxed) + 1ULL;
        if ((new_completed % 10) == 0 && logger_) {
            logger_->info("Worker: completed " + std::to_string(new_completed) + " tasks");
        }
    }
    
    return true;
}

void BlockingRuntime::pause() {
    pause_requested_.store(true, std::memory_order_relaxed);
}

int BlockingRuntime::get_task_count() const {
    return static_cast<int>(tasks_completed_.load(std::memory_order_relaxed));
}

std::uint64_t BlockingRuntime::get_bytes_sent() const {
    return bytes_sent_.load(std::memory_order_relaxed);
}

std::uint64_t BlockingRuntime::get_bytes_received() const {
    return bytes_received_.load(std::memory_order_relaxed);
}
