/**
 * \file SkillTestIterator.hpp
 * \brief Cycles through all registered (skill_id, case_index) combinations,
 *        producing test request+response buffers on demand.
 *
 * Built once from SkillRegistry state at initialization time.
 * Wraps around when all combinations are exhausted.
 */
#pragma once

#include "skills/registry/PayloadBuffer.hpp"

#include <cstdint>
#include <memory>
#include <vector>

struct TestTaskData {
    uint32_t skill_id;
    std::unique_ptr<TaskMessenger::Skills::PayloadBufferBase> request;
    std::unique_ptr<TaskMessenger::Skills::PayloadBufferBase> response;
};

class SkillTestIterator {
public:
    /**
     * \brief Build the iteration table from currently registered skills.
     *
     * Queries SkillRegistry::instance() for skill_ids() and
     * get_test_case_count() per skill. Must be called after the registry
     * is fully populated (i.e. after static initialization).
     */
    SkillTestIterator();

    /**
     * \brief Generate the next \p count test task items.
     *
     * Each item contains a skill_id, a freshly-allocated request buffer,
     * and a matching response buffer. Advances the internal cursor and
     * wraps to the beginning when all combinations are exhausted.
     *
     * \param count Number of items to produce.
     * \return Vector of TestTaskData (may be shorter if no skills are registered).
     */
    std::vector<TestTaskData> next(uint32_t count);

    /// Reset cursor to the first (skill_id, case_index) entry.
    void reset();

    /// Total number of (skill_id, case_index) combinations.
    [[nodiscard]] size_t total_combinations() const { return entries_.size(); }

private:
    struct Entry {
        uint32_t skill_id;
        size_t   case_index;
    };

    std::vector<Entry> entries_;
    size_t cursor_ = 0;
};
