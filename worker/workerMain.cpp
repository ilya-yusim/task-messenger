/**
 * \file worker/workerMain.cpp
 * \brief Entrypoint for the worker process, bridging runtime selection and optional UI.
 */

#ifdef HAS_WORKER_UI
#include "worker/ui/WorkerUI.hpp"
#endif
#include "session/WorkerSession.hpp"
#include "WorkerOptions.hpp"
#include "skills/registry/SkillRegistry.hpp"
#include "logger.hpp"
#include <options/Options.hpp>
#include <processUtils.hpp>
#include <iostream>

/** \brief Entrypoint for the worker binary. */
int main(int argc, char* argv[]) {
    try {
        std::string opt_err;
        auto parse_res = shared_opts::Options::load_and_parse(argc, argv, opt_err);
        if (parse_res == shared_opts::Options::ParseResult::Help || parse_res == shared_opts::Options::ParseResult::Version) {
            return 0;
        }
        if (parse_res == shared_opts::Options::ParseResult::Error) {
            std::cerr << "worker option parse error: " << opt_err << std::endl;
            return 2;
        }

        // Determine mode
        WorkerMode mode = WorkerMode::Blocking;
        {
            auto mode_opt = transport::worker_opts::get_worker_mode();
            if (mode_opt && (*mode_opt == std::string{"async"})) mode = WorkerMode::Async;
        }

        // Obtain worker connection options
        namespace wo = transport::worker_opts;
        std::string manager_host = wo::get_manager_host().value_or("localhost");
        int manager_port = wo::get_manager_port().value_or(8080);

        // Setup logger
        auto logger = std::make_shared<Logger>("Worker");
        
        // Add vector sink if UI enabled (and UI compiled) for log retrieval
        bool ui_enabled = transport::worker_opts::get_ui_enabled().value_or(false);
    #ifndef HAS_WORKER_UI
        if (ui_enabled) {
                logger->warning("UI requested but FTXUI not available; running headless (byte stats still logged).");
        }
        ui_enabled = false; // force headless when UI not built
    #endif

        if (ui_enabled) {
            auto vec_sink = std::make_shared<VectorSink>();
            vec_sink->set_level(LogLevel::Info);
            logger->add_sink(vec_sink);
        } else {
            auto stdout_sink = std::make_shared<StdoutSink>();
            stdout_sink->set_level(LogLevel::Info);
            logger->add_sink(stdout_sink);
        }

        // Log registered skills (verifies static initialization worked)
        auto& skill_registry = TaskMessenger::Skills::SkillRegistry::instance();
        logger->info("Registered skills: " + std::to_string(skill_registry.skill_count()));
     
        // Start worker session directly
        WorkerOptions opts{mode, manager_host, manager_port};
        auto session = std::make_shared<WorkerSession>(opts, logger);

        if (!ui_enabled) {
            session->start();
            return 0;
        }

        // Run with UI (UI manages runtime thread internally)
#ifdef HAS_WORKER_UI
        try {
            auto ui = std::make_shared<WorkerUI>(session, logger);
            ui->Run();
        } catch (const std::exception& e) {
            logger->error(std::string{"UI error: "} + e.what());
        }
#endif
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "worker error: " << e.what() << std::endl;
        return 1;
    }
}
