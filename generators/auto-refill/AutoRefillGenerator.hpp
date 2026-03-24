#pragma once

#include "generators/common/IGenerator.hpp"
#include "generators/common/SkillTestIterator.hpp"
#include "generators/common/TaskGenerator.hpp"
#include "generators/common/VerificationHelper.hpp"

#include <memory>

class AutoRefillGenerator : public IGenerator {
public:
    bool initialize(DispatcherApp& app) override;
    int run(DispatcherApp& app) override;
    void on_shutdown() override;

private:
    TaskGenerator task_gen_;
    std::unique_ptr<SkillTestIterator> iterator_;
    VerificationHelper verifier_;
};
