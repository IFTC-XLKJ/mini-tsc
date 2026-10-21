/* Minimal POSIX <sys/time.h> shim for building the vendored QuickJS engine on
 * Windows with clang targeting the MSVC ABI (no POSIX/MingW sysroot present).
 *
 * Only the surface QuickJS actually uses is provided: struct timeval,
 * gettimeofday() (drives Date.now / Math.random seeding) plus clock_gettime/
 * struct timespec (used by the Atomics wait path). On non-Windows this file is
 * never selected — the real system header wins because this compat dir is added
 * last on the -I list only for the Windows build.
 */
#ifndef TS_QUICKJS_COMPAT_SYS_TIME_H
#define TS_QUICKJS_COMPAT_SYS_TIME_H

#ifdef _WIN32

#include <time.h>
#include <stdint.h>
#include <windows.h>

/* <windows.h> pulls in <winsock.h> (unless WIN32_LEAN_AND_MEAN), which already
 * defines an identical struct timeval. Reuse it when present. */
#if !defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_) && !defined(_TIMEVAL_DEFINED)
#define _TIMEVAL_DEFINED
struct timeval {
  long tv_sec;
  long tv_usec;
};
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

typedef int clockid_t;

static inline int gettimeofday(struct timeval* tv, void* tz) {
  (void)tz;
  if (!tv) return 0;
  FILETIME ft;
  ULARGE_INTEGER ui;
  GetSystemTimeAsFileTime(&ft);
  ui.LowPart = ft.dwLowDateTime;
  ui.HighPart = ft.dwHighDateTime;
  /* FILETIME is 100ns ticks since 1601-01-01; convert to Unix epoch. */
  uint64_t ticks = ui.QuadPart - 116444736000000000ULL;
  tv->tv_sec = (long)(ticks / 10000000ULL);
  tv->tv_usec = (long)((ticks % 10000000ULL) / 10ULL);
  return 0;
}

static inline int clock_gettime(clockid_t clk_id, struct timespec* ts) {
  (void)clk_id;
  struct timeval tv;
  gettimeofday(&tv, 0);
  ts->tv_sec = tv.tv_sec;
  ts->tv_nsec = tv.tv_usec * 1000;
  return 0;
}

#else
#include_next <sys/time.h>
#endif /* _WIN32 */

#endif /* TS_QUICKJS_COMPAT_SYS_TIME_H */