#pragma once

#include <cstddef>
#include <cstdint>

struct WorkerGreeting {
    uint32_t magic;
    uint32_t version;
    uint64_t node_id;
};

static_assert(sizeof(WorkerGreeting) == 16, "WorkerGreeting wire size changed — update protocol version");
static_assert(offsetof(WorkerGreeting, magic) == 0);
static_assert(offsetof(WorkerGreeting, version) == 4);
static_assert(offsetof(WorkerGreeting, node_id) == 8);

inline constexpr uint32_t kWorkerGreetingMagic = 0x544D5747; // "TMWG"
inline constexpr uint32_t kWorkerGreetingVersion = 1;
