/**
 * \file IServerSocket.hpp
 * \brief Non-blocking server/acceptor interface.
 * \ingroup socket_backend
 * \details Defines server startup and accept primitives.
 * \see IAsyncStream
 */
#pragma once

#include <memory>
#include <system_error>
#include <string>
#include <thread>
#include <chrono>
#include "ISocketLifecycle.hpp"

struct IAsyncStream; // fwd

     /** \brief Non-blocking server role interface (acceptor + startup).
      *  \ingroup socket_backend
      */
struct IServerSocket : public virtual ISocketLifecycle {
  
                /** \brief Bind + listen convenience for server startup.
          *  \details Performs the platform-specific bind + listen sequence and applies any
          *  backend socket options (normalization, permissions) required. Prefer this over
          *  calling low-level steps directly so backend behavior stays encapsulated.
          *  Implementations should avoid throwing and instead log failures internally.
          *  \param host Interface/address to bind (e.g. "0.0.0.0").
          *  \param port Port number to listen on.
          *  \param backlog Hint for pending connection queue length (platform may clamp).
          *  \return true on success, false on failure (with errors logged internally).
                    */
                virtual bool start_listening(const std::string& host, int port, int backlog) = 0;
        
        /** \brief Attempt a non-blocking accept.
         *  \details Returns a connected stream if a pending client exists; otherwise returns
         *  nullptr. Sets \p error only on non-transient failure (e.g. fatal socket error).
         *  \param error Receives non-transient error codes; cleared on success or if no client.
         */
        virtual std::shared_ptr<IAsyncStream> try_accept(std::error_code& error) = 0;

        /** \brief Timed blocking accept supporting responsive shutdown.
         *  \details Rationale :
         *  We considered building a custom accept loop around try_accept + carefully tuned
         *  sleeps. That approach becomes a balancing act between latency (how fast we accept
         *  a new client) and CPU efficiency (avoiding busy-wait). By delegating blocking
         *  behavior to the underlying TCP/IP stack with a recv timeout we leverage its
         *  existing, well-tested scheduling and wake-up logic instead of reinventing it. A
         *  moderately long timeout keeps the acceptor thread almost idle under no load while
         *  still bounding shutdown latency.
         *  
         *  Default Timeout Choice :
         *  500ms strikes a practical balance: negligible CPU usage in idle periods and
         *  sub-second responsiveness when initiating shutdown. Platforms exposing finer
         *  control (e.g. SO_RCVTIMEO) handle the timing internally; if unsupported, backend
         *  implementations should approximate.
         *  
         *  Behavior :
         *  - Returns a new connected stream on success (error cleared).
         *  - Returns nullptr with error cleared when the timeout elapses or transient
         *    conditions (e.g. would-block, aborted) occur; caller can loop and check its own
         *    shutdown flag.
         *  - Returns nullptr with error set on non-transient failures.
         *  \param error Receives non-transient errors; cleared on success or timeout.
         *  \param timeout Desired maximum blocking interval. Backend may clamp or approximate.
         */
        virtual std::shared_ptr<IAsyncStream> blocking_accept(std::error_code& error,
                                                              std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) {
        
            error.clear();
            const auto slice = std::chrono::milliseconds(5);
            auto elapsed = std::chrono::milliseconds(0);
            while (elapsed < timeout) {
                auto client = try_accept(error);
                if (client) {
                    error.clear();
                    return client;
                }
                if (error) {
                    return nullptr; // Non-transient failure
                }
                std::this_thread::sleep_for(slice);
                elapsed += slice;
            }
            // Timed out: return nullptr with error cleared
            error.clear();
            return nullptr;
        }
};
