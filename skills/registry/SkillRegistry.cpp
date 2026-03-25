/**
 * @file skills/registry/SkillRegistry.cpp
 * @brief Implementation of the unified SkillRegistry.
 *
 * Skills self-register via static initialization using REGISTER_SKILL_CLASS.
 * See SkillRegistration.hpp and skills/builtins/ for examples.
 */
#include "SkillRegistry.hpp"
#include "CompareUtils.hpp"
#include "PayloadBuffer.hpp"
#include "logger.hpp"

#include <cstdlib>
#include <iostream>

namespace TaskMessenger::Skills {

SkillRegistry& SkillRegistry::instance() {
    static SkillRegistry instance;
    return instance;
}

SkillRegistry::SkillRegistry(std::shared_ptr<Logger> logger)
    : logger_(std::move(logger))
{
}

void SkillRegistry::register_skill(std::unique_ptr<ISkill> skill) {
    std::lock_guard<std::mutex> lock(mutex_);

    const uint32_t id = skill->skill_id();
    const auto name = skill->skill_name();

    auto it = skills_.find(id);
    if (it != skills_.end() && it->second->skill_name() != name) {
        // Hash collision between different skills — fatal error.
        // Registration runs during static init (before main), logger may be null.
        std::cerr << "FATAL: Skill ID collision: \"" << name
                  << "\" and \"" << it->second->skill_name()
                  << "\" both hash to " << id << std::endl;
        std::abort();
    }

    name_to_id_[std::string(name)] = id;
    skills_[id] = std::move(skill);
}

bool SkillRegistry::has_skill(uint32_t skill_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return skills_.find(skill_id) != skills_.end();
}

bool SkillRegistry::has_skill(std::string_view name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return name_to_id_.find(std::string(name)) != name_to_id_.end();
}

std::optional<uint32_t> SkillRegistry::get_skill_id(std::string_view name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = name_to_id_.find(std::string(name));
    if (it != name_to_id_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string SkillRegistry::get_skill_name(uint32_t skill_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = skills_.find(skill_id);
    if (it != skills_.end()) {
        return std::string(it->second->skill_name());
    }
    return "";
}

std::vector<uint32_t> SkillRegistry::skill_ids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint32_t> result;
    result.reserve(skills_.size());
    for (const auto& [id, skill] : skills_) {
        result.push_back(id);
    }
    return result;
}

size_t SkillRegistry::skill_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return skills_.size();
}

bool SkillRegistry::dispatch(
    uint32_t skill_id,
    uint32_t task_id,
    std::span<const uint8_t> request,
    std::span<uint8_t> response
) {
    // Find skill under lock, copy shared_ptr to keep it alive outside lock
    std::shared_ptr<ISkill> skill;
    std::string name;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = skills_.find(skill_id);
        if (it == skills_.end()) {
            log_debug("Unknown skill_id=" + std::to_string(skill_id) +
                      " for task_id=" + std::to_string(task_id));
            return false;
        }
        skill = it->second;
        name = std::string(skill->skill_name());
    }
    
    // Process outside lock (handler execution may be slow)
    bool success = skill->process(request, response);
    
    if (success) {
        log_debug("Processed skill=" + name +
                  " task_id=" + std::to_string(task_id));
    } else {
        log_debug("Failed to process skill=" + name +
                  " task_id=" + std::to_string(task_id));
    }
    
    return success;
}

std::shared_ptr<IPayloadFactory> SkillRegistry::get_payload_factory(uint32_t skill_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = skills_.find(skill_id);
    if (it != skills_.end()) {
        return it->second;
    }
    return nullptr;
}

std::unique_ptr<PayloadBufferBase> SkillRegistry::create_response_buffer(
    uint32_t skill_id,
    std::span<const uint8_t> request
) const {
    auto factory = get_payload_factory(skill_id);
    if (!factory) {
        return nullptr;
    }
    return factory->create_response_buffer_for_request(request);
}

std::unique_ptr<PayloadBufferBase> SkillRegistry::create_test_request_buffer(
    uint32_t skill_id,
    size_t case_index
) const {
    auto factory = get_payload_factory(skill_id);
    if (!factory) {
        return nullptr;
    }
    return factory->create_test_request_buffer(case_index);
}

size_t SkillRegistry::get_test_case_count(uint32_t skill_id) const {
    auto factory = get_payload_factory(skill_id);
    return factory ? factory->get_test_case_count() : 0;
}

VerificationResult SkillRegistry::verify_response(
    uint32_t skill_id,
    std::span<const uint8_t> request,
    std::span<const uint8_t> worker_response
) const {
    auto factory = get_payload_factory(skill_id);
    if (!factory) {
        return VerificationResult::failure("Unknown skill_id=" + std::to_string(skill_id));
    }
    
    return factory->verify_response(request, worker_response);
}

void SkillRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    skills_.clear();
    name_to_id_.clear();
}

void SkillRegistry::set_logger(std::shared_ptr<Logger> logger) {
    std::lock_guard<std::mutex> lock(mutex_);
    logger_ = std::move(logger);
}

void SkillRegistry::log_debug(const std::string& message) {
    if (logger_) {
        logger_->debug("[SkillRegistry] " + message);
    }
}

} // namespace TaskMessenger::Skills
