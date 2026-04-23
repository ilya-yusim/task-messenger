//
// RendezvousHttpHandler.cpp — HTTP dashboard + constructor/destructor
//
// Separated from RendezvousServer.cpp because ZeroTierSockets.h and
// httplib.h both redefine Windows socket symbols and cannot coexist
// in the same translation unit on MSVC.
//

#include <httplib.h>

#include "RendezvousServer.hpp"
#include "SnapshotListener.hpp"
#include "processUtils.hpp"

#include <filesystem>

namespace rendezvous {

// ── Construction / Destruction ──────────────────────────────────────────────

RendezvousServer::RendezvousServer(std::shared_ptr<Logger> logger)
    : logger_(std::move(logger)),
      http_server_(std::make_unique<httplib::Server>()) {
    http_server_->new_task_queue = [] {
        return new httplib::ThreadPool(/*base_threads=*/1, /*max_threads=*/4);
    };
}

RendezvousServer::~RendezvousServer() {
    stop();
}

// ── HTTP helpers called from start() / stop() ───────────────────────────────

bool RendezvousServer::bind_http(const std::string& host, int port) {
    if (!http_server_ || !http_server_->bind_to_port(host, port)) {
        if (logger_)
            logger_->error("RendezvousServer: failed to bind HTTP on "
                           + host + ":" + std::to_string(port));
        return false;
    }
    return true;
}

void RendezvousServer::start_http_thread() {
    http_thread_ = std::thread([this]() {
        try { ProcessUtils::set_current_thread_name("RV-HTTP"); } catch (...) {}
        if (!http_server_->listen_after_bind()) {
            if (running_.load(std::memory_order_relaxed) && logger_)
                logger_->warning("RendezvousServer: HTTP listen loop exited with error");
        }
    });
}

void RendezvousServer::stop_http() noexcept {
    if (http_server_) http_server_->stop();
    if (http_thread_.joinable()) http_thread_.join();
}

// ── HTTP Dashboard ──────────────────────────────────────────────────────────

std::string RendezvousServer::resolve_dashboard_dir() {
#ifdef DASHBOARD_DIR
    std::filesystem::path p(DASHBOARD_DIR);
    if (std::filesystem::is_directory(p)) return p.string();
#endif

    std::error_code ec;
    const auto exe_path = ProcessUtils::get_executable_path();
    if (!exe_path.empty()) {
        // Dev layout: <repo>/builddir/services/rendezvous/<exe>
        //  → three parents up = <repo>, then dashboard/
        auto candidate_dev = exe_path.parent_path().parent_path().parent_path().parent_path()
                             / "dashboard";
        if (std::filesystem::is_directory(candidate_dev, ec)) return candidate_dev.string();

        // Installed layout: exe next to a "dashboard" directory
        auto candidate_installed = exe_path.parent_path() / "dashboard";
        if (std::filesystem::is_directory(candidate_installed, ec)) return candidate_installed.string();
    }
    return {};
}

void RendezvousServer::register_http_routes() {
    if (!http_server_) return;

    // MIME types
    http_server_->set_file_extension_and_mimetype_mapping("html", "text/html; charset=utf-8");
    http_server_->set_file_extension_and_mimetype_mapping("css",  "text/css; charset=utf-8");
    http_server_->set_file_extension_and_mimetype_mapping("js",   "application/javascript; charset=utf-8");
    http_server_->set_file_extension_and_mimetype_mapping("svg",  "image/svg+xml");
    http_server_->set_file_extension_and_mimetype_mapping("ico",  "image/x-icon");
    http_server_->set_file_extension_and_mimetype_mapping("woff2","font/woff2");

    // Mount dashboard static assets
    const auto dashboard_dir = resolve_dashboard_dir();
    if (!dashboard_dir.empty()) {
        if (http_server_->set_mount_point("/", dashboard_dir)) {
            if (logger_) logger_->info("RendezvousServer: dashboard mounted from " + dashboard_dir);
        } else {
            if (logger_) logger_->warning("RendezvousServer: set_mount_point failed for " + dashboard_dir);
        }
    } else {
        if (logger_) logger_->info("RendezvousServer: no dashboard assets found; UI not served");
    }

    // Health check
    http_server_->Get("/healthz", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("ok\n", "text/plain; charset=utf-8");
        res.status = 200;
    });

    // Cached monitoring snapshot
    http_server_->Get("/api/monitor", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(state_mtx_);
        if (last_snapshot_json_.empty()) {
            res.status = 503;
            res.set_content(R"({"error":"no snapshot available"})", "application/json");
            return;
        }
        res.set_header("Cache-Control", "no-store");
        res.set_content(last_snapshot_json_, "application/json");
        res.status = 200;
    });

    // Trailing-slash variant
    http_server_->Get("/api/monitor/", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(state_mtx_);
        if (last_snapshot_json_.empty()) {
            res.status = 503;
            res.set_content(R"({"error":"no snapshot available"})", "application/json");
            return;
        }
        res.set_header("Cache-Control", "no-store");
        res.set_content(last_snapshot_json_, "application/json");
        res.status = 200;
    });

    // Error handler
    http_server_->set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status == 0) res.status = 404;
        if (res.body.empty()) res.set_content("not found\n", "text/plain; charset=utf-8");
    });

    http_server_->set_exception_handler([this](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& ex) {
            if (logger_) logger_->warning("RendezvousServer: HTTP handler exception: " + std::string(ex.what()));
        } catch (...) {
            if (logger_) logger_->warning("RendezvousServer: HTTP handler exception (unknown)");
        }
        res.status = 500;
        res.set_content("internal error\n", "text/plain; charset=utf-8");
    });
}

} // namespace rendezvous
