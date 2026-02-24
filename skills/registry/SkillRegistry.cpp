/**
 * @file skills/registry/SkillRegistry.cpp
 * @brief Implementation of the unified SkillRegistry.
 *
 * Skills self-register via static initialization using REGISTER_SKILL macro.
 * See SkillRegistration.hpp and skills/builtins/ for examples.
 */
#include "SkillRegistry.hpp"
#include "PayloadBuffer.hpp"
#include "logger.hpp"

namespace TaskMessenger::Skills {

SkillRegistry& SkillRegistry::instance() {
    static SkillRegistry instance;
    return instance;
}

SkillRegistry::SkillRegistry(std::shared_ptr<Logger> logger)
    : logger_(std::move(logger))
{
}

void SkillRegistry::register_skill(SkillDescriptor descriptor) {
    std::lock_guard<std::mutex> lock(mutex_);
    skills_[descriptor.id] = std::move(descriptor);
}

bool SkillRegistry::has_skill(uint32_t skill_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return skills_.find(skill_id) != skills_.end();
}

std::string SkillRegistry::get_skill_name(uint32_t skill_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = skills_.find(skill_id);
    if (it != skills_.end()) {
        return it->second.name;
    }
    return "";
}

std::vector<uint32_t> SkillRegistry::skill_ids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint32_t> result;
    result.reserve(skills_.size());
    for (const auto& [id, desc] : skills_) {
        result.push_back(id);
    }
    return result;
}

size_t SkillRegistry::skill_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return skills_.size();
}

std::unique_ptr<PayloadBufferBase> SkillRegistry::dispatch(
    uint32_t skill_id,
    uint32_t task_id,
    std::span<const uint8_t> payload
) {
    // Find skill under lock, get handler pointer
    ISkillHandler* handler = nullptr;
    std::string skill_name;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = skills_.find(skill_id);
        if (it == skills_.end() || !it->second.handler) {
            log_debug("Unknown skill_id=" + std::to_string(skill_id) +
                      " for task_id=" + std::to_string(task_id));
            return nullptr;
        }
        handler = it->second.handler.get();
        skill_name = it->second.name;
    }
    
    // Process outside lock (handler execution may be slow)
    auto response = handler->process(payload);
    
    if (response) {
        log_debug("Processed skill=" + skill_name +
                  " task_id=" + std::to_string(task_id));
    } else {
        log_debug("Failed to process skill=" + skill_name +
                  " task_id=" + std::to_string(task_id));
    }
    
    return response;
}

IPayloadFactory* SkillRegistry::get_payload_factory(uint32_t skill_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = skills_.find(skill_id);
    if (it != skills_.end() && it->second.payload_factory) {
        return it->second.payload_factory.get();
    }
    return nullptr;
}

void SkillRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    skills_.clear();
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
