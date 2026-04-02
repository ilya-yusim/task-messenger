#pragma once

#include <cstdint>

struct WorkerGreeting {
    uint32_t magic;
    uint32_t version;
    uint64_t node_id;
};

inline constexpr uint32_t kWorkerGreetingMagic = 0x544D5747; // "TMWG"
inline constexpr uint32_t kWorkerGreetingVersion = 1;
