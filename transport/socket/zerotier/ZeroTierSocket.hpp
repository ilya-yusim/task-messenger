/**
 * \file ZeroTierSocket.hpp
 * \brief ZeroTier implementation of IAsyncStream and IBlockingStream.
 * \ingroup socket_backend
 * \details Stream socket backed by libzt, providing both non-blocking and blocking
 *  operations and cooperating with the shared \ref transport::ZeroTierNodeService.
 */
#pragma once

#define ADD_EXPORTS // This is required for ZeroTierSocket.h to export symbols from the libzt library
#include <ZeroTierSockets.h>
#include "ZeroTierNodeService.hpp" // Shared node + network lease
#include "transport/socket/IAsyncStream.hpp"
#include "transport/socket/IBlockingStream.hpp"
#include <memory>
#include <string>
#include <system_error>
#include <functional>
#include <atomic>
#include <chrono>
#include <mutex>


// Forward declare Logger to avoid pulling in logger header here
class Logger;

/** \brief ZeroTier-backed stream implementing async and blocking roles.
 *  \ingroup socket_backend
 *  \details Provides non-blocking and blocking operations using libzt. Cooperates
 *  with a process-wide ZeroTier node via \ref transport::ZeroTierNodeService.
 */
class ZeroTierSocket : public virtual IAsyncStream, public virtual IBlockingStream {
public:
    // Distinguish intended operating mode for the lifetime of this socket
    enum class SocketMode { NonBlocking, Blocking };
    // === Construction & Lifecycle ===
    /** \brief Constructor for client sockets (default NonBlocking mode). */
    ZeroTierSocket();

    /** \brief Constructor selecting explicit socket mode. */
    explicit ZeroTierSocket(SocketMode mode);

    /** \brief Constructor for client sockets with logger injection.
     *  \param logger Shared logger instance for diagnostics (optional).
     */
    explicit ZeroTierSocket(std::shared_ptr<Logger> logger);

    /** \brief Constructor with explicit mode and logger. */
    ZeroTierSocket(SocketMode mode, std::shared_ptr<Logger> logger);

    /** \brief Wrap an existing socket file descriptor (e.g., from accept).
     *  \param existing_fd Already-created socket fd/handle.
     */
    explicit ZeroTierSocket(int existing_fd);

    /** \brief Wrap an existing socket with logger injection.
     *  \param existing_fd Already-created socket fd/handle.
     *  \param logger Shared logger instance for diagnostics (optional).
     */
    ZeroTierSocket(int existing_fd, std::shared_ptr<Logger> logger);

    /** \brief Destructor. */
    ~ZeroTierSocket() override;

    // === Starting a server ===
    /** \brief Convenience: bind and listen in one call.
     *  \param host Host/interface to bind (e.g. "0.0.0.0").
     *  \param port Port number to bind to.
     *  \param backlog Listen backlog.
     *  \return true on success; false if bind/listen fails (errors are logged).
     */
    bool start_listening(const std::string& host, int port, int backlog = 128) override;

    /** \brief Attempt to accept a new connection non-blockingly.
     *  \param error Receives non-transient errors; cleared on success or would-block.  
     *  \return New stream on success; nullptr if no pending client.
     */
    std::shared_ptr<IAsyncStream> try_accept(std::error_code& error) override;

    // === Starting a client ===
    /** \brief Attempt a non-blocking connection to host:port. (Not used since blocking connect with timeout is more efficient.)
     *  \param host Hostname or IP address.
     *  \param port Port number.
     *  \param error Receives error on immediate failure; cleared otherwise.
     *  \return true if connect completed; false if still in progress.
     */
    bool try_connect(const std::string& host, int port, std::error_code& error);

    /** \brief Check if a pending connection has completed. (Not used since blocking connect with timeout is more efficient.)
     *  \param error Receives completion error if any; cleared on success or pending.
     *  \return true if completed; false if still pending.
     */
    bool check_connect_complete(std::error_code& error);

    // === I/O operations ===
    /** \brief Attempt a non-blocking read.
     *  \param buffer Destination buffer.
     *  \param size Maximum bytes to read.
     *  \param bytes_read Out: bytes read.
     *  \param error Receives error on completion; cleared on success or would-block.
     *  \return true if completed (success or error); false if would block.
     */
    bool try_read(void* buffer, size_t size, size_t& bytes_read, std::error_code& error) override;

    /** \brief Attempt a non-blocking write.
     *  \param buffer Source buffer.
     *  \param size Bytes to write.
     *  \param bytes_written Out: bytes written.
     *  \param error Receives error on completion; cleared on success or would-block.
     *  \return true if completed (success or error); false if would block.
     */
    bool try_write(const void* buffer, size_t size, size_t& bytes_written, std::error_code& error) override;

    // === Blocking I/O operations (IBlockingStream) ===
    /** \brief Perform blocking connect operation.
     *  \param host Hostname or IP address.
     *  \param port Port number.
     *  \param error Receives error on failure.
     */
    void connect(const std::string& host, int port, std::error_code& error) override;

    /** \brief Perform blocking read operation.
     *  \param buffer Destination buffer.
     *  \param size Maximum bytes to read.
     *  \param bytes_read Out: bytes read.
     *  \param error Receives error on failure.
     */
    void read(void* buffer, size_t size, size_t& bytes_read, std::error_code& error) override;

    /** \brief Perform blocking write operation.
     *  \param buffer Source buffer.
     *  \param size Bytes to write.
     *  \param bytes_written Out: bytes written.
     *  \param error Receives error on failure.
     */
    void write(const void* buffer, size_t size, size_t& bytes_written, std::error_code& error) override;

    // === Closing / teardown ===
    /** \brief Close the socket and clean up resources. */
    void close() override;
    
    /** \brief Request shutdown - interrupts any blocking operations.
     *  \details Sets an atomic flag that causes blocking operations (read, connect, accept)
     *  to exit their wait loops and return with bad_file_descriptor error.
     */
    void shutdown() override;

    // === Status & information ===
    /** \brief Check if the socket is currently open and usable.
     *  \return true if socket is open; false otherwise.
     */
    bool is_open() const override;

    /** \brief Get the file descriptor or handle for the socket.
     *  \return Underlying socket fd/handle.
     */
    int get_handle() const override;

    /** \brief Get a string representation of the remote endpoint.
     *  \return "ip:port" or empty if not connected.
     */
    std::string remote_endpoint() const override;

    /** \brief Get a string representation of the local endpoint.
     *  \return "ip:port" or empty if not bound.
     */
    std::string local_endpoint() const override;

    /** \brief Identify the socket implementation type.
     *  \return Implementation tag string ("zerotier").
     */
    std::string socket_type() const;

    /** \brief Enable or disable TCP_NODELAY (Nagle's algorithm).
     *  \\param enable true to disable Nagle (send immediately), false to enable Nagle.
     *  \\return true on success, false on failure.
     *  \\note Call after connection is established. Disabling Nagle is useful for
     *         low-latency messaging, especially with scatter-gather sends.
     */
    bool set_no_delay(bool enable);

    // === Factory helpers ===
    // Create a new client socket
    static std::shared_ptr<ZeroTierSocket> create();
    // Create a new client socket with injected logger
    static std::shared_ptr<ZeroTierSocket> create(std::shared_ptr<Logger> logger);
    // Create a new client socket in Blocking mode
    static std::shared_ptr<ZeroTierSocket> create_blocking(std::shared_ptr<Logger> logger = nullptr);
    // Wrap existing fd
    static std::shared_ptr<ZeroTierSocket> from_fd(int fd);
    // Wrap existing fd with logger
    static std::shared_ptr<ZeroTierSocket> from_fd(int fd, std::shared_ptr<Logger> logger);

    /** \brief Timed blocking accept for dedicated acceptor threads.
     *  \details Design choice: rather than a manual loop around \ref try_accept with sleeps
     *  (which trades latency vs CPU), we rely on libzt/lwIP blocking accept with a receive
     *  timeout (SO_RCVTIMEO equivalent). This moves efficient sleeping and wakeups into the
     *  TCP/IP stack where it is already optimized and well-tested. A finite timeout lets the
     *  thread wake periodically to observe shutdown without fragile self-wake mechanisms.
     *  Semantics:
     *  - On successful accept: returns a new non-blocking stream (error cleared).
     *  - On timeout or transient conditions (EAGAIN/EWOULDBLOCK/ETIMEDOUT/ECONNABORTED/ESHUTDOWN/EBADF):
     *    returns nullptr with error cleared so caller can continue or exit.
     *  - On non-transient error: returns nullptr with error set.
     *  \param error Receives non-transient errors; cleared on success or transient wake.
     *  \param timeout Maximum blocking interval before returning nullptr (default 500ms).
     */
    std::shared_ptr<IAsyncStream> blocking_accept(std::error_code& error,
                                                  std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) override;

private:
    // === Helper methods ===
    /** \brief Setup a socket with non-blocking and other options.
     *  \param fd Socket file descriptor to configure.
     */
    void setup_socket(int fd);

    // Internal non-virtual cleanup used by destructor to avoid calling virtual close()
    void cleanup_resources_no_virtual();

    /** \brief Set socket to non-blocking mode.
     *  \param non_blocking true for non-blocking; false for blocking.
     */
    void set_non_blocking(bool non_blocking);

    /** \brief Check if an error code indicates a would-block condition.
     *  \param error_code Error code to check.
     *  \return true if would block; false otherwise.
     */
    bool is_would_block_error(int error_code) const;

    /** \brief Translate a ZeroTier errno to std::error_code.
     *  \param zts_error ZeroTier errno value.
     *  \return Corresponding std::error_code.
     */
    std::error_code translate_error(int zts_error) const;

    /** \brief Convert ZeroTier errno to std::error_code (legacy method).
     *  \param zt_errno ZeroTier errno value.
     *  \return Corresponding std::error_code.
     */
    std::error_code make_error_code(int zt_errno) const;

    /** \brief Update endpoint strings based on current socket state. */
    void update_endpoints();

    // Keep bind/listen private - only start_listening is exposed publicly
    bool bind(const std::string& host, int port);
    bool listen(int backlog = 128);

    /** \brief Lazily open the underlying fd if not already open.
     *  \details Requires the ZeroTier node to be started and network lease acquired.
     *  Call after ensure_lease(). Returns true on success, false on failure.
     */
    bool open_fd_if_needed(std::error_code* error = nullptr);

    // === Private state ===
    int socket_fd_{};
    std::mutex socket_mtx_;  // Protects socket_fd_ access
    std::atomic<bool> is_open_{};
    std::atomic<bool> is_connected_{};
    std::string local_endpoint_{};
    std::string remote_endpoint_{};

    // Desired operating mode for this socket (applied once in setup_socket)
    bool non_blocking_mode_ { true };

    // Keep the ZeroTier node running and the default network joined
    transport::ZeroTierNodeService::NetworkLease lease_{};

    // Ensure we have a network lease (joins default network on first use)
    void ensure_lease() {
        if (!lease_.valid()) {
            lease_ = transport::ZeroTierNodeService::instance().acquire_default();
        }
    }

    // Additional state tracking
    bool is_server_socket_{};
    bool connect_in_progress_{};
    int bind_port_{};
    std::string bind_host_{};
    // Indicates close() is in progress or completed to help accept loops avoid racing
    std::atomic<bool> closing_{false};
    // Indicates shutdown requested - interrupts blocking operations
    std::atomic<bool> shutdown_requested_{false};

    // Optional logger (can be injected/set externally to trace socket operations)
    std::shared_ptr<Logger> logger_{};
};