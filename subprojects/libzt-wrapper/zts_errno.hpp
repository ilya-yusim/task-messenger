#pragma once

// Thread-local replacement for the original global zts_errno.
// Access via the macro zts_errno which dereferences a per-thread int.
// We expose a C linkage accessor so C and C++ code can share it consistently.

#ifdef __cplusplus
extern "C" {
#endif
int *zts_errno_location(void);
#ifdef __cplusplus
}
#endif

#define zts_errno (*zts_errno_location())
