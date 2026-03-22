/**
 * \file TaskIdGenerator.hpp
 * \brief Atomic counter helper for issuing unique task IDs.
 */
#pragma once

#include <atomic>
#include <cstdint>

class TaskIdGenerator {
public:
    TaskIdGenerator() = default;
    ~TaskIdGenerator() = default;

    TaskIdGenerator(const TaskIdGenerator&) = delete;
    TaskIdGenerator& operator=(const TaskIdGenerator&) = delete;
    TaskIdGenerator(TaskIdGenerator&&) = delete;
    TaskIdGenerator& operator=(TaskIdGenerator&&) = delete;

    uint32_t get_next_id() {
        uint32_t next = counter_.fetch_add(1, std::memory_order_relaxed);
        if (next == 0) {
            next = counter_.fetch_add(1, std::memory_order_relaxed);
        }
        return next;
    }

private:
    std::atomic<uint32_t> counter_{1};
};
