#ifndef MESSAGE_TYPES_HPP
#define MESSAGE_TYPES_HPP

#include <cstdint>

enum MessageType : uint8_t {
    PING = 0x01,
    PONG = 0x02,
    APPLICATION_DATA = 0x03
};

#endif // MESSAGE_TYPES_HPP