/**
 * \file rendezvous/RendezvousProtocol.hpp
 * \brief Wire protocol for rendezvous service communication over ZeroTier sockets.
 *
 * All messages are framed as:
 *   [4 bytes: payload length (little-endian)] [1 byte: message type] [JSON payload]
 *
 * Payload length covers the type byte + JSON body (i.e. total frame minus the 4-byte header).
 */
#pragma once

#include "transport/socket/IBlockingStream.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <system_error>
#include <stdexcept>

namespace rendezvous {

/// Maximum accepted frame payload (type byte + JSON body).  64 KB is generous
/// for any rendezvous message including full monitoring snapshots.
inline constexpr uint32_t kMaxFramePayload = 64u * 1024u;

/// Frame header size: 4-byte little-endian length prefix.
inline constexpr size_t kFrameHeaderSize = 4;

/// Message types exchanged between client/server.
enum class MessageType : uint8_t {
    RegisterRequest   = 1,
    RegisterResponse  = 2,
    UnregisterRequest = 3,
    UnregisterResponse = 4,
    DiscoverRequest   = 5,
    DiscoverResponse  = 6,
    ReportSnapshot    = 7,
    ReportAck         = 8,
};

// ── Blocking I/O helpers ────────────────────────────────────────────────────

/// Read exactly \p size bytes from a blocking stream.  Throws on error or short read.
inline void read_exact(IBlockingStream& s, void* buf, size_t size) {
    auto* dst = static_cast<char*>(buf);
    size_t remaining = size;
    while (remaining > 0) {
        size_t br = 0;
        std::error_code ec;
        s.read(dst, remaining, br, ec);
        if (ec) throw std::system_error(ec, "rendezvous: read_exact failed");
        if (br == 0) throw std::runtime_error("rendezvous: read_exact: connection closed");
        dst += br;
        remaining -= br;
    }
}

/// Write exactly \p size bytes to a blocking stream.  Throws on error or short write.
inline void write_exact(IBlockingStream& s, const void* buf, size_t size) {
    auto* src = static_cast<const char*>(buf);
    size_t remaining = size;
    while (remaining > 0) {
        size_t bw = 0;
        std::error_code ec;
        s.write(src, remaining, bw, ec);
        if (ec) throw std::system_error(ec, "rendezvous: write_exact failed");
        if (bw == 0) throw std::runtime_error("rendezvous: write_exact: connection closed");
        src += bw;
        remaining -= bw;
    }
}

// ── Frame read / write ──────────────────────────────────────────────────────

/// Read a framed message.  Returns the message type and the JSON payload string.
inline std::pair<MessageType, std::string> read_message(IBlockingStream& s) {
    // Read 4-byte little-endian length
    uint8_t len_buf[kFrameHeaderSize];
    read_exact(s, len_buf, kFrameHeaderSize);
    uint32_t payload_len = static_cast<uint32_t>(len_buf[0])
                         | (static_cast<uint32_t>(len_buf[1]) << 8)
                         | (static_cast<uint32_t>(len_buf[2]) << 16)
                         | (static_cast<uint32_t>(len_buf[3]) << 24);

    if (payload_len < 1) {
        throw std::runtime_error("rendezvous: frame payload too short (no type byte)");
    }
    if (payload_len > kMaxFramePayload) {
        throw std::runtime_error("rendezvous: frame payload exceeds maximum ("
                                 + std::to_string(payload_len) + " > "
                                 + std::to_string(kMaxFramePayload) + ")");
    }

    // Read type byte
    uint8_t type_byte = 0;
    read_exact(s, &type_byte, 1);

    // Read JSON body
    std::string json_body;
    uint32_t json_len = payload_len - 1;
    if (json_len > 0) {
        json_body.resize(json_len);
        read_exact(s, json_body.data(), json_len);
    }

    return {static_cast<MessageType>(type_byte), std::move(json_body)};
}

/// Write a framed message (type + JSON body).
inline void write_message(IBlockingStream& s, MessageType type, const std::string& json_body) {
    uint32_t payload_len = static_cast<uint32_t>(1 + json_body.size());
    if (payload_len > kMaxFramePayload) {
        throw std::runtime_error("rendezvous: write_message: payload exceeds maximum");
    }

    // Write 4-byte little-endian length
    uint8_t len_buf[kFrameHeaderSize];
    len_buf[0] = static_cast<uint8_t>(payload_len & 0xFF);
    len_buf[1] = static_cast<uint8_t>((payload_len >> 8) & 0xFF);
    len_buf[2] = static_cast<uint8_t>((payload_len >> 16) & 0xFF);
    len_buf[3] = static_cast<uint8_t>((payload_len >> 24) & 0xFF);
    write_exact(s, len_buf, kFrameHeaderSize);

    // Write type byte
    uint8_t type_byte = static_cast<uint8_t>(type);
    write_exact(s, &type_byte, 1);

    // Write JSON body
    if (!json_body.empty()) {
        write_exact(s, json_body.data(), json_body.size());
    }
}

} // namespace rendezvous
