#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

namespace shared_opts {
class Options {
public:
    using Provider = std::function<void(CLI::App&, const nlohmann::json&)>;

    enum class ParseResult { Ok, Help, Version, Error };

    static void add_provider(Provider p);
    static ParseResult load_and_parse(int argc, char** argv, std::string& err);
    // Directory of the loaded config file (if any). Useful for resolving relative paths in providers.
    static std::optional<std::filesystem::path> get_config_dir();
    // Full path to loaded config file (if any)
    static std::optional<std::filesystem::path> get_config_file();

private:
    static std::mutex& providers_mutex();
};
}
