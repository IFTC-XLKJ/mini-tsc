/* Minimal <pthread.h> shim for building the vendored QuickJS engine on Windows
 * with clang targeting the MSVC ABI (no POSIX/MingW sysroot present).
 *
 * QuickJS only needs pthreads because CONFIG_ATOMICS is defined unconditionally
 * (see quickjs.c) to back the SharedArrayBuffer/Atomics.wait feature. The
 * embedding shim (node_quickjs.c) never uses that feature, but the symbols must
 * still link. This maps the small mutex+condition-variable surface onto the
 * Win32 primitives already used elsewhere in the runtime (see thread_pool.c).
 *
 * The static initializer PTHREAD_MUTEX_INITIALIZER cannot map to a
 * CRITICAL_SECTION (which needs runtime initialisation), so mutexes/conds are
 * lazily initialised on first use via the two-slot "initialized" flag.
 */
#ifndef TS_QUICKJS_COMPAT_PTHREAD_H
#define TS_QUICKJS_COMPAT_PTHREAD_H

#ifdef _WIN32

#include <windows.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h> /* compat shim: struct timeval + gettimeofday (timedwait) */

/* ETIMEDOUT is defined by <errno.h> on MSVC-compatible CRT; guard anyway. */
#ifndef ETIMEDOUT
#define ETIMEDOUT 138
#endif

typedef struct {
  CRITICAL_SECTION cs;
  volatile long initialized;
} pthread_mutex_t;

typedef struct {
  CONDITION_VARIABLE cv;
  volatile long initialized;
} pthread_cond_t;

#define PTHREAD_MUTEX_INITIALIZER { { 0 }, 0 }

typedef unsigned int pthread_t;

static inline void ts_pthread_mutex_ensure(pthread_mutex_t* m) {
  if (InterlockedCompareExchange(&m->initialized, 1, 0) == 0) {
    InitializeCriticalSection(&m->cs);
  }
}

static inline void ts_pthread_cond_ensure(pthread_cond_t* c) {
  if (InterlockedCompareExchange(&c->initialized, 1, 0) == 0) {
    InitializeConditionVariable(&c->cv);
  }
}

static inline int pthread_mutex_init(pthread_mutex_t* m, const void* attr) {
  (void)attr;
  m->initialized = 0;
  ts_pthread_mutex_ensure(m);
  return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t* m) {
  if (m->initialized) {
    DeleteCriticalSection(&m->cs);
    m->initialized = 0;
  }
  return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t* m) {
  ts_pthread_mutex_ensure(m);
  EnterCriticalSection(&m->cs);
  return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t* m) {
  LeaveCriticalSection(&m->cs);
  return 0;
}

static inline int pthread_cond_init(pthread_cond_t* c, const void* attr) {
  (void)attr;
  c->initialized = 0;
  ts_pthread_cond_ensure(c);
  return 0;
}

static inline int pthread_cond_destroy(pthread_cond_t* c) {
  if (c->initialized) c->initialized = 0;
  return 0;
}

static inline int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m) {
  ts_pthread_cond_ensure(c);
  ts_pthread_mutex_ensure(m);
  if (!SleepConditionVariableCS(&c->cv, &m->cs, INFINITE)) return EINVAL;
  return 0;
}

/* ts is an absolute CLOCK_REALTIME deadline (see quickjs.c Atomics path). */
static inline int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m,
                                         const struct timespec* ts) {
  ts_pthread_cond_ensure(c);
  ts_pthread_mutex_ensure(m);
  struct timeval now;
  gettimeofday(&now, 0);
  long long target_ms =
      (long long)ts->tv_sec * 1000LL + (long long)ts->tv_nsec / 1000000LL;
  long long now_ms = (long long)now.tv_sec * 1000LL + (long long)now.tv_usec / 1000LL;
  long long remaining = target_ms - now_ms;
  if (remaining < 0) remaining = 0;
  DWORD wait_ms = remaining > 0x7FFFFFFF ? 0x7FFFFFFF : (DWORD)remaining;
  if (!SleepConditionVariableCS(&c->cv, &m->cs, wait_ms)) {
    if (GetLastError() == ERROR_TIMEOUT) return ETIMEDOUT;
    return EINVAL;
  }
  return 0;
}

static inline int pthread_cond_signal(pthread_cond_t* c) {
  ts_pthread_cond_ensure(c);
  WakeConditionVariable(&c->cv);
  return 0;
}

static inline int pthread_cond_broadcast(pthread_cond_t* c) {
  ts_pthread_cond_ensure(c);
  WakeAllConditionVariable(&c->cv);
  return 0;
}

#endif /* _WIN32 */

#endif /* TS_QUICKJS_COMPAT_PTHREAD_H */