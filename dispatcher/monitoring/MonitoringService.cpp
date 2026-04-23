
#include <httplib.h>

#include "MonitoringService.hpp"

#include "MonitoringOptions.hpp"
#include "SnapshotReporter.hpp"

#include <nlohmann/json.hpp>

#include "processUtils.hpp"
#include <chrono>
#include <filesystem>
#include <thread>

namespace monitoring {

MonitoringService::MonitoringService(std::shared_ptr<Logger> logger,
                                     AsyncTransportServer& server,
                         MonitoringSnapshotBuilder::UptimeProvider uptime_provider,
                         MonitoringSnapshotBuilder::DispatcherStateProvider dispatcher_state_provider)
    : logger_(std::move(logger)),
      server_(server),
    snapshot_builder_(logger_, server_, std::move(uptime_provider), std::move(dispatcher_state_provider)),
            http_server_(std::make_unique<httplib::Server>()) {
        // Override cpp-httplib default pool sizing to keep monitoring overhead minimal.
        http_server_->new_task_queue = [] {
                return new httplib::ThreadPool(/*base_threads=*/1, /*max_threads=*/4);
        };
}

MonitoringService::~MonitoringService() {
    stop();
}

bool MonitoringService::start() {
    if (running_.load(std::memory_order_relaxed)) {
        return true;
    }

    // Defaults are local-host friendly for the prototype browser deployment.
    listen_host_ = monitoring_opts::get_listen_host().value_or(std::string("127.0.0.1"));
    listen_port_ = monitoring_opts::get_listen_port().value_or(9090);
    snapshot_interval_ms_ = monitoring_opts::get_snapshot_interval_ms().value_or(1000);
    register_routes();

    if (!http_server_ || !http_server_->bind_to_port(listen_host_, listen_port_)) {
        if (logger_) {
            logger_->error("MonitoringService: failed to listen on " + listen_host_ + ":" +
                           std::to_string(listen_port_));
        }
        return false;
    }

    running_.store(true, std::memory_order_relaxed);
    // Dedicated acceptor thread keeps monitoring traffic isolated from dispatcher core loops.
    acceptor_thread_ = std::thread([this]() {
        try {
            ProcessUtils::set_current_thread_name("MonitoringAcceptor");
        } catch (...) {
        }
        accept_loop();
    });

    // Background reporter relays snapshots to the rendezvous server on a timer so
    // the rendezvous dashboard stays fresh independently of dispatcher HTTP polls.
    report_thread_ = std::thread([this]() {
        try {
            ProcessUtils::set_current_thread_name("MonitoringReporter");
        } catch (...) {
        }
        report_loop();
    });

    if (logger_) {
        logger_->info("MonitoringService: listening on " + listen_host_ + ":" +
                      std::to_string(listen_port_));
    }

    return true;
}

void MonitoringService::stop() noexcept {
    // Stop flag is observed by accept loop between timed accept attempts.
    running_.store(false, std::memory_order_relaxed);

    if (http_server_) {
        http_server_->stop();
    }

    // Wake reporter thread so it can observe the stop flag promptly.
    {
        std::lock_guard<std::mutex> lk(rv_mtx_);
    }
    rv_cv_.notify_all();

    if (acceptor_thread_.joinable()) {
        acceptor_thread_.join();
    }
    if (report_thread_.joinable()) {
        report_thread_.join();
    }
}

bool MonitoringService::is_running() const noexcept {
    return running_.load(std::memory_order_relaxed);
}

void MonitoringService::set_snapshot_reporter(
        std::shared_ptr<SnapshotReporter> reporter) {
    {
        std::lock_guard<std::mutex> lk(rv_mtx_);
        snapshot_reporter_ = std::move(reporter);
    }
    rv_cv_.notify_all();
}

void MonitoringService::accept_loop() {
    if (!http_server_) {
        return;
    }

    // listen_after_bind blocks until stop() is called.
    if (!http_server_->listen_after_bind()) {
        if (running_.load(std::memory_order_relaxed) && logger_) {
            logger_->warning("MonitoringService: listen loop exited with error");
        }
    }
}

void MonitoringService::report_loop() {
    const auto interval = std::chrono::milliseconds(
        snapshot_interval_ms_ > 0 ? snapshot_interval_ms_ : 1000);

    while (running_.load(std::memory_order_relaxed)) {
        // Build once per tick and publish into the cache so HTTP handlers and
        // the rendezvous relay both share a single build() invocation per interval.
        std::shared_ptr<const std::string> payload;
        try {
            payload = build_and_cache_snapshot();
        } catch (const std::exception& ex) {
            if (logger_) {
                logger_->warning(std::string("MonitoringService: snapshot build failed: ") + ex.what());
            }
        } catch (...) {
            if (logger_) {
                logger_->warning("MonitoringService: snapshot build failed (unknown)");
            }
        }

        // Forward the freshly cached payload to the rendezvous server if attached.
        std::shared_ptr<SnapshotReporter> reporter;
        {
            std::unique_lock<std::mutex> lk(rv_mtx_);
            rv_cv_.wait_for(lk, interval, [this]() {
                return !running_.load(std::memory_order_relaxed);
            });
            if (!running_.load(std::memory_order_relaxed)) return;
            reporter = snapshot_reporter_;
        }

        if (reporter && payload) {
            try {
                reporter->report(*payload);
            } catch (const std::exception& ex) {
                if (logger_) {
                    logger_->warning(std::string("MonitoringService: snapshot relay failed: ") + ex.what());
                }
            } catch (...) {
                if (logger_) {
                    logger_->warning("MonitoringService: snapshot relay failed (unknown)");
                }
            }
        }
    }
}

std::shared_ptr<const std::string> MonitoringService::build_and_cache_snapshot() {
    const auto snapshot = snapshot_builder_.build();
    nlohmann::json body_json = snapshot;
    auto payload = std::make_shared<const std::string>(body_json.dump());
    {
        std::lock_guard<std::mutex> lk(cache_mtx_);
        cached_snapshot_json_ = payload;
    }
    return payload;
}

std::shared_ptr<const std::string> MonitoringService::get_or_build_cached_snapshot() {
    {
        std::lock_guard<std::mutex> lk(cache_mtx_);
        if (cached_snapshot_json_) return cached_snapshot_json_;
    }
    // No snapshot cached yet (first request before first tick) — build once.
    return build_and_cache_snapshot();
}

std::string MonitoringService::resolve_dashboard_dir() {
#ifdef DASHBOARD_DIR
    // Priority 1: compile-time override (absolute path injected by Meson).
    std::filesystem::path p(DASHBOARD_DIR);
    if (std::filesystem::is_directory(p)) {
        return p.string();
    }
#endif

    // Priority 2: locate executable and walk up to dev layout
    // (<builddir>/generators/<name>/<exe> → <repo>/dashboard).
    std::error_code ec;
    const auto exe_path = ProcessUtils::get_executable_path();

    if (!exe_path.empty()) {
        // Dev layout: <repo>/builddir/generators/<name>/<exe>
        //  -> four parents up from exe path = <repo>, then dashboard/
        auto candidate_dev = exe_path.parent_path().parent_path().parent_path().parent_path()
                             / "dashboard";
        if (std::filesystem::is_directory(candidate_dev, ec)) {
            return candidate_dev.string();
        }

        // Installed layout: exe next to a "dashboard" directory.
        auto candidate_installed = exe_path.parent_path() / "dashboard";
        if (std::filesystem::is_directory(candidate_installed, ec)) {
            return candidate_installed.string();
        }
    }

    return {};
}

void MonitoringService::register_routes() {
    if (!http_server_) {
        return;
    }

    // Register explicit MIME mappings so browsers execute JS and CSS correctly.
    http_server_->set_file_extension_and_mimetype_mapping("html", "text/html; charset=utf-8");
    http_server_->set_file_extension_and_mimetype_mapping("css",  "text/css; charset=utf-8");
    http_server_->set_file_extension_and_mimetype_mapping("js",   "application/javascript; charset=utf-8");
    http_server_->set_file_extension_and_mimetype_mapping("svg",  "image/svg+xml");
    http_server_->set_file_extension_and_mimetype_mapping("ico",  "image/x-icon");
    http_server_->set_file_extension_and_mimetype_mapping("woff2","font/woff2");

    // Mount dashboard static assets at root. Non-fatal if directory is absent
    // (API routes remain fully operational).
    const auto dashboard_dir = resolve_dashboard_dir();
    if (!dashboard_dir.empty()) {
        if (http_server_->set_mount_point("/", dashboard_dir)) {
            if (logger_) {
                logger_->info("MonitoringService: dashboard assets mounted from " + dashboard_dir);
            }
        } else {
            if (logger_) {
                logger_->warning("MonitoringService: set_mount_point failed for " + dashboard_dir);
            }
        }
    } else {
        if (logger_) {
            logger_->info("MonitoringService: no dashboard assets directory found; UI not served");
        }
    }

    http_server_->Get("/healthz", [this](const httplib::Request&, httplib::Response& res) {
        (void)this;
        res.set_content("ok\n", "text/plain; charset=utf-8");
        res.status = 200;
    });

    http_server_->Get("/api/monitor", [this](const httplib::Request&, httplib::Response& res) {
        auto payload = get_or_build_cached_snapshot();
        res.set_header("Cache-Control", "no-store");
        res.set_content(*payload, "application/json");
        res.status = 200;
    });

    // Accept a trailing slash variant for dashboard convenience.
    http_server_->Get("/api/monitor/", [this](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        auto payload = get_or_build_cached_snapshot();
        res.set_header("Cache-Control", "no-store");
        res.set_content(*payload, "application/json");
        res.status = 200;
    });

    http_server_->set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status == 0) {
            res.status = 404;
        }
        if (res.body.empty()) {
            res.set_content("not found\n", "text/plain; charset=utf-8");
        }
    });

    http_server_->set_exception_handler([this](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
        try {
            if (ep) {
                std::rethrow_exception(ep);
            }
        } catch (const std::exception& ex) {
            if (logger_) {
                logger_->warning(std::string("MonitoringService: handler exception: ") + ex.what());
            }
        } catch (...) {
            if (logger_) {
                logger_->warning("MonitoringService: handler exception (unknown)");
            }
        }

        res.status = 500;
        res.set_content("internal error\n", "text/plain; charset=utf-8");
    });
}

} // namespace monitoring
