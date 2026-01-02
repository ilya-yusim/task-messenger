/**
 * \file ZeroTierErrnoCompat.hpp
 * \brief Cross-platform errno compatibility helpers for ZeroTier/libzt.
 * \ingroup socket_backend
 * \details Normalizes libzt errno values to host errno space and provides utilities
 *  to identify would-block conditions and to stringify common errors.
 */
#pragma once

#include <cerrno>

// Detect compiler and platform
#ifdef _MSC_VER
    #if _MSC_VER >= 1910  // Visual Studio 2017 or later
        #define ZT_USING_SYSTEM_ERRNO 1
    #else
        #define ZT_USING_SYSTEM_ERRNO 0
    #endif
#else
    #define ZT_USING_SYSTEM_ERRNO 1
#endif

// Include appropriate errno definitions
#if ZT_USING_SYSTEM_ERRNO
    // Use system errno.h (MSVC 2017+ or other compilers)
    #ifdef _WIN32
        #include <winsock2.h>
        #include <ws2tcpip.h>
    #endif
#else
    // Use lwIP errno definitions for older MSVC
    #define EINPROGRESS     115
    #define EALREADY        114
    #define ENOTSOCK        128
    #define EDESTADDRREQ    109
    #define EMSGSIZE        122
    #define EPROTOTYPE      136
    #define ENOPROTOOPT     123
    #define EPROTONOSUPPORT 135
    #define ESOCKTNOSUPPORT 137
    #define EOPNOTSUPP      130
    #define EPFNOSUPPORT    134
    #define EAFNOSUPPORT    102
    #define EADDRINUSE      100
    #define EADDRNOTAVAIL   101
    #define ENETDOWN        116
    #define ENETUNREACH     118
    #define ENETRESET       117
    #define ECONNABORTED    106
    #define ECONNRESET      108
    #define ENOBUFS         119
    #define EISCONN         113
    #define ENOTCONN        126
    #define ESHUTDOWN       140
    #define ETOOMANYREFS    141
    #define ETIMEDOUT       138
    #define ECONNREFUSED    107
    #define ELOOP           114
    #define ENAMETOOLONG    78
    #define EHOSTDOWN       112
    #define EHOSTUNREACH    110
    #define ENOTEMPTY       90
    #define EPROCLIM        142
    #define EUSERS          143
    #define EDQUOT          144
    #define ESTALE          145
    #define EREMOTE         146
    #define EWOULDBLOCK     EAGAIN
#endif

// Ensure ESHUTDOWN is defined on all platforms
#ifndef ESHUTDOWN
#define ESHUTDOWN 200  // Use a high value that's unlikely to conflict
#endif

// ZeroTier-specific errno constants
// These are the errno values that ZeroTier libzt may return
#define ZTS_EINPROGRESS     115
#define ZTS_EAGAIN          11  
#define ZTS_EWOULDBLOCK     ZTS_EAGAIN
#define ZTS_ECONNREFUSED    107
#define ZTS_ECONNRESET      108  
#define ZTS_ENOTCONN        126
#define ZTS_ETIMEDOUT       138
#define ZTS_EHOSTUNREACH    110
#define ZTS_ENETUNREACH     118
#define ZTS_EADDRINUSE      100
#define ZTS_EADDRNOTAVAIL   101
#define ZTS_EBADF           9
#define ZTS_EINVAL          22
#define ZTS_ENOMEM          12
#define ZTS_ENOBUFS         119
#define ZTS_EISCONN         113
#define ZTS_ESHUTDOWN       140

/** \brief Normalize and interpret ZeroTier errno values.
 *  \details Utilities to translate libzt error codes to standard errno values,
 *  detect non-blocking/would-block results, and render human-readable strings.
 */
class ZeroTierErrnoCompat {
public:
    /** \brief Normalize a platform/libzt errno to a standard value.
     *  \details Converts libzt-specific errno values to POSIX-like errno equivalents
     *  for consistent host-side handling.
     *  \param errno_val Errno value to normalize.
     *  \return Normalized errno value.
     */
    static int normalize_errno(int errno_val) {
        // Handle ZeroTier-specific constants first
        if (errno_val == ZTS_EINPROGRESS) return EINPROGRESS;
        if (errno_val == ZTS_EAGAIN || errno_val == ZTS_EWOULDBLOCK) return EWOULDBLOCK;
        if (errno_val == ZTS_ECONNREFUSED) return ECONNREFUSED;
        if (errno_val == ZTS_ECONNRESET) return ECONNRESET;
        if (errno_val == ZTS_ENOTCONN) return ENOTCONN;
        if (errno_val == ZTS_ETIMEDOUT) return ETIMEDOUT;
        if (errno_val == ZTS_EHOSTUNREACH) return EHOSTUNREACH;
        if (errno_val == ZTS_ENETUNREACH) return ENETUNREACH;
        if (errno_val == ZTS_EADDRINUSE) return EADDRINUSE;
        if (errno_val == ZTS_EADDRNOTAVAIL) return EADDRNOTAVAIL;
        if (errno_val == ZTS_EBADF) return EBADF;
        if (errno_val == ZTS_EINVAL) return EINVAL;
        if (errno_val == ZTS_ENOMEM) return ENOMEM;
        if (errno_val == ZTS_ENOBUFS) return ENOBUFS;
        if (errno_val == ZTS_EISCONN) return EISCONN;
        if (errno_val == ZTS_ESHUTDOWN) return ESHUTDOWN;
        
        // Return as-is if no mapping needed
        return errno_val;
    }
    
    /** \brief Check whether an errno indicates a would-block condition.
     *  \param errno_val Errno value to test.
     *  \return true if the operation would block; false otherwise.
     */
    static bool is_would_block_errno(int errno_val) {
        // Check original values first for would-block conditions
        if (errno_val == EAGAIN || errno_val == EWOULDBLOCK || errno_val == EINPROGRESS) {
            return true;
        }
        if (errno_val == ZTS_EAGAIN || errno_val == ZTS_EWOULDBLOCK || errno_val == ZTS_EINPROGRESS) {
            return true;
        }
        
        // Also check normalized values
        int normalized = normalize_errno(errno_val);
        return (normalized == EAGAIN) || 
               (normalized == EWOULDBLOCK) ||
               (normalized == EINPROGRESS);
    }
    
    /** \brief Convert errno to a human-readable string.
     *  \param errno_val Errno value to describe.
     *  \return Constant string description.
     */
    static const char* errno_to_string(int errno_val) {
        int normalized = normalize_errno(errno_val);
        
        switch (normalized) {
            case EINPROGRESS: return "Operation in progress";
            case EWOULDBLOCK: return "Operation would block";
            case ECONNREFUSED: return "Connection refused";
            case ECONNRESET: return "Connection reset";
            case ENOTCONN: return "Socket not connected";
            case ETIMEDOUT: return "Operation timed out";
            case EHOSTUNREACH: return "Host unreachable";
            case ENETUNREACH: return "Network unreachable";
            case EADDRINUSE: return "Address already in use";
            case EADDRNOTAVAIL: return "Address not available";
            case EBADF: return "Bad file descriptor";
            case EINVAL: return "Invalid argument";
            case ENOMEM: return "Not enough memory";
            case ENOBUFS: return "No buffer space available";
            case EISCONN: return "Socket already connected";
            case ESHUTDOWN: return "Socket shut down";
            default: return "Unknown error";
        }
    }
};
