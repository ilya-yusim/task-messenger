
#include <httplib.h>

#include "MonitoringService.hpp"

#include "MonitoringOptions.hpp"

#include <nlohmann/json.hpp>

#include "processUtils.hpp"
#include <thread>

namespace monitoring {

MonitoringService::MonitoringService(std::shared_ptr<Logger> logger,
                                     AsyncTransportServer& server,
                                     MonitoringSnapshotBuilder::UptimeProvider uptime_provider)
    : logger_(std::move(logger)),
      server_(server),
      snapshot_builder_(logger_, server_, std::move(uptime_provider)),
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

    if (acceptor_thread_.joinable()) {
        acceptor_thread_.join();
    }
}

bool MonitoringService::is_running() const noexcept {
    return running_.load(std::memory_order_relaxed);
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

void MonitoringService::register_routes() {
    if (!http_server_) {
        return;
    }

    http_server_->Get("/healthz", [this](const httplib::Request&, httplib::Response& res) {
        (void)this;
        res.set_content("ok\n", "text/plain; charset=utf-8");
        res.status = 200;
    });

    http_server_->Get("/api/monitor", [this](const httplib::Request&, httplib::Response& res) {
        const auto snapshot = snapshot_builder_.build();
        nlohmann::json body_json = snapshot;
        res.set_header("Cache-Control", "no-store");
        res.set_content(body_json.dump(), "application/json");
        res.status = 200;
    });

    // Accept a trailing slash variant for dashboard convenience.
    http_server_->Get("/api/monitor/", [this](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        const auto snapshot = snapshot_builder_.build();
        nlohmann::json body_json = snapshot;
        res.set_header("Cache-Control", "no-store");
        res.set_content(body_json.dump(), "application/json");
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
