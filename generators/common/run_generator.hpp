/**
 * \file run_generator.hpp
 * \brief Thin launcher that eliminates boilerplate across generator mains.
 */
#pragma once

#include "IGenerator.hpp"
#include "GeneratorOptions.hpp"
#include "dispatcher/DispatcherApp.hpp"
#include "skills/registry/SkillRegistry.hpp"
#include "skills/registry/CompareUtils.hpp"

/**
 * \brief Launch a generator with standard dispatcher lifecycle.
 *
 * Handles: DispatcherApp boot, skill registry logging, verification config,
 * generator initialize/run/shutdown, and clean teardown.
 *
 * \param argc From main()
 * \param argv From main()
 * \param generator The generator algorithm to run
 * \return Process exit code
 *
 * Usage:
 * \code
 * int main(int argc, char* argv[]) {
 *     MyGenerator gen;
 *     return run_generator(argc, argv, gen);
 * }
 * \endcode
 */
inline int run_generator(int argc, char* argv[], IGenerator& generator) {
    DispatcherApp app;
    int rc = app.start(argc, argv);
    if (rc != 0) {
        return (rc == 1) ? 0 : rc;
    }

    auto logger = app.logger();

    // Log registered skills
    auto& registry = TaskMessenger::Skills::SkillRegistry::instance();
    logger->info("Registered skills: " + std::to_string(registry.skill_count()));

    // Configure verification from generator options
    auto& cfg = TaskMessenger::Skills::CompareConfig::defaults();
    cfg.enabled = generator_opts::get_verify_enabled();
    cfg.abs_epsilon = generator_opts::get_verify_epsilon();
    cfg.rel_epsilon = generator_opts::get_verify_rel_epsilon();
    cfg.inject_failure = generator_opts::get_verify_inject_failure();

    if (!generator.initialize(app)) {
        logger->error("Generator initialization failed");
        app.stop();
        return 1;
    }

    int result = generator.run(app);

    generator.on_shutdown();

    app.stop();
    return result;
}
