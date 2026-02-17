// ManagerOptions.cpp - Manager-specific options provider with auto-registration
#include "ManagerOptions.hpp"
#include "options/Options.hpp"

#include <atomic>
#include <mutex>

namespace {
    std::mutex g_manager_opts_mtx;
    bool g_interactive_mode = false;
    std::atomic<bool> g_manager_registered{false};
}

namespace manager_opts {

void register_options() {
    bool expected = false;
    if (!g_manager_registered.compare_exchange_strong(expected, true)) {
        return; // already registered
    }
    
    shared_opts::Options::add_provider([](CLI::App& app, const nlohmann::json& j){
        bool interactive_default = false;
        
        // Check JSON config for interactive mode
        if (j.contains("manager")) {
            const auto& mj = j["manager"];
            if (mj.contains("interactive") && mj["interactive"].is_boolean()) {
                interactive_default = mj["interactive"].get<bool>();
            }
        }
        
        {
            std::lock_guard<std::mutex> lk(g_manager_opts_mtx);
            g_interactive_mode = interactive_default;
        }
        
        // Manager-specific CLI flags
        app.add_flag("--interactive", g_interactive_mode, 
                    "Run manager in interactive mode (prompt for tasks instead of auto-refill)")
            ->group("Manager");
    });
}

bool get_interactive_mode() {
    std::lock_guard<std::mutex> lk(g_manager_opts_mtx);
    return g_interactive_mode;
}

} // namespace manager_opts

// Static auto-registration object
namespace {
    struct ManagerOptsAutoReg {
        ManagerOptsAutoReg() { manager_opts::register_options(); }
    };
    [[maybe_unused]] static ManagerOptsAutoReg s_manager_auto_reg;
}
