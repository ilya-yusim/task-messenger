#include "SnapshotReporter.hpp"

#include "rendezvous/RendezvousProtocol.hpp"
#include "transport/socket/IBlockingStream.hpp"
#include "transport/socket/SocketFactory.hpp"

namespace monitoring {

SnapshotReporter::SnapshotReporter(std::string host, int port,
                                   std::shared_ptr<Logger> logger)
    : host_(std::move(host)), port_(port), logger_(std::move(logger)) {}

SnapshotReporter::~SnapshotReporter() { cancel(); }

void SnapshotReporter::cancel() {
    cancelled_.store(true, std::memory_order_release);

    std::shared_ptr<IBlockingStream> sock;
    {
        std::lock_guard<std::mutex> lk(socket_mtx_);
        sock = socket_;
        socket_.reset();
    }
    if (sock) {
        try { sock->shutdown(); } catch (...) {}
        try { sock->close(); } catch (...) {}
    }
}

bool SnapshotReporter::report(const std::string& snapshot_json) {
    if (cancelled_.load(std::memory_order_acquire)) return false;

    // Lazy-connect: open the socket on first call, reuse on subsequent calls.
    std::shared_ptr<IBlockingStream> sock;
    {
        std::lock_guard<std::mutex> lk(socket_mtx_);
        sock = socket_;
    }

    if (!sock) {
        try {
            sock = transport::SocketFactory::create_blocking_client(logger_);
        } catch (const std::exception& ex) {
            if (logger_)
                logger_->warning(std::string("SnapshotReporter: socket create failed: ")
                                 + ex.what());
            return false;
        }

        // Publish before connecting so cancel() can interrupt connect().
        {
            std::lock_guard<std::mutex> lk(socket_mtx_);
            if (cancelled_.load(std::memory_order_acquire)) {
                try { sock->shutdown(); } catch (...) {}
                try { sock->close(); } catch (...) {}
                return false;
            }
            socket_ = sock;
        }

        std::error_code ec;
        try {
            sock->connect(host_, port_, ec);
        } catch (const std::exception& ex) {
            if (logger_)
                logger_->warning(std::string("SnapshotReporter: connect threw: ")
                                 + ex.what());
            ec = std::make_error_code(std::errc::io_error);
        }
        if (ec) {
            if (logger_)
                logger_->warning("SnapshotReporter: connect to "
                                 + host_ + ":" + std::to_string(port_)
                                 + " failed: " + ec.message());
            std::lock_guard<std::mutex> lk(socket_mtx_);
            if (socket_ == sock) socket_.reset();
            try { sock->close(); } catch (...) {}
            return false;
        }
    }

    // Send snapshot frame and read ack on the persistent connection.
    try {
        rendezvous::write_message(*sock, rendezvous::MessageType::ReportSnapshot,
                                  snapshot_json);
        auto [resp_type, resp_body] = rendezvous::read_message(*sock);
        if (resp_type != rendezvous::MessageType::ReportAck) {
            if (logger_)
                logger_->warning("SnapshotReporter: unexpected response type "
                                 + std::to_string(static_cast<int>(resp_type)));
            // Treat as protocol error → tear down so next call reconnects.
            std::lock_guard<std::mutex> lk(socket_mtx_);
            if (socket_ == sock) socket_.reset();
            try { sock->shutdown(); } catch (...) {}
            try { sock->close(); } catch (...) {}
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        if (logger_)
            logger_->warning(std::string("SnapshotReporter: I/O error: ") + ex.what());
        std::lock_guard<std::mutex> lk(socket_mtx_);
        if (socket_ == sock) socket_.reset();
        try { sock->shutdown(); } catch (...) {}
        try { sock->close(); } catch (...) {}
        return false;
    }
}

} // namespace monitoring
