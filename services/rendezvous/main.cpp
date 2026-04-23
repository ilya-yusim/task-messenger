#include "RendezvousServer.hpp"
#include "rendezvous/RendezvousOptions.hpp"
#include "options/Options.hpp"
#include "logger.hpp"

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

namespace {
std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int) {
    g_shutdown_requested.store(true, std::memory_order_relaxed);
}
} // namespace

int main(int argc, char* argv[]) {
    // --- Logging ---
    auto logger = std::make_shared<Logger>("Rendezvous");
    auto stdout_sink = std::make_shared<StdoutSink>();
    stdout_sink->set_level(LogLevel::Info);
    logger->add_sink(stdout_sink);

    // --- Parse options ---
    std::string opts_err;
    auto res = shared_opts::Options::load_and_parse(argc, argv, opts_err);
    if (res == shared_opts::Options::ParseResult::Help ||
        res == shared_opts::Options::ParseResult::Version) {
        return 0;
    }
    if (res == shared_opts::Options::ParseResult::Error) {
        std::cerr << "Error: " << opts_err << "\n";
        return 1;
    }

    // --- Build server config from parsed options ---
    rendezvous::RendezvousServer::Config cfg;
    if (auto h = rendezvous_opts::get_host(); h && !h->empty()) cfg.vn_listen_host = *h;
    if (auto p = rendezvous_opts::get_port(); p) cfg.vn_listen_port = *p;
    if (auto dh = rendezvous_opts::get_dashboard_host(); dh && !dh->empty()) cfg.http_listen_host = *dh;
    if (auto dp = rendezvous_opts::get_dashboard_port(); dp) cfg.http_listen_port = *dp;
    if (auto sh = rendezvous_opts::get_snapshot_listen_host(); sh && !sh->empty()) cfg.snapshot_listen_host = *sh;
    if (auto sp = rendezvous_opts::get_snapshot_port(); sp) cfg.snapshot_listen_port = *sp;

    // --- Start ---
    rendezvous::RendezvousServer server(logger);
    if (!server.start(cfg)) {
        logger->error("Failed to start rendezvous server");
        return 2;
    }

    // --- Install signal handlers ---
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);

    logger->info("Rendezvous service running.  Press Ctrl+C to stop.");

    // --- Block until signal ---
    while (!g_shutdown_requested.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    logger->info("Shutdown requested, stopping...");
    server.stop();
    return 0;
}
