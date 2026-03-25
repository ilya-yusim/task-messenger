#include "SkillTestIterator.hpp"
#include "skills/registry/SkillRegistry.hpp"

using namespace TaskMessenger::Skills;

SkillTestIterator::SkillTestIterator() {
    auto& registry = SkillRegistry::instance();
    auto skill_ids = registry.skill_ids();

    for (uint32_t sid : skill_ids) {
        size_t case_count = registry.get_test_case_count(sid);
        for (size_t ci = 0; ci < case_count; ++ci) {
            entries_.push_back({sid, ci});
        }
    }
}

std::vector<TestTaskData> SkillTestIterator::next(uint32_t count) {
    std::vector<TestTaskData> result;

    if (entries_.empty() || count == 0) {
        return result;
    }

    result.reserve(count);
    auto& registry = SkillRegistry::instance();

    for (uint32_t i = 0; i < count; ++i) {
        auto& entry = entries_[cursor_];
        cursor_ = (cursor_ + 1) % entries_.size();

        auto request = registry.create_test_request_buffer(entry.skill_id, entry.case_index);
        if (!request) {
            continue;   // skip entries that fail to produce a buffer
        }

        auto response = registry.create_response_buffer(entry.skill_id, request->span());
        if (!response) {
            continue;
        }

        result.push_back({entry.skill_id, std::move(request), std::move(response)});
    }

    return result;
}

void SkillTestIterator::reset() {
    cursor_ = 0;
}
