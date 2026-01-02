#include "Options.hpp"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <utility>
#include <iostream>

namespace shared_opts {

struct ProviderHolder { std::function<void(CLI::App&, const nlohmann::json&)> cb; };

static std::vector<ProviderHolder>& providers() {
    static std::vector<ProviderHolder> p;
    return p;
}

// Store the loaded config file path (if any) so that option providers can resolve relative paths.
static std::optional<std::filesystem::path>& loaded_config_file_storage() {
    static std::optional<std::filesystem::path> p; return p;
}

std::mutex& Options::providers_mutex() {
    static std::mutex m;
    return m;
}

void Options::add_provider(Provider p) {
    std::lock_guard<std::mutex> lk(providers_mutex());
    providers().push_back(ProviderHolder{std::move(p)});
}

Options::ParseResult Options::load_and_parse(int argc, char** argv, std::string& err) {
    CLI::App app{"task-messenger"};
    app.set_version_flag("-V,--version", std::string{"task-messenger 0.1"});

    std::string config_file;
    app.add_option("-c,--config", config_file, "JSON config file to load")->group("General");

    // Allow extra args temporarily while we probe for the config file path.
    // We'll do a strict parse on the real app after option providers are registered.
    app.allow_extras(true);

    // Minimal pre-parser to discover -c/--config early.
    // This lets us load the JSON config and register provider options that may
    // depend on it before we run the full parse.
    CLI::App config_probe{"config_probe"};
    config_probe.add_option("-c,--config", config_file);
    // Accept unknown arguments during probing so we don't throw on options
    // that the real app (with providers) will add later.
    config_probe.allow_extras(true);
    try { config_probe.parse(argc, argv); } catch(...) {}

    nlohmann::json cfg_json;
    if (!config_file.empty()) {
        std::ifstream ifs(config_file);
        if (ifs) {
            try { ifs >> cfg_json; } catch(...) { /* ignore malformed */ }
            // Store absolute path for later retrieval.
            try {
                std::filesystem::path abs = std::filesystem::absolute(config_file);
                loaded_config_file_storage() = abs;
            } catch(...) {
                // Ignore filesystem errors; leave unset.
            }
        }
    } else {
        loaded_config_file_storage().reset();
    }

    {
        std::lock_guard<std::mutex> lk(providers_mutex());
        for (auto &ph : providers()) {
            if (ph.cb) ph.cb(app, cfg_json);
        }
    }

    app.allow_extras(false);
    app.require_subcommand(0);
    try {
        app.parse(argc, argv);
        return ParseResult::Ok;
    } catch (const CLI::CallForHelp &) {
        std::cout << app.help() << std::endl;
        return ParseResult::Help;
    } catch (const CLI::CallForAllHelp &) {
        std::cout << app.help() << std::endl;
        return ParseResult::Help;
    } catch (const CLI::CallForVersion &v) {
        std::cout << v.what() << std::endl;
        return ParseResult::Version;
    } catch (const std::exception &e) {
        err = e.what();
        return ParseResult::Error;
    }
}

std::optional<std::filesystem::path> Options::get_config_dir() {
    auto &s = loaded_config_file_storage();
    if (s && s->has_parent_path()) return s->parent_path();
    return std::nullopt;
}

std::optional<std::filesystem::path> Options::get_config_file() {
    return loaded_config_file_storage();
}

}
