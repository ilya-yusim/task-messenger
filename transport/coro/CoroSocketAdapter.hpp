/**
 * \file CoroSocketAdapter.hpp
 * \brief A lightweight wrapper that adds C++20 coroutine awaitable operations to IAsyncStream.
 * \details Wraps an IAsyncStream and provides awaitable read/write operations, resuming 
 * coroutines on event-loop threads with minimal allocation overhead. Does not inherit from
 * IAsyncStream - uses composition to add coroutine capability while delegating base operations.
 */
#pragma once

#include "logger.hpp"
#include <memory>
#include <functional>
#include <system_error>
#include <coroutine>
#include <cstdint>
#include <thread>
#include <chrono>
#include "coroIoContext.hpp"
#include "transport/socket/SocketFactory.hpp"
#include "transport/socket/IAsyncStream.hpp"


namespace transport {

/** \brief Forward declaration for factory to allow factory utilities without full definition. */
class SocketFactory;

/** \defgroup coro_adapter Socket Adapter
 *  \ingroup coro_module
 *  \brief Awaitable socket operations wrapping IAsyncStream.
 */

/**
 * \brief Coroutine-aware wrapper adding awaitable operations to `IAsyncStream`.
 * \details
 * - Wraps an IAsyncStream and adds coroutine awaitable methods
 * - Exposes underlying socket for direct access to base operations
 * - Fast-path: attempts non-blocking `try_*` completion in `await_ready()` to avoid suspension
 * - Slow-path: unfinished operations registered with `CoroIoContext` and resumed when ready
 * - Threading: continuations always resume on an event-loop thread
 * - \invariant At most one in-flight operation per adapter instance
 * \ingroup coro_adapter
 * \see transport::CoroIoContext \see IAsyncStream
 */
class CoroSocketAdapter : public std::enable_shared_from_this<CoroSocketAdapter> {
public:
    /** \brief Internal operation types for coroutine-driven socket operations. */
    enum class OperationType { NONE, READ, READ_HEADER, WRITE };

    // Constructors
    explicit CoroSocketAdapter(std::shared_ptr<IAsyncStream> socket)
        : socket_(std::move(socket)), logger_(nullptr) {}

    CoroSocketAdapter(std::shared_ptr<IAsyncStream> socket, std::shared_ptr<Logger> logger)
        : socket_(std::move(socket)), logger_(std::move(logger)) {}

    CoroSocketAdapter(std::shared_ptr<IAsyncStream> socket, std::shared_ptr<Logger> logger, std::shared_ptr<CoroIoContext> ctx)
        : socket_(std::move(socket)), logger_(std::move(logger)), context_(std::move(ctx)) {}

    // Factories
    /** \brief Create a client adapter using the `SocketFactory` backend. */
    static std::shared_ptr<CoroSocketAdapter> create_client(std::shared_ptr<Logger> logger, std::shared_ptr<CoroIoContext> ctx = nullptr) {
        auto stream = SocketFactory::create_async_client(logger);
        return std::make_shared<CoroSocketAdapter>(stream, logger, ctx);
    }
    /** \brief Create a server adapter using the `SocketFactory` backend (requires subsequent `start_listening`). */
    static std::shared_ptr<CoroSocketAdapter> create_server(std::shared_ptr<Logger> logger, std::shared_ptr<CoroIoContext> ctx = nullptr) {
        auto stream = SocketFactory::create_async_server(logger);
        return std::make_shared<CoroSocketAdapter>(stream, logger, ctx);
    }

    // Access to underlying socket for base operations
    /** \brief Get the underlying socket pointer for direct access to base operations. */
    IAsyncStream* socket() { return socket_.get(); }
    const IAsyncStream* socket() const { return socket_.get(); }
    std::shared_ptr<IAsyncStream> socket_ptr() { return socket_; }

    // Convenience forwarding for common operations
    /** \brief Connect to a remote host/port. */
    void connect(const std::string &host, int port, std::error_code& error);
    /** \brief Start listening socket (bind + listen). */
    bool start_listening(const std::string& host, int port, int backlog = 128) {
        if (!socket_) return false;
        return socket_->start_listening(host, port, backlog);
    }
    /** \brief Close the socket. */
    void close() { if (socket_) socket_->close(); }
    /** \brief Request shutdown - interrupts blocking operations in underlying socket. */
    void shutdown() { if (socket_) socket_->shutdown(); }
    /** \brief Check if socket is open. */
    bool is_open() const { return socket_ && socket_->is_open(); }
    /** \brief Get remote endpoint. */
    std::string remote_endpoint() const { return socket_ ? socket_->remote_endpoint() : ""; }
    /** \brief Get local endpoint. */
    std::string local_endpoint() const { return socket_ ? socket_->local_endpoint() : ""; }

    /** \brief Timed blocking accept returning wrapped client adapter. */
    std::shared_ptr<CoroSocketAdapter> blocking_accept(std::error_code& error,
                                                       std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) {
        error.clear();
        if (!socket_) {
            error = std::make_error_code(std::errc::bad_file_descriptor);
            return nullptr;
        }
        auto client_stream = socket_->blocking_accept(error, timeout);
        if (!client_stream) {
            return nullptr;
        }
        return std::make_shared<CoroSocketAdapter>(client_stream, logger_, context_);
    }

    // Async IO operations (coroutine awaitable extensions)
    /** \brief Asynchronously read into `buffer` up to `size` bytes.
     *  \see transport::CoroIoContext::register_pending
     *  \see try_complete_current_operation
     */
    auto async_read(void* buffer, size_t size) {
        struct ReadAwaitable {
            std::shared_ptr<CoroSocketAdapter> socket;
            void* buffer;
            size_t size;
            bool await_ready() const noexcept {
                socket->prepare_read_operation(buffer, size);
                socket->current_operation_ = OperationType::READ;
                return socket->try_complete_current_operation();
            }
            void await_suspend(std::coroutine_handle<> handle) noexcept {
                socket->suspend_for_operation(handle, OperationType::READ);
                if (!socket->context_) socket->context_ = transport::default_loop();
                socket->context_->register_pending(CoroIoContext::PendingOpCategory::Read, [s = socket]() {
                    return s->try_complete_current_operation();
                }, handle);
            }
            size_t await_resume() {
                if (socket->last_error_) {
                    throw std::system_error(socket->last_error_, "Async read operation failed");
                }
                return socket->last_bytes_transferred_;
            }
        };
        return ReadAwaitable{shared_from_this(), buffer, size};
    }
    /** \brief Asynchronously read a protocol header (e.g. task/task-response header) into `buffer`.
     *  \details Separate from `async_read()` to:
     *  1. Attribute attempt histograms to `PendingOpCategory::ReadHeader` (distinct from bulk body reads).
     *  2. Allow future header-specific policies (timeouts, validation) without touching generic reads.
     *  3. Provide clearer call-site intent in protocol code.
     *  Prefer this for fixed-size framing structures; use `async_read()` for variable-size payloads.
     *  \see transport::CoroIoContext::register_pending
     *  \see try_complete_current_operation
     */
    auto async_read_header(void* buffer, size_t size) {
        struct ReadHeaderAwaitable {
            std::shared_ptr<CoroSocketAdapter> socket;
            void* buffer;
            size_t size;
            bool await_ready() const noexcept {
                socket->prepare_read_operation(buffer, size);
                socket->current_operation_ = OperationType::READ_HEADER;
                return socket->try_complete_current_operation();
            }
            void await_suspend(std::coroutine_handle<> handle) noexcept {
                socket->suspend_for_operation(handle, OperationType::READ_HEADER);
                if (!socket->context_) socket->context_ = transport::default_loop();
                socket->context_->register_pending(CoroIoContext::PendingOpCategory::ReadHeader, [s = socket]() {
                    return s->try_complete_current_operation();
                }, handle);
            }
            size_t await_resume() {
                if (socket->last_error_) {
                    throw std::system_error(socket->last_error_, "Async read header operation failed");
                }
                return socket->last_bytes_transferred_;
            }
        };
        return ReadHeaderAwaitable{shared_from_this(), buffer, size};
    }
    /** \brief Asynchronously write `size` bytes from `buffer`.
     *  \see transport::CoroIoContext::register_pending
     *  \see try_complete_current_operation
     */
    auto async_write(const void* buffer, size_t size) {
        struct WriteAwaitable {
            std::shared_ptr<CoroSocketAdapter> socket;
            const void* buffer;
            size_t size;
            bool await_ready() const noexcept {
                socket->prepare_write_operation(buffer, size);
                socket->current_operation_ = OperationType::WRITE;
                return socket->try_complete_current_operation();
            }
            void await_suspend(std::coroutine_handle<> handle) noexcept {
                socket->suspend_for_operation(handle, OperationType::WRITE);
                if (!socket->context_) socket->context_ = transport::default_loop();
                socket->context_->register_pending(CoroIoContext::PendingOpCategory::Write, [s = socket]() {
                    return s->try_complete_current_operation();
                }, handle);
            }
            size_t await_resume() {
                if (socket->last_error_) {
                    throw std::system_error(socket->last_error_, "Async write operation failed");
                }
                return socket->last_bytes_transferred_;
            }
        };
        return WriteAwaitable{shared_from_this(), buffer, size};
    }
    /** \brief Convenience template: write POD object by value. */
    template<typename T>
    auto async_write(const T& data) { return async_write(&data, sizeof(T)); }
    /** \brief Convenience template: read POD object by value. */
    template<typename T>
    auto async_read(T& data) { return async_read(&data, sizeof(T)); }

    // Metrics / introspection
    /** \brief Retrieve last error set by backend `try_*` calls. */
    std::error_code get_last_error() const { return last_error_; }
    /** \brief Retrieve last bytes transferred set by backend `try_*` calls. */
    size_t get_last_bytes_transferred() const { return last_bytes_transferred_; }
    /** \brief Access the currently suspended coroutine handle (if any). */
    std::coroutine_handle<> get_suspended_coroutine() const { return suspended_handle_; }
    /** \brief Access the underlying async stream pointer (non-owning). */
    IAsyncStream* get_underlying_socket() const { return socket_.get(); }

    // Internal helpers (remain public for awaitable local struct access)
    /** \brief Store the awaiting coroutine and record the current operation type. */
    void suspend_for_operation(std::coroutine_handle<> handle, OperationType op) {
        suspended_handle_ = handle;
        current_operation_ = op;
    }
    /** \brief Attempt to advance/complete the active operation; returns true when finished (success or error). */
    bool try_complete_current_operation() {
        switch (current_operation_) {
            case OperationType::READ:
            case OperationType::READ_HEADER: {
                if (!socket_) {
                    last_error_ = std::make_error_code(std::errc::bad_file_descriptor);
                    return true;
                }
                bool completed = socket_->try_read(read_buffer_, read_size_, last_bytes_transferred_, last_error_);
                if (completed) current_operation_ = OperationType::NONE;
                return completed;
            }
            case OperationType::WRITE: {
                if (!socket_) {
                    last_error_ = std::make_error_code(std::errc::bad_file_descriptor);
                    return true;
                }
                bool completed = socket_->try_write(write_buffer_, write_size_, last_bytes_transferred_, last_error_);
                if (completed) current_operation_ = OperationType::NONE;
                return completed;
            }
            case OperationType::NONE:
            default:
                return true;
        }
    }
    /** \brief Prepare a read operation using the provided buffer reference. */
    void prepare_read_operation(void* buffer, size_t size) { read_buffer_ = buffer; read_size_ = size; }
    /** \brief Prepare a write operation using the provided buffer reference. */
    void prepare_write_operation(const void* buffer, size_t size) { write_buffer_ = buffer; write_size_ = size; }

private:
    /** \brief Underlying asynchronous stream implementation (transport-specific). */
    std::shared_ptr<IAsyncStream> socket_;
    /** \brief Logger instance shared with accepted child sockets. */
    std::shared_ptr<Logger> logger_;
    /** \brief Associated coroutine I/O context (event loop); defaults to `transport::default_loop()` when first used. */
    std::shared_ptr<CoroIoContext> context_{};
    /** \brief Handle of the currently suspended coroutine awaiting I/O completion. */
    std::coroutine_handle<> suspended_handle_;
    /** \brief Active operation type; `NONE` when idle. */
    OperationType current_operation_ = OperationType::NONE;
    /** \brief Last error reported by backend `try_*` call. */
    std::error_code last_error_;
    /** \brief Last byte count reported by backend `try_*` call. */
    size_t last_bytes_transferred_ = 0;
    /** \brief Buffers describing the currently prepared read/write operation. */
    void* read_buffer_ = nullptr;
    size_t read_size_ = 0;
    const void* write_buffer_ = nullptr;
    size_t write_size_ = 0;
};

} // namespace transport
