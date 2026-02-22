#pragma once

/**
 * @file thread_compat.h
 * @brief Cross-platform threading compatibility shim.
 *
 * On POSIX (macOS, Linux) this simply re-exports <pthread.h>.
 * On Windows (MSVC / MinGW) it maps the subset of pthreads used by this
 * project onto Win32 threading primitives so that no external pthreads-win32
 * library is required.
 *
 * Supported surface area:
 *   Types   : pthread_t, pthread_mutex_t
 *   Macros  : PTHREAD_MUTEX_INITIALIZER
 *   Functions: pthread_create, pthread_detach, pthread_join,
 *              pthread_mutex_lock, pthread_mutex_unlock
 */

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h> /* _beginthreadex */
#include <stdlib.h>  /* free */

/* ---- Types ---- */

typedef HANDLE pthread_t;

typedef CRITICAL_SECTION pthread_mutex_t;

/* ---- PTHREAD_MUTEX_INITIALIZER ----
 *
 * CRITICAL_SECTION cannot be statically initialised with a constant, so we
 * use a sentinel value.  The first lock/unlock call detects the sentinel and
 * performs the real InitializeCriticalSection().
 *
 * We store the sentinel by zero-filling the struct and setting a flag in the
 * first byte that Win32 never writes itself (SpinCount is at offset 4+, and
 * the DebugInfo pointer at offset 0 is always a valid heap pointer or -1).
 */
#define PTHREAD_MUTEX_INITIALIZER {NULL, 0, 0, NULL, NULL, 0}

/* ---- Internal helpers ---- */

/* Trampoline to bridge _beginthreadex's __stdcall to pthread's void* calling
 * convention. */
typedef struct _pthread_win32_ctx {
    void *(*start_routine)(void *);
    void *arg;
} _pthread_win32_ctx_t;

static unsigned __stdcall _pthread_win32_thread_start(void *param) {
    _pthread_win32_ctx_t *ctx      = (_pthread_win32_ctx_t *)param;
    void *(*start_routine)(void *) = ctx->start_routine;
    void *arg                      = ctx->arg;
    free(ctx);
    start_routine(arg);
    return 0;
}

/* Lazily initialise a CRITICAL_SECTION whose memory was zero-filled.
 * A DebugInfo of NULL is the sentinel we wrote in PTHREAD_MUTEX_INITIALIZER. */
static inline void _pthread_mutex_ensure_init(pthread_mutex_t *m) {
    if (m->DebugInfo == NULL) {
        InitializeCriticalSection(m);
    }
}

/* ---- pthread_create ---- */
static inline int pthread_create(pthread_t *thread, const void *attr, void *(*start_routine)(void *), void *arg) {
    (void)attr;
    _pthread_win32_ctx_t *ctx = (_pthread_win32_ctx_t *)malloc(sizeof(_pthread_win32_ctx_t));
    if (!ctx)
        return 1; /* EAGAIN */
    ctx->start_routine = start_routine;
    ctx->arg           = arg;
    HANDLE h           = (HANDLE)_beginthreadex(NULL, 0, _pthread_win32_thread_start, ctx, 0, NULL);
    if (!h) {
        free(ctx);
        return 1;
    }
    *thread = h;
    return 0;
}

/* ---- pthread_detach ---- */
static inline int pthread_detach(pthread_t thread) {
    CloseHandle(thread);
    return 0;
}

/* ---- pthread_join ---- */
static inline int pthread_join(pthread_t thread, void **retval) {
    (void)retval;
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return 0;
}

/* ---- pthread_mutex_lock ---- */
static inline int pthread_mutex_lock(pthread_mutex_t *mutex) {
    _pthread_mutex_ensure_init(mutex);
    EnterCriticalSection(mutex);
    return 0;
}

/* ---- pthread_mutex_unlock ---- */
static inline int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    LeaveCriticalSection(mutex);
    return 0;
}

#else /* POSIX */

#include <pthread.h>

#endif /* _WIN32 */
