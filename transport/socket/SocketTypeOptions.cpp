/**
 * \file SocketTypeOptions.cpp
 * \brief CLI/config option registration for socket backend type.
 * \ingroup socket_backend
 * \details Registers a provider that captures user-specified backend type and
 * exposes it to the factory for resolution. Idempotent registration guarded
 * by atomic flag.
 */
#include "SocketFactory.hpp"
#include <options/Options.hpp>
#include <optional>
#include <mutex>
#include <atomic>

namespace
{
    std::mutex g_socket_type_mtx;
    std::optional<std::string> g_socket_type_str; // e.g. "zerotier"
    std::atomic<bool> g_socket_type_registered{false};
}

namespace transport
{
    namespace socket_opts
    {
        /** \brief Register backend selection options (once). */
        void register_options()
        {
            bool expected = false;
            if (!g_socket_type_registered.compare_exchange_strong(expected, true))
                return;
            shared_opts::Options::add_provider([](CLI::App &app, const nlohmann::json &j)
                                             {
        std::string type_default = "zerotier";
        if (j.contains("sockets") && j["sockets"].contains("type") && j["sockets"]["type"].is_string()) {
            type_default = j["sockets"]["type"].get<std::string>();
        }
        {
            std::lock_guard<std::mutex> lk(g_socket_type_mtx);
            g_socket_type_str = type_default;
        }
        app.add_option("--socket-type", g_socket_type_str, "Socket backend type (zerotier)")->group("Sockets"); });
        }

        /** \brief Retrieve raw backend type string chosen by user. */
        std::optional<std::string> get_socket_type_raw()
        {
            std::lock_guard<std::mutex> lk(g_socket_type_mtx);
            return g_socket_type_str;
        }

    }
} // namespace transport::socket_opts

namespace
{
    struct SocketTypeOptsAutoReg
    {
        SocketTypeOptsAutoReg() { transport::socket_opts::register_options(); }
    } socket_type_opts_auto_reg_instance; // NOLINT(cert-err58-cpp)
}
