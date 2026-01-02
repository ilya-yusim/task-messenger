// managerMain.cpp - Main AsyncTransportServer application for asynchronous transfer of tasks to workers.
#include "transport/AsyncTransportServer.hpp"
#include "TaskGenerator.hpp"
#include "options/Options.hpp"

static uint32_t user_prompt(AsyncTransportServer &server); // forward declaration

int main(int argc, char* argv[]) {

    // --- Stage 1: Build logging pipeline ---
    auto logger = std::make_shared<Logger>("AsyncTransportServer");
    auto stdout_sink = std::make_shared<StdoutSink>();
    stdout_sink->set_level(LogLevel::Info); // Set to Info for general output
    logger->add_sink(stdout_sink);

    try {

        // --- Stage 2: Parse CLI/JSON options ---

        // Note, all options auto-register via static objects; no manual call needed
        // Parse the command line and JSON config once for the entire process
        std::string opts_err;
        auto parse_res = shared_opts::Options::load_and_parse(argc, argv, opts_err);
        if (parse_res == shared_opts::Options::ParseResult::Help || parse_res == shared_opts::Options::ParseResult::Version) {
            return 0; // help/version already printed
        } else if (parse_res == shared_opts::Options::ParseResult::Error) {
            logger->error(std::string("Failed to parse options: ") + opts_err);
            return 2;
        }

        // --- Stage 3: Bring up transport server subsystem ---
        logger->info("Async Transport Server starting...");

        AsyncTransportServer server(logger);
        if (!server.start()) {
            logger->error("Failed to start Async Transport Server");
            return 3;
        }
        logger->info("Async Transport Server started successfully");

        // --- Stage 4: Interactive loop (prompt → generate → enqueue) ---
        DefaultTaskGenerator generator;
        while (true) {

            // Prompt user for number of tasks
            uint32_t refill = user_prompt(server);
            if (refill == 0) {
                server.stop();
                break;
            }

            // Generate tasks
            auto tasks = generator.make_tasks(refill);

            // Enqueue tasks to transport server
            server.enqueue_tasks(std::move(tasks));
        }

    } catch (const std::exception& e) {
        logger->error("Exception in Async Transport Server main loop: " + std::string(e.what()));
        return 1;
    }

    // --- Stage 5: Final shutdown log ---
    logger->info("Async Transport Server shutting down...");

    return 0;
}

// Prompts the user for the next batch size or commands like stats/quit.
// Parses input robustly and returns the desired number of tasks to generate.
static uint32_t user_prompt(AsyncTransportServer &server)
{
    static std::atomic<uint32_t> custom_refill_amount{25};

    std::cout << "\n=== TASK POOL MANAGEMENT ===\n";
    std::cout << "How many tasks would you like to generate?\n";

    std::cout << "Current default amount: " << custom_refill_amount.load() << "\n";
    std::cout << "Options:\n";
    std::cout << "  1. Press Enter - Use default amount (" << custom_refill_amount.load() << ")\n";
    std::cout << "  2. Enter number - Use custom amount (1-1000000)\n";
    std::cout << "  3. Type 'q' or 'quit' - Gracefully shutdown manager\n";
    std::cout << "  4. Type 'set <number>' - Set new default and use it\n";
    std::cout << "  5. Type 's' or 'stats' - Print comprehensive statistics\n";

    for (;;)
    {
        std::cout << "Choice: ";
        std::cout.flush();

        std::string input;
        if (!std::getline(std::cin, input))
        {
            // EOF or input error, shutdown gracefully
            return 0;
        }

        // Trim whitespace
        input.erase(0, input.find_first_not_of(" \t\r\n"));
        input.erase(input.find_last_not_of(" \t\r\n") + 1);

        if (input.empty())
        {
            // Use default
            return custom_refill_amount.load();
        }

        if (input == "q" || input == "quit" || input == "exit")
        {
            return 0; // Signal shutdown
        }

        if (input == "s" || input == "stats")
        {
            server.print_transporter_statistics();
            continue; // re-prompt
        }

        // Check for 'set' command
        if (input.rfind("set ", 0) == 0)
        {
            std::string number_str = input.substr(4);
            try
            {
                uint32_t new_default = std::stoul(number_str);
                if (new_default > 0 && new_default <= 1000)
                {
                    custom_refill_amount.store(new_default);
                    std::cout << "New default refill amount set to: " << new_default << "\n";
                    return new_default;
                }
                else
                {
                    std::cout << "Invalid amount. Using current default: " << custom_refill_amount.load() << "\n";
                    return custom_refill_amount.load();
                }
            }
            catch (...)
            {
                std::cout << "Invalid number. Using current default: " << custom_refill_amount.load() << "\n";
                return custom_refill_amount.load();
            }
        }

        // Try to parse as number
        try
        {
            uint32_t amount = std::stoul(input);
            if (amount > 0 && amount <= 1000000)
            {
                return amount;
            }
            else
            {
                std::cout << "Invalid amount (must be 1-1000000). Using default: " << custom_refill_amount.load() << "\n";
                return custom_refill_amount.load();
            }
        }
        catch (...)
        {
            std::cout << "Invalid input. Type a number, 'set <n>', 's'/'stats', or 'q'/'quit'. Using default: " << custom_refill_amount.load() << "\n";
            return custom_refill_amount.load();
        }
    }
}
