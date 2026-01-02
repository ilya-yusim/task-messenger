#include "zts_errno.hpp"

#if defined(_MSC_VER)
__declspec(thread) static int zts_errno_tls_var = 0;
#elif defined(__cplusplus)
thread_local static int zts_errno_tls_var = 0;
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Thread_local static int zts_errno_tls_var = 0;
#else
static int zts_errno_tls_var = 0; /* Fallback: not thread-safe */
#endif

extern "C" int *zts_errno_location(void) { return &zts_errno_tls_var; }
