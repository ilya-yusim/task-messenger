#include "VerificationHelper.hpp"
#include "skills/registry/CompareUtils.hpp"
#include "skills/registry/SkillRegistry.hpp"

#include <iostream>

using namespace TaskMessenger::Skills;

bool VerificationHelper::is_enabled() {
    return CompareConfig::defaults().enabled;
}

std::unique_ptr<PayloadBufferBase>
VerificationHelper::clone_request(const PayloadBufferBase& request) {
    if (!is_enabled()) {
        return nullptr;
    }
    return request.clone();
}

VerificationResult VerificationHelper::verify(
    uint32_t task_id,
    uint32_t skill_id,
    std::span<const uint8_t> request,
    std::span<const uint8_t> response)
{
    auto result = SkillRegistry::instance().verify_response(
        skill_id, request, response);

    if (!result.passed) {
        std::cerr << "[VERIFY FAIL] Task " << task_id
                  << " (skill " << skill_id << "): "
                  << result.message << "\n";
    }

    return result;
}
