#pragma once

#include "generators/common/IGenerator.hpp"
#include "generators/common/TaskGenerator.hpp"

class AutoRefillGenerator : public IGenerator {
public:
    bool initialize(DispatcherApp& app) override;
    int run(DispatcherApp& app) override;
    void on_shutdown() override;

private:
    TaskGenerator task_gen_;
};
