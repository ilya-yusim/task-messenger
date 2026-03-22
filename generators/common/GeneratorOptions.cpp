// GeneratorOptions.cpp - Generator-specific options provider with auto-registration
#include "GeneratorOptions.hpp"
#include "options/Options.hpp"

#include <atomic>
#include <mutex>

namespace {
    std::mutex g_gen_opts_mtx;
    bool g_verify_enabled = false;
    double g_verify_epsilon = 1e-9;
    double g_verify_rel_epsilon = 1e-6;
    bool g_verify_inject_failure = false;
    std::atomic<bool> g_gen_registered{false};
}

namespace generator_opts {

void register_options() {
    bool expected = false;
    if (!g_gen_registered.compare_exchange_strong(expected, true)) {
        return; // already registered
    }

    shared_opts::Options::add_provider([](CLI::App& app, const nlohmann::json& j){
        bool verify_default = false;
        double verify_eps_default = 1e-9;
        double verify_rel_eps_default = 1e-6;
        bool verify_inject_failure_default = false;

        // Check JSON config for generator options (reads "manager" section for
        // backward compatibility with existing config files)
        if (j.contains("manager")) {
            const auto& mj = j["manager"];
            if (mj.contains("verify") && mj["verify"].is_boolean()) {
                verify_default = mj["verify"].get<bool>();
            }
            if (mj.contains("verify_epsilon") && mj["verify_epsilon"].is_number()) {
                verify_eps_default = mj["verify_epsilon"].get<double>();
            }
            if (mj.contains("verify_rel_epsilon") && mj["verify_rel_epsilon"].is_number()) {
                verify_rel_eps_default = mj["verify_rel_epsilon"].get<double>();
            }
            if (mj.contains("verify_inject_failure") && mj["verify_inject_failure"].is_boolean()) {
                verify_inject_failure_default = mj["verify_inject_failure"].get<bool>();
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_gen_opts_mtx);
            g_verify_enabled = verify_default;
            g_verify_epsilon = verify_eps_default;
            g_verify_rel_epsilon = verify_rel_eps_default;
            g_verify_inject_failure = verify_inject_failure_default;
        }

        app.add_flag("--verify", g_verify_enabled,
                    "Enable task verification (compare worker results with local computation)")
            ->group("Generator");

        app.add_option("--verify-epsilon", g_verify_epsilon,
                      "Absolute epsilon for numeric comparisons in verification (default: 1e-9)")
            ->group("Generator");

        app.add_option("--verify-rel-epsilon", g_verify_rel_epsilon,
                      "Relative epsilon for numeric comparisons in verification (default: 1e-6)")
            ->group("Generator");

        app.add_flag("--verify-inject-failure", g_verify_inject_failure,
                    "Inject intentional verification failures for testing (corrupts response data)")
            ->group("Generator");
    });
}

bool get_verify_enabled() {
    std::lock_guard<std::mutex> lk(g_gen_opts_mtx);
    return g_verify_enabled;
}

double get_verify_epsilon() {
    std::lock_guard<std::mutex> lk(g_gen_opts_mtx);
    return g_verify_epsilon;
}

double get_verify_rel_epsilon() {
    std::lock_guard<std::mutex> lk(g_gen_opts_mtx);
    return g_verify_rel_epsilon;
}

bool get_verify_inject_failure() {
    std::lock_guard<std::mutex> lk(g_gen_opts_mtx);
    return g_verify_inject_failure;
}

} // namespace generator_opts

// Static auto-registration object
namespace {
    struct GeneratorOptsAutoReg {
        GeneratorOptsAutoReg() { generator_opts::register_options(); }
    };
    [[maybe_unused]] static GeneratorOptsAutoReg s_gen_auto_reg;
}
