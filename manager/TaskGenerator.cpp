#include "TaskGenerator.hpp"

#include <utility>

/** \ingroup task_messenger_manager */
void DefaultTaskGenerator::generate_tasks(std::shared_ptr<TaskMessagePool> pool, uint32_t count) {
    if (!pool || stopped_.load()) {
        return;
    }

    auto tasks = make_tasks(count);
    if (!tasks.empty()) {
        pool->add_tasks(std::move(tasks));
    }
}

/** \ingroup task_messenger_manager */
std::vector<TaskMessage> DefaultTaskGenerator::make_tasks(uint32_t count) {
    std::vector<TaskMessage> out;
    if (stopped_.load() || count == 0) {
        return out;
    }

    out.reserve(count);
    for (uint32_t i = 0; i < count && !stopped_.load(); ++i) {
        uint32_t task_id = task_id_generator_.get_next_id();
        uint32_t task_type = (i % 3) + 1; // Cycle through task types 1-3
        std::string task_data = generate_task_data(task_id, task_type);

        // header and payload are serialized here, through TaskMessage constructor
        out.emplace_back(task_id, task_type, std::move(task_data));
    }

    return out;
}

/** \ingroup task_messenger_manager */
void DefaultTaskGenerator::stop() {
    stopped_.store(true);
}

/** \ingroup task_messenger_manager */
std::string DefaultTaskGenerator::generate_task_data(uint32_t task_id, uint32_t task_type) {
    if (task_type == 2) {
        return std::to_string(task_id);
    }
    return "Task data " + std::to_string(task_id);
}
