/**
 * \file ZeroTierSocket.cpp
 * \brief Implementation of ZeroTier-backed stream socket.
 * \ingroup socket_backend
 */
#include "ZeroTierSocket.hpp"
#include "ZeroTierErrnoCompat.hpp"
#include "logger.hpp"
#include <stdexcept>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
//     // On Windows, lwIP ERR_WOULDBLOCK (-7) maps to WSAEWOULDBLOCK (10035) 
//     // but ZeroTier reports it as errno 140 through err_to_errno conversion
//     #define ZT_ERRNO_WOULDBLOCK 140
#else
    #include <poll.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
//     // On Unix systems, this would typically be EAGAIN (11) or EWOULDBLOCK
//     #define ZT_ERRNO_WOULDBLOCK EWOULDBLOCK
#endif

// Thread-local errno accessor provided by libzt-wrapper (copied into vendor tree)
#if __has_include("zts_errno.hpp")
#include "zts_errno.hpp"
#elif __has_include("zts_errno.h")
#include "zts_errno.h" // fallback if naming differs
#endif

#// ZeroTierSockets.h is included via ZeroTierSocket.hpp which provides a guarded include/fallback

// === ZeroTierSocket Implementation ===

ZeroTierSocket::ZeroTierSocket() 
        : socket_fd_(-1), is_open_(false), is_connected_(false), 
            is_server_socket_(false), connect_in_progress_(false), bind_port_(0) {
    // Constructor should be lightweight; defer fd creation and network setup to connect()/bind()
}

ZeroTierSocket::ZeroTierSocket(SocketMode mode)
        : socket_fd_(-1), is_open_(false), is_connected_(false),
          is_server_socket_(false), connect_in_progress_(false), bind_port_(0) {
    // Configure desired mode
    non_blocking_mode_ = (mode == SocketMode::NonBlocking);
    // Constructor should be lightweight; defer fd creation and network setup to connect()/bind()
}

ZeroTierSocket::ZeroTierSocket(std::shared_ptr<Logger> logger)
        : socket_fd_(-1), is_open_(false), is_connected_(false),
            is_server_socket_(false), connect_in_progress_(false), bind_port_(0), logger_(std::move(logger)) {
            // Constructor should be lightweight; defer fd creation and network setup to connect()/bind()
}

ZeroTierSocket::ZeroTierSocket(SocketMode mode, std::shared_ptr<Logger> logger)
        : socket_fd_(-1), is_open_(false), is_connected_(false),
          is_server_socket_(false), connect_in_progress_(false), bind_port_(0), logger_(std::move(logger)) {
        non_blocking_mode_ = (mode == SocketMode::NonBlocking);
    // Constructor should be lightweight; defer fd creation and network setup to connect()/bind()
}

ZeroTierSocket::ZeroTierSocket(int existing_fd) 
        : socket_fd_(existing_fd), is_open_(existing_fd >= 0), is_connected_(true),
            is_server_socket_(false), connect_in_progress_(false), bind_port_(0) {
    
    if (socket_fd_ < 0) {
        throw std::runtime_error("Invalid socket file descriptor");
    }
    // Constructor should be lightweight; do not perform network setup here
    // Existing fd already open; apply socket options
    setup_socket(socket_fd_);
}

ZeroTierSocket::ZeroTierSocket(int existing_fd, std::shared_ptr<Logger> logger)
        : socket_fd_(existing_fd), is_open_(existing_fd >= 0), is_connected_(true),
            is_server_socket_(false), connect_in_progress_(false), bind_port_(0), logger_(std::move(logger)) {
        if (socket_fd_ < 0) {
                throw std::runtime_error("Invalid socket file descriptor");
        }
    // Constructor should be lightweight; do not perform network setup here
        setup_socket(socket_fd_);
}

ZeroTierSocket::~ZeroTierSocket() {
    // Avoid virtual dispatch; perform direct cleanup
    cleanup_resources_no_virtual();
}

void ZeroTierSocket::cleanup_resources_no_virtual() {
    if (socket_fd_ >= 0) {
        // Close directly - this will interrupt any blocking operations immediately
        // shutdown() does not interrupt lwIP's internal mailbox waits on Windows
        zts_close(socket_fd_);
        socket_fd_ = -1;
        is_server_socket_ = false;
        connect_in_progress_ = false;
    }
    if (lease_.valid()) {
        lease_.release();
    }
}

void ZeroTierSocket::setup_socket(int fd) {
    // Set socket to non-blocking mode
    set_non_blocking(non_blocking_mode_);
    
    // Set receive and send timeouts to allow blocking operations to be interrupted
    // lwIP on Windows uses mailbox architecture that doesn't wake on close()
    // Timeouts allow operations to periodically check if socket is still valid
    if (!non_blocking_mode_) {
        struct zts_timeval tv;
        tv.tv_sec = 1;  // 1 second timeout
        tv.tv_usec = 0;
        zts_bsd_setsockopt(fd, ZTS_SOL_SOCKET, ZTS_SO_RCVTIMEO, &tv, sizeof(tv));
        zts_bsd_setsockopt(fd, ZTS_SOL_SOCKET, ZTS_SO_SNDTIMEO, &tv, sizeof(tv));
    }
    
    // Enable TCP_NODELAY by default for low-latency scatter-send messaging
    set_no_delay(true);
}

bool ZeroTierSocket::set_no_delay(bool enable) {
    if (socket_fd_ < 0) {
        return false;
    }
    int flag = enable ? 1 : 0;
    int result = zts_set_no_delay(socket_fd_, flag);
    if (result == ZTS_ERR_OK) {
        if (logger_) {
            logger_->debug("TCP_NODELAY " + std::string(enable ? "enabled" : "disabled") + 
                          " on fd " + std::to_string(socket_fd_));
        }
        return true;
    }
    if (logger_) {
        logger_->warning("Failed to set TCP_NODELAY on fd " + std::to_string(socket_fd_));
    }
    return false;
}

void ZeroTierSocket::set_non_blocking(bool non_blocking) {
    if (socket_fd_ < 0) return;
    
    int flags = zts_bsd_fcntl(socket_fd_, ZTS_F_GETFL, 0);
    if (flags < 0) return;
    
    if (non_blocking) {
        flags |= ZTS_O_NONBLOCK;
    } else {
        flags &= ~ZTS_O_NONBLOCK;
    }
    
    zts_bsd_fcntl(socket_fd_, ZTS_F_SETFL, flags);
}

bool ZeroTierSocket::try_connect(const std::string& host, int port, std::error_code& error) {
    // Ensure network is joined before connecting
    ensure_lease();

    // Lazily create fd if needed
    if (!open_fd_if_needed(&error)) {
        return true; // Error is completion
    }
    
    // If we're already in the process of connecting, check status instead of calling connect again
    if (connect_in_progress_) {
        return check_connect_complete(error);
    }
    
    struct zts_sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = ZTS_AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    
    if (zts_inet_pton(ZTS_AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        error = std::make_error_code(std::errc::invalid_argument);
        return true;
    }
    
    // First call to connect - initiate the connection
    int result = zts_bsd_connect(socket_fd_, (struct zts_sockaddr*)&addr, sizeof(addr));
    
    if (result == ZTS_ERR_OK) {
        // Connected immediately
        error = std::error_code{};
        return true;
    } else {
        int zts_err = zts_errno;
        if (ZeroTierErrnoCompat::is_would_block_errno(zts_err)) {
            // Connection in progress - mark it and return false to indicate "try again"
            connect_in_progress_ = true;
            error = std::error_code{}; // Clear any previous error
            return false;
        } else {
            // Connection failed immediately
            error = translate_error(zts_err);
            return true;
        }
    }
}

bool ZeroTierSocket::check_connect_complete(std::error_code& error) {
    if (socket_fd_ < 0 || !connect_in_progress_) {
        error = std::make_error_code(std::errc::bad_file_descriptor);
        return true;
    }
    
    // Try to get the peer address using ZeroTier's API
    char ip_str[ZTS_IP_MAX_STR_LEN];
    unsigned short port;
    
    if (zts_getpeername(socket_fd_, ip_str, sizeof(ip_str), &port) == ZTS_ERR_OK) {
        // Successfully got peer address - connection is complete
        error = std::error_code{};
        connect_in_progress_ = false;
        return true;
    }
    
    int zts_err = zts_errno;
    if (ZeroTierErrnoCompat::is_would_block_errno(zts_err) || 
        zts_err == ZTS_ENOTCONN) {
        // Still connecting
        return false;
    } else {
        // Connection failed
        error = translate_error(zts_err);
        connect_in_progress_ = false;
        return true;
    }
}

std::shared_ptr<IAsyncStream> ZeroTierSocket::try_accept(std::error_code& error) {
    if (socket_fd_ < 0 || !is_server_socket_) {
        error = std::make_error_code(std::errc::bad_file_descriptor);
        return nullptr;
    }
    
    struct zts_sockaddr_in client_addr;
    zts_socklen_t client_len = sizeof(client_addr);
    
    int client_fd = zts_bsd_accept(socket_fd_, (struct zts_sockaddr*)&client_addr, &client_len);
    
    if (client_fd >= 0) {
        // Success - create a new socket from the accepted connection
        try {
            error = std::error_code{};
            return std::make_shared<ZeroTierSocket>(client_fd);
        } catch (const std::exception&) {
            zts_close(client_fd);
            error = std::make_error_code(std::errc::resource_unavailable_try_again);
            return nullptr;
        }
    } else {
        int zts_err = zts_errno;
        if (is_would_block_error(zts_err)) {
            // Would block, no connection available
            return nullptr;
        } else {
            // Error occurred
            error = translate_error(zts_err);
            return nullptr;
        }
    }
}

bool ZeroTierSocket::try_read(void* buffer, size_t size, size_t& bytes_read, std::error_code& error) {
    if (socket_fd_ < 0) {
        error = std::make_error_code(std::errc::bad_file_descriptor);
        bytes_read = 0;
        return true;
    }
    
    ssize_t result = zts_recv(socket_fd_, buffer, size, 0);

    if (result > 0) {
        bytes_read = static_cast<size_t>(result);
        error = std::error_code{};
        return true;
    } else if (result == 0) {
        // Zero bytes: check zts_errno for a more specific cause if provided
        bytes_read = 0;
        int zts_err = zts_errno;
        if (zts_err != 0) {
            error = translate_error(zts_err);
        } else {
            error = std::make_error_code(std::errc::not_connected);
        }
        return true;
    } else {
        bytes_read = 0;
        int zts_err = zts_errno;
        if (is_would_block_error(zts_err)) {
            return false; // Would block
        } else {
            error = translate_error(zts_err);
            return true;
        }
    }
}

bool ZeroTierSocket::try_write(const void* buffer, size_t size, size_t& bytes_written, std::error_code& error) {
    if (socket_fd_ < 0) {
        error = std::make_error_code(std::errc::bad_file_descriptor);
        bytes_written = 0;
        return true;
    }
    
    ssize_t result = zts_send(socket_fd_, buffer, size, 0);
    
    if (result >= 0) {
        bytes_written = static_cast<size_t>(result);
        error = std::error_code{};
        return true;
    } else {
        bytes_written = 0;
        int zts_err = zts_errno;
        if (is_would_block_error(zts_err)) {
            return false; // Would block
        } else {
            error = translate_error(zts_err);
            return true;
        }
    }
}

bool ZeroTierSocket::bind(const std::string& host, int port) {
    // Ensure network is joined before binding/listening
    ensure_lease();

    // Lazily create fd if needed
    std::error_code fd_ec;
    if (!open_fd_if_needed(&fd_ec)) {
        if (logger_) {
            logger_->error(std::string{"ZeroTierSocket::bind: Failed to create socket: "} + fd_ec.message());
        }
        return false;
    }
    
    bind_host_ = host;
    bind_port_ = port;
    
    struct zts_sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = ZTS_AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    
    std::string bind_addr = host;
    if (host.empty() || host == "0.0.0.0") {
        bind_addr = "0.0.0.0";
    }
    
    if (zts_inet_pton(ZTS_AF_INET, bind_addr.c_str(), &addr.sin_addr) <= 0) {
        if (logger_) {
            logger_->error("ZeroTierSocket::bind: Invalid bind address: " + host);
        }
        return false;
    }
    
    if (zts_bsd_bind(socket_fd_, (struct zts_sockaddr*)&addr, sizeof(addr)) != 0) {
        int zts_err = zts_errno;
        std::string error_msg = "ZeroTierSocket::bind: Failed to bind to " + host + ":" + std::to_string(port) + 
                               " (" + ZeroTierErrnoCompat::errno_to_string(zts_err) + ")";
        if (logger_) {
            logger_->error(error_msg);
        }
        return false;
    }

    return true;
}

bool ZeroTierSocket::listen(int backlog) {
    if (socket_fd_ < 0) {
        if (logger_) {
            logger_->error("ZeroTierSocket::listen: Socket not open before listen call");
        }
        return false;
    }
    
    if (zts_bsd_listen(socket_fd_, backlog) != 0) {
        int zts_err = zts_errno;
        std::string error_msg = "ZeroTierSocket::listen: Failed to listen on " + bind_host_ + ":" + std::to_string(bind_port_) + 
                               " (" + ZeroTierErrnoCompat::errno_to_string(zts_err) + ")";
        if (logger_) {
            logger_->error(error_msg);
        }
        return false;
    }
    
    is_server_socket_ = true;
    return true;
}

void ZeroTierSocket::close() {
    int fd_to_close = -1;
    {
        std::lock_guard<std::mutex> lock(socket_mtx_);
        if (socket_fd_ >= 0) {
            fd_to_close = socket_fd_;
            socket_fd_ = -1;  // Invalidate immediately under lock
        }
    }
    
    // Close outside the lock to avoid holding lock during potentially slow operation
    if (fd_to_close >= 0) {
        // Close directly - this will interrupt any blocking operations
        // (shutdown() does not interrupt lwIP's internal mailbox waits)
        zts_close(fd_to_close);
        is_server_socket_ = false;
        connect_in_progress_ = false;
    }
    // Release network lease (may cause leave if last user)
    // if (lease_.valid()) {
        // lease_.release();
    // }
}

bool ZeroTierSocket::is_open() const {
    return socket_fd_ >= 0;
}

std::string ZeroTierSocket::socket_type() const {
    return "zerotier_socket";
}

// === Blocking interface implementations ===
void ZeroTierSocket::connect(const std::string& host, int port, std::error_code& error) {
    // Ensure network is joined before connecting
    ensure_lease();

    // Lazily create fd if needed
    if (!open_fd_if_needed(&error)) {
        return;
    }

    struct zts_sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = ZTS_AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (zts_inet_pton(ZTS_AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        error = std::make_error_code(std::errc::invalid_argument);
        return;
    }

    // Set socket to non-blocking mode temporarily so connect() returns immediately
    // This allows us to implement our own timeout logic with periodic checks
    int fd;
    {
        std::lock_guard<std::mutex> lock(socket_mtx_);
        fd = socket_fd_;
        if (fd < 0) {
            error = std::make_error_code(std::errc::bad_file_descriptor);
            return;
        }
    }
    
    // Save original blocking mode and switch to non-blocking for connect
    int flags = zts_bsd_fcntl(fd, ZTS_F_GETFL, 0);
    if (flags >= 0) {
        zts_bsd_fcntl(fd, ZTS_F_SETFL, flags | ZTS_O_NONBLOCK);
    }

    // Initiate non-blocking connect
    int result = zts_bsd_connect(fd, (struct zts_sockaddr*)&addr, sizeof(addr));
    
    if (result == 0) {
        // Connected immediately (rare for TCP)
        // Restore blocking mode
        if (flags >= 0) {
            zts_bsd_fcntl(fd, ZTS_F_SETFL, flags);
        }
        is_connected_ = true;
        update_endpoints();
        error = std::error_code{};
        return;
    }
    
    int zts_err = zts_errno;
    if (!ZeroTierErrnoCompat::is_would_block_errno(zts_err) && zts_err != EINPROGRESS) {
        // Immediate failure (invalid address, etc.)
        if (flags >= 0) {
            zts_bsd_fcntl(fd, ZTS_F_SETFL, flags);
        }
        error = translate_error(zts_err);
        return;
    }
    
    // Connection in progress - poll with timeout checks
    // Check every 500ms to detect if socket was closed (shutdown requested)
    const int poll_timeout_ms = 500;
    
    while (true) {
        // Check for shutdown request
        if (shutdown_requested_.load(std::memory_order_relaxed)) {
            if (flags >= 0) {
                zts_bsd_fcntl(fd, ZTS_F_SETFL, flags);
            }
            error = std::make_error_code(std::errc::bad_file_descriptor);
            return;
        }
        
        // Check if socket still valid
        {
            std::lock_guard<std::mutex> lock(socket_mtx_);
            if (socket_fd_ < 0) {
                // Socket was closed (shutdown requested)
                error = std::make_error_code(std::errc::bad_file_descriptor);
                return;
            }
        }
        
        // Poll for writability (indicates connection complete)
        struct zts_pollfd pfd;
        pfd.fd = fd;
        pfd.events = ZTS_POLLOUT;
        pfd.revents = 0;
        
        int poll_result = zts_bsd_poll(&pfd, 1, poll_timeout_ms);
        
        if (poll_result > 0 && (pfd.revents & ZTS_POLLOUT)) {
            // Socket is writable - check if connection succeeded
            int sock_err = 0;
            zts_socklen_t len = sizeof(sock_err);
            if (zts_bsd_getsockopt(fd, ZTS_SOL_SOCKET, ZTS_SO_ERROR, &sock_err, &len) == 0) {
                if (sock_err == 0) {
                    // Connection succeeded
                    // Restore blocking mode
                    if (flags >= 0) {
                        zts_bsd_fcntl(fd, ZTS_F_SETFL, flags);
                    }
                    is_connected_ = true;
                    update_endpoints();
                    error = std::error_code{};
                    return;
                } else {
                    // Connection failed with error
                    if (flags >= 0) {
                        zts_bsd_fcntl(fd, ZTS_F_SETFL, flags);
                    }
                    error = translate_error(sock_err);
                    return;
                }
            }
        } else if (poll_result < 0) {
            // Poll error
            int poll_err = zts_errno;
            if (flags >= 0) {
                zts_bsd_fcntl(fd, ZTS_F_SETFL, flags);
            }
            error = translate_error(poll_err);
            return;
        }
        // poll_result == 0 means timeout - loop again to check socket validity
    }
}

void ZeroTierSocket::read(void* buffer, size_t size, size_t& bytes_read, std::error_code& error) {
    // Loop to handle SO_RCVTIMEO timeouts - retry if socket still valid
    // This allows close() from another thread to be detected
    while (true) {
        // Check for shutdown request
        if (shutdown_requested_.load(std::memory_order_relaxed)) {
            error = std::make_error_code(std::errc::bad_file_descriptor);
            bytes_read = 0;
            return;
        }
        
        int fd;
        {
            std::lock_guard<std::mutex> lock(socket_mtx_);
            fd = socket_fd_;
        }
        
        if (fd < 0) {
            error = std::make_error_code(std::errc::bad_file_descriptor);
            bytes_read = 0;
            return;
        }

        ssize_t result = zts_recv(fd, buffer, size, 0);

        if (result > 0) {
            bytes_read = static_cast<size_t>(result);
            error = std::error_code{};
            return;
        } else if (result == 0) {
            bytes_read = 0;
            int zts_err = zts_errno;
            if (zts_err != 0) {
                error = translate_error(zts_err);
            } else {
                error = std::make_error_code(std::errc::not_connected);
            }
            return;
        } else {
            // Error occurred
            int zts_err = zts_errno;
            
            // Normalize error code to handle platform differences
            int norm = ZeroTierErrnoCompat::normalize_errno(zts_err);
            
            // If timeout/wouldblock or shutdown, check if socket still valid and retry
            // ESHUTDOWN can occur if close() was called while recv was blocked
            if (norm == ETIMEDOUT || norm == ESHUTDOWN || norm == EWOULDBLOCK) {
                // Re-check socket validity under lock before retrying
                std::lock_guard<std::mutex> lock(socket_mtx_);
                if (socket_fd_ >= 0) {
                    continue; // Socket still valid, retry recv
                }
                // Socket was closed, return bad_file_descriptor
                error = std::make_error_code(std::errc::bad_file_descriptor);
                bytes_read = 0;
                return;
            }
            
            // Other errors are fatal
            error = translate_error(zts_err);
            bytes_read = 0;
            return;
        }
    }
}

void ZeroTierSocket::write(const void* buffer, size_t size, size_t& bytes_written, std::error_code& error) {
    if (socket_fd_ < 0) {
        error = std::make_error_code(std::errc::bad_file_descriptor);
        bytes_written = 0;
        return;
    }

    ssize_t result = zts_send(socket_fd_, buffer, size, 0);
    
    if (result >= 0) {
        bytes_written = static_cast<size_t>(result);
        error = std::error_code{};
    } else {
        bytes_written = 0;
        error = translate_error(zts_errno);
    }
}

int ZeroTierSocket::get_handle() const {
    return socket_fd_;
}

std::string ZeroTierSocket::remote_endpoint() const {
    if (!is_open()) return "";
    
    char ip_str[ZTS_IP_MAX_STR_LEN];
    unsigned short port;
    
    if (zts_getpeername(socket_fd_, ip_str, sizeof(ip_str), &port) == ZTS_ERR_OK) {
        return std::string(ip_str) + ":" + std::to_string(port);
    }
    return "";
}

std::string ZeroTierSocket::local_endpoint() const {
    if (!is_open()) return "";
    
    char ip_str[ZTS_IP_MAX_STR_LEN];
    unsigned short port;
    
    if (zts_getsockname(socket_fd_, ip_str, sizeof(ip_str), &port) == ZTS_ERR_OK) {
        return std::string(ip_str) + ":" + std::to_string(port);
    }
    return "";
}

bool ZeroTierSocket::is_would_block_error(int error_code) const {
    // Use the compatibility layer for consistent errno handling
    return ZeroTierErrnoCompat::is_would_block_errno(error_code);
}

std::error_code ZeroTierSocket::translate_error(int zts_error) const {
    // Use the compatibility layer to normalize the error first
    int normalized_error = ZeroTierErrnoCompat::normalize_errno(zts_error);
    
    switch (normalized_error) {
        case EINPROGRESS:
            return std::make_error_code(std::errc::operation_in_progress);
        
#ifdef EAGAIN
        case EAGAIN:
#endif
#if defined(EWOULDBLOCK) && ( !defined(EAGAIN) || (EWOULDBLOCK != EAGAIN) )
        case EWOULDBLOCK:
#endif
            return std::make_error_code(std::errc::resource_unavailable_try_again);
        case EALREADY:
            return std::make_error_code(std::errc::operation_in_progress);
        case ECONNABORTED:
            return std::make_error_code(std::errc::connection_aborted);
        case ENETDOWN:
            return std::make_error_code(std::errc::network_unreachable);
        case EPROTONOSUPPORT:
            return std::make_error_code(std::errc::protocol_not_supported);
        case EOPNOTSUPP:
            return std::make_error_code(std::errc::function_not_supported);
        case ECONNREFUSED:
            return std::make_error_code(std::errc::connection_refused);
        case ETIMEDOUT:
            return std::make_error_code(std::errc::timed_out);
        case ECONNRESET:
            return std::make_error_code(std::errc::connection_reset);
        case EHOSTUNREACH:
            return std::make_error_code(std::errc::host_unreachable);
        case ENETUNREACH:
            return std::make_error_code(std::errc::network_unreachable);
        case ENOTCONN:
            return std::make_error_code(std::errc::not_connected);
        case EADDRINUSE:
            return std::make_error_code(std::errc::address_in_use);
        case EADDRNOTAVAIL:
            return std::make_error_code(std::errc::address_not_available);
        case EBADF:
            return std::make_error_code(std::errc::bad_file_descriptor);
        case EINVAL:
            return std::make_error_code(std::errc::invalid_argument);
        case ENOMEM:
            return std::make_error_code(std::errc::not_enough_memory);
        case ENOBUFS:
            return std::make_error_code(std::errc::no_buffer_space);
        case EISCONN:
            return std::make_error_code(std::errc::already_connected);
        case ESHUTDOWN:
            return std::make_error_code(std::errc::connection_aborted);
        default:
            return std::make_error_code(std::errc::io_error);
    }
}

void ZeroTierSocket::update_endpoints() {
    // Update local and remote endpoint strings
    if (is_open()) {
        local_endpoint_ = local_endpoint();
        remote_endpoint_ = remote_endpoint();
    }
}

bool ZeroTierSocket::start_listening(const std::string& host, int port, int backlog) {
    // Ensure network is joined before binding/listening and create fd if needed
    ensure_lease();

    std::error_code ec;
    if (!open_fd_if_needed(&ec)) {
        if (logger_) {
            logger_->error(std::string{"ZeroTierSocket::start_listening: Failed to open socket fd: "} + ec.message());
        }
        return false;
    }

    // Store bind parameters for diagnostics
    bind_host_ = host;
    bind_port_ = port;

    // Perform bind + listen using libzt primitives
    if (!bind(host, port)) {
        return false;
    }
    if (!listen(backlog)) {
        return false;
    }
    return true;
}

// === Factory helpers ===
std::shared_ptr<ZeroTierSocket> ZeroTierSocket::create() {
    return std::make_shared<ZeroTierSocket>();
}

std::shared_ptr<ZeroTierSocket> ZeroTierSocket::create(std::shared_ptr<Logger> logger) {
    return std::make_shared<ZeroTierSocket>(std::move(logger));
}

std::shared_ptr<ZeroTierSocket> ZeroTierSocket::create_blocking(std::shared_ptr<Logger> logger) {
    if (logger) {
        return std::make_shared<ZeroTierSocket>(SocketMode::Blocking, std::move(logger));
    }
    return std::make_shared<ZeroTierSocket>(SocketMode::Blocking);
}

std::shared_ptr<ZeroTierSocket> ZeroTierSocket::from_fd(int fd) {
    return std::make_shared<ZeroTierSocket>(fd);
}

std::shared_ptr<ZeroTierSocket> ZeroTierSocket::from_fd(int fd, std::shared_ptr<Logger> logger) {
    return std::make_shared<ZeroTierSocket>(fd, std::move(logger));
}

std::shared_ptr<IAsyncStream> ZeroTierSocket::blocking_accept(std::error_code& error, std::chrono::milliseconds timeout) {
    error.clear();
    if (socket_fd_ < 0 || !is_server_socket_) {
        error = std::make_error_code(std::errc::bad_file_descriptor);
        return nullptr;
    }

    // Ensure the listening socket is in blocking mode for a dedicated acceptor thread.
    set_non_blocking(false);

    // Apply a receive timeout so accept wakes periodically and we can notice shutdown.
    // This relies on LWIP_SO_RCVTIMEO affecting netconn_accept() timeout.
    // Convert std::chrono::milliseconds -> seconds + microseconds.
    if (timeout.count() < 0) {
        timeout = std::chrono::milliseconds(0);
    }
    auto secs = static_cast<int>(timeout.count() / 1000);
    auto micros = static_cast<int>((timeout.count() % 1000) * 1000); // ms -> microseconds
    (void)zts_set_recv_timeout(socket_fd_, secs, micros); // best-effort; ignore failure

    struct zts_sockaddr_in client_addr;
    zts_socklen_t client_len = sizeof(client_addr);

    for (;;) {
        // Check for shutdown request
        if (shutdown_requested_.load(std::memory_order_relaxed)) {
            error = std::make_error_code(std::errc::bad_file_descriptor);
            return nullptr;
        }
        
        // Take a snapshot of the fd and server state to avoid TOCTOU between check and accept
        const int listen_fd = socket_fd_;
        const bool server_state = is_server_socket_;
        if (listen_fd < 0 || !server_state) {
            error = std::make_error_code(std::errc::bad_file_descriptor);
            return nullptr;
        }

        int client_fd = zts_bsd_accept(listen_fd, (struct zts_sockaddr*)&client_addr, &client_len);
        if (client_fd >= 0) {
            try {
                // Wrap accepted fd; constructor sets it to non-blocking for I/O
                error.clear();
                if (logger_) {
                    return ZeroTierSocket::from_fd(client_fd, logger_);
                }
                return ZeroTierSocket::from_fd(client_fd);
            } catch (...) {
                zts_close(client_fd);
                error = std::make_error_code(std::errc::resource_unavailable_try_again);
                return nullptr;
            }
        }

        // Handle error conditions
        int zts_err = zts_errno;
        // Normalize to host errno space
        int norm = ZeroTierErrnoCompat::normalize_errno(zts_err);
        if (norm == EAGAIN || norm == EWOULDBLOCK || norm == ETIMEDOUT) {
            // Propagate a non-error null to let caller check its own control flags
            error.clear();
            return nullptr;
        }

        if (norm == ECONNABORTED) {
            // Transient condition: return to caller without error
            error.clear();
            return nullptr;
        }

        if (norm == ESHUTDOWN) {
            // Treat as timeout-equivalent and let caller decide
            error.clear();
            return nullptr;
        }

        // EBADF can occur if the listening fd was closed between snapshot and accept
        if (norm == EBADF) {
            // Treat as closure signal; return quietly
            error.clear();
            return nullptr;
        }

        // Any other error -> return translated error
        error = translate_error(zts_err);
        return nullptr;
    }
}

void ZeroTierSocket::shutdown() {
    shutdown_requested_.store(true, std::memory_order_relaxed);
    // Propagate shutdown to the node service to interrupt network join operations
    transport::ZeroTierNodeService::instance().shutdown();
}

bool ZeroTierSocket::open_fd_if_needed(std::error_code* error) {
    std::lock_guard<std::mutex> lock(socket_mtx_);
    if (socket_fd_ >= 0) return true;
    int fd = zts_socket(ZTS_AF_INET, ZTS_SOCK_STREAM, 0);
    if (fd < 0) {
        if (error) {
            int zts_err = zts_errno;
            *error = translate_error(zts_err);
        }
        return false;
    }
    socket_fd_ = fd;
    setup_socket(socket_fd_);
    return true;
}