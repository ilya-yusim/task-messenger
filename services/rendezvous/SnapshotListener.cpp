#include "SnapshotListener.hpp"

#include "transport/socket/IServerSocket.hpp"
#include "transport/socket/IBlockingStream.hpp"
#include "transport/socket/SocketFactory.hpp"
#include "processUtils.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>

namespace rendezvous {

SnapshotListener::SnapshotListener(std::shared_ptr<Logger> logger,
                                   SnapshotCallback on_snapshot)
    : logger_(std::move(logger)), on_snapshot_(std::move(on_snapshot)) {}

SnapshotListener::~SnapshotListener() { stop(); }

bool SnapshotListener::start(const std::string& listen_host, int listen_port) {
    if (running_.load(std::memory_order_relaxed)) return true;

    auto sock = transport::SocketFactory::create_blocking_server(logger_);
    if (!sock->start_listening(listen_host, listen_port, 16)) {
        if (logger_)
            logger_->error("SnapshotListener: failed to listen on VN "
                           + listen_host + ":" + std::to_string(listen_port));
        return false;
    }
    server_socket_ = std::move(sock);
    running_.store(true, std::memory_order_relaxed);

    accept_thread_ = std::thread([this]() {
        try { ProcessUtils::set_current_thread_name("RV-Snap-Accept"); } catch (...) {}
        accept_loop();
    });

    if (logger_)
        logger_->info("SnapshotListener: VN snapshot relay on "
                      + listen_host + ":" + std::to_string(listen_port));
    return true;
}

void SnapshotListener::stop() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;

    if (server_socket_) {
        server_socket_->shutdown();
    }
    if (accept_thread_.joinable()) accept_thread_.join();
    if (server_socket_) {
        server_socket_->close();
        server_socket_.reset();
    }
    if (logger_) logger_->info("SnapshotListener: stopped");
}

void SnapshotListener::accept_loop() {
    while (running_.load(std::memory_order_relaxed)) {
        try {
            std::error_code ec;
            auto client_socket = server_socket_->accept(ec);
            if (!client_socket) {
                if (ec && running_.load(std::memory_order_relaxed)) {
                    if (logger_)
                        logger_->error("SnapshotListener: accept error: " + ec.message());
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                continue;
            }

            if (!running_.load(std::memory_order_relaxed)) {
                try { client_socket->close(); } catch (...) {}
                break;
            }

            auto client = std::dynamic_pointer_cast<IBlockingStream>(client_socket);
            try {
                handle_connection(*client);
            } catch (const std::exception& ex) {
                if (logger_)
                    logger_->info(std::string("SnapshotListener: connection closed: ")
                                  + ex.what());
            }
            try { client->close(); } catch (...) {}
        } catch (const std::exception& e) {
            if (running_.load(std::memory_order_relaxed) && logger_)
                logger_->error("SnapshotListener: accept exception: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void SnapshotListener::handle_connection(IBlockingStream& stream) {
    // Identity for this connection — populated from the first frame and
    // reused for every subsequent ReportSnapshot bump so the snapshot payload
    // doesn't have to repeat role/name on every tick.
    std::string conn_role = "dispatcher";
    std::string conn_name = "default";
    bool identity_seen = false;

    while (running_.load(std::memory_order_relaxed)) {
        auto [msg_type, body] = read_message(stream);

        if (msg_type != MessageType::ReportSnapshot) {
            if (logger_)
                logger_->warning("SnapshotListener: unexpected message type "
                                 + std::to_string(static_cast<int>(msg_type))
                                 + "; closing connection");
            return;
        }

        if (!identity_seen) {
            try {
                auto j = nlohmann::json::parse(body);
                if (j.contains("role") && j["role"].is_string())
                    conn_role = j["role"].get<std::string>();
                if (j.contains("name") && j["name"].is_string())
                    conn_name = j["name"].get<std::string>();
            } catch (...) {
                // Tolerate non-JSON or missing identity — keep defaults.
            }
            identity_seen = true;
        }

        if (on_snapshot_) {
            try {
                on_snapshot_(conn_role, conn_name, body);
            } catch (const std::exception& ex) {
                if (logger_)
                    logger_->warning(std::string("SnapshotListener: callback error: ")
                                     + ex.what());
            }
        }

        write_message(stream, MessageType::ReportAck, R"({"ok":true})");
    }
}

} // namespace rendezvous
