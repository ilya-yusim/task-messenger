/**
 * \file worker/runtime/IRuntimeMode.hpp
 * \brief Strategy interface describing worker runtime behaviors.
 */
#pragma once

#include <string>
#include <cstdint>

class TaskProcessor;       // fwd

/** \brief Abstraction for a runtime implementation handling socket I/O and task dispatch. */
class IRuntimeMode {
public:
    virtual ~IRuntimeMode() = default;

    /** \brief Create or reconnect a socket to the manager. */
    virtual bool connect() = 0;
    /** \brief Close the active socket while keeping resources available. */
    virtual void disconnect() = 0;
    /** \brief Release all socket resources, leaving the transport network. */
    virtual void release() = 0;
    /** \brief Interrupt blocking operations and begin shutdown. */
    virtual void shutdown() = 0;
    /** \brief Check whether a connected socket is currently available. */
    virtual bool is_connected() const = 0;
    /** \brief Retrieve a printable description of the local endpoint. */
    virtual std::string get_local_endpoint() const = 0;

    /** \brief Execute the I/O loop until pause or failure occurs. */
    virtual bool run_loop(TaskProcessor& processor) = 0;
    /** \brief Request the active loop to pause gracefully. */
    virtual void pause() = 0;

    /** \brief Completed task count. */
    virtual int get_task_count() const = 0;
    /** \brief Raw bytes sent since last reset. */
    virtual std::uint64_t get_bytes_sent() const = 0;
    /** \brief Raw bytes received since last reset. */
    virtual std::uint64_t get_bytes_received() const = 0;
};
