/**
 * \file worker/processor/TaskProcessor.cpp
 * \brief Implementation of the simple task execution shim.
 */
#include "TaskProcessor.hpp"
#include "logger.hpp"

#include <algorithm>
#include <string>
#include <limits>
#include <random>
#include <thread>
#include <chrono>

std::string TaskProcessor::process(uint32_t task_id, uint32_t skill_id, const std::string& payload) {
    if (logger_) {
        logger_->debug("Processing task " + std::to_string(task_id) +
                       " with skill " + std::to_string(skill_id));
    }

    // Add random delay between 0.1 and 1.0 seconds
    // static thread_local std::mt19937 rng(std::random_device{}());
    // std::uniform_int_distribution<int> dist(100, 1000); // milliseconds
    // int delay_ms = dist(rng);
    // std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

    switch (skill_id) {
        case 1: {
            std::string result = payload;
            std::reverse(result.begin(), result.end());
            return "Reversed: " + result;
        }
        case 2: {
            try {
                long long parsed = std::stoll(payload);
                // Check for overflow when doubling within int range.
                if (parsed > std::numeric_limits<int>::max() / 2 ||
                    parsed < std::numeric_limits<int>::min() / 2) {
                    return "Error: overflow";
                }
                int num = static_cast<int>(parsed);
                int result = num * 2; // Double the number with overflow protection above
                return "Double of " + payload + " is " + std::to_string(result);
            } catch (...) {
                return "Error: Invalid number format";
            }
        }
        case 3:
            return std::string("Processed file: ") + payload + " (simulated)";
        default:
            return std::string("Unknown skill_id: ") + std::to_string(skill_id);
    }
}
