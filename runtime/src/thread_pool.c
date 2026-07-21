#define _CRT_SECURE_NO_WARNINGS
#include "runtime.h"
#include "ts_features.h"
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#endif

#define TS_POOL_WORKERS 4

typedef struct {
  TsJob* head;
  TsJob* tail;
  int count;
#ifdef _WIN32
  CRITICAL_SECTION mu;
  CONDITION_VARIABLE cv;
#else
  pthread_mutex_t mu;
  pthread_cond_t cv;
#endif
} TsJobQueue;

static TsJobQueue g_work = {0};
static TsJobQueue g_done = {0};
static int g_inflight = 0;       /* submitted, not yet completed on main */
static int g_pool_inited = 0;
static int g_pool_shutdown = 0;

#ifdef _WIN32
static HANDLE g_workers[TS_POOL_WORKERS];
#else
static pthread_t g_workers[TS_POOL_WORKERS];
#endif

static void q_init(TsJobQueue* q) {
  q->head = q->tail = NULL;
  q->count = 0;
#ifdef _WIN32
  InitializeCriticalSection(&q->mu);
  InitializeConditionVariable(&q->cv);
#else
  pthread_mutex_init(&q->mu, NULL);
  pthread_cond_init(&q->cv, NULL);
#endif
}

static void q_push(TsJobQueue* q, TsJob* job) {
  job->next = NULL;
#ifdef _WIN32
  EnterCriticalSection(&q->mu);
#else
  pthread_mutex_lock(&q->mu);
#endif
  if (q->tail) q->tail->next = job;
  else q->head = job;
  q->tail = job;
  q->count++;
#ifdef _WIN32
  WakeConditionVariable(&q->cv);
  LeaveCriticalSection(&q->mu);
#else
  pthread_cond_signal(&q->cv);
  pthread_mutex_unlock(&q->mu);
#endif
}

/* Pop with optional timeout_ms (<0 = forever). Returns NULL on timeout/shutdown. */
static TsJob* q_pop(TsJobQueue* q, int timeout_ms, int* shutdown_flag) {
  TsJob* job = NULL;
#ifdef _WIN32
  EnterCriticalSection(&q->mu);
  while (!q->head) {
    if (shutdown_flag && *shutdown_flag) {
      LeaveCriticalSection(&q->mu);
      return NULL;
    }
    if (timeout_ms == 0) {
      LeaveCriticalSection(&q->mu);
      return NULL;
    }
    if (timeout_ms < 0) {
      SleepConditionVariableCS(&q->cv, &q->mu, INFINITE);
    } else {
      if (!SleepConditionVariableCS(&q->cv, &q->mu, (DWORD)timeout_ms)) {
        /* timeout */
        if (!q->head) {
          LeaveCriticalSection(&q->mu);
          return NULL;
        }
      }
      if (!q->head && timeout_ms >= 0) {
        /* after timed wait, re-check */
        if (!q->head) {
          LeaveCriticalSection(&q->mu);
          return NULL;
        }
      }
    }
  }
  job = q->head;
  q->head = job->next;
  if (!q->head) q->tail = NULL;
  q->count--;
  LeaveCriticalSection(&q->mu);
#else
  pthread_mutex_lock(&q->mu);
  if (timeout_ms < 0) {
    while (!q->head) {
      if (shutdown_flag && *shutdown_flag) {
        pthread_mutex_unlock(&q->mu);
        return NULL;
      }
      pthread_cond_wait(&q->cv, &q->mu);
    }
  } else if (timeout_ms == 0) {
    if (!q->head) {
      pthread_mutex_unlock(&q->mu);
      return NULL;
    }
  } else {
    struct timespec ts;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ts.tv_sec = tv.tv_sec + timeout_ms / 1000;
    ts.tv_nsec = (tv.tv_usec * 1000L) + (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
      ts.tv_sec += 1;
      ts.tv_nsec -= 1000000000L;
    }
    while (!q->head) {
      if (shutdown_flag && *shutdown_flag) {
        pthread_mutex_unlock(&q->mu);
        return NULL;
      }
      int rc = pthread_cond_timedwait(&q->cv, &q->mu, &ts);
      if (rc == ETIMEDOUT && !q->head) {
        pthread_mutex_unlock(&q->mu);
        return NULL;
      }
      if (rc != 0 && rc != ETIMEDOUT && !q->head) {
        pthread_mutex_unlock(&q->mu);
        return NULL;
      }
    }
  }
  job = q->head;
  q->head = job->next;
  if (!q->head) q->tail = NULL;
  q->count--;
  pthread_mutex_unlock(&q->mu);
#endif
  job->next = NULL;
  return job;
}

#ifdef _WIN32
static DWORD WINAPI worker_main(LPVOID arg) {
  (void)arg;
  for (;;) {
    TsJob* job = q_pop(&g_work, -1, &g_pool_shutdown);
    if (!job) break;
    if (job->work) job->work(job->userdata);
    q_push(&g_done, job);
  }
  return 0;
}
#else
static void* worker_main(void* arg) {
  (void)arg;
  for (;;) {
    TsJob* job = q_pop(&g_work, -1, &g_pool_shutdown);
    if (!job) break;
    if (job->work) job->work(job->userdata);
    q_push(&g_done, job);
  }
  return NULL;
}
#endif

void ts_thread_pool_init(void) {
  if (g_pool_inited) return;
  g_pool_inited = 1;
  g_pool_shutdown = 0;
  g_inflight = 0;
  q_init(&g_work);
  q_init(&g_done);
  for (int i = 0; i < TS_POOL_WORKERS; i++) {
#ifdef _WIN32
    g_workers[i] = CreateThread(NULL, 0, worker_main, NULL, 0, NULL);
#else
    pthread_create(&g_workers[i], NULL, worker_main, NULL);
#endif
  }
}

void ts_thread_pool_submit(TsJob* job) {
  if (!job) return;
  ts_thread_pool_init();
#ifdef _WIN32
  EnterCriticalSection(&g_done.mu);
  g_inflight++;
  LeaveCriticalSection(&g_done.mu);
#else
  pthread_mutex_lock(&g_done.mu);
  g_inflight++;
  pthread_mutex_unlock(&g_done.mu);
#endif
  q_push(&g_work, job);
}

int ts_jobs_pending(void) {
  if (!g_pool_inited) return 0;
  int n;
#ifdef _WIN32
  EnterCriticalSection(&g_done.mu);
  n = g_inflight;
  LeaveCriticalSection(&g_done.mu);
#else
  pthread_mutex_lock(&g_done.mu);
  n = g_inflight;
  pthread_mutex_unlock(&g_done.mu);
#endif
  return n > 0;
}

int ts_completion_poll(void) {
  /* Pool not started → no jobs; avoid EnterCriticalSection on uninit mutex */
  if (!g_pool_inited) return 0;
  int n = 0;
  for (;;) {
    TsJob* job = q_pop(&g_done, 0, NULL);
    if (!job) break;
    if (job->complete) job->complete(job->userdata);
#ifdef _WIN32
    EnterCriticalSection(&g_done.mu);
    if (g_inflight > 0) g_inflight--;
    LeaveCriticalSection(&g_done.mu);
#else
    pthread_mutex_lock(&g_done.mu);
    if (g_inflight > 0) g_inflight--;
    pthread_mutex_unlock(&g_done.mu);
#endif
    free(job);
    n++;
  }
  return n;
}

void ts_completion_wait(int timeout_ms) {
  /* Wait until a completion is available or timeout. Does not consume. */
  if (!g_pool_inited) return;
  if (timeout_ms < 0) timeout_ms = 50;
#ifdef _WIN32
  EnterCriticalSection(&g_done.mu);
  if (!g_done.head && g_inflight > 0) {
    SleepConditionVariableCS(&g_done.cv, &g_done.mu, (DWORD)timeout_ms);
  }
  LeaveCriticalSection(&g_done.mu);
#else
  pthread_mutex_lock(&g_done.mu);
  if (!g_done.head && g_inflight > 0) {
    struct timespec ts;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ts.tv_sec = tv.tv_sec + timeout_ms / 1000;
    ts.tv_nsec = (tv.tv_usec * 1000L) + (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
      ts.tv_sec += 1;
      ts.tv_nsec -= 1000000000L;
    }
    pthread_cond_timedwait(&g_done.cv, &g_done.mu, &ts);
  }
  pthread_mutex_unlock(&g_done.mu);
#endif
}

void ts_thread_pool_shutdown(void) {
  if (!g_pool_inited) return;
  g_pool_shutdown = 1;
  /* Wake all workers */
#ifdef _WIN32
  for (int i = 0; i < TS_POOL_WORKERS; i++) WakeConditionVariable(&g_work.cv);
  for (int i = 0; i < TS_POOL_WORKERS; i++) {
    if (g_workers[i]) {
      WaitForSingleObject(g_workers[i], 2000);
      CloseHandle(g_workers[i]);
      g_workers[i] = NULL;
    }
  }
#else
  for (int i = 0; i < TS_POOL_WORKERS; i++) pthread_cond_broadcast(&g_work.cv);
  for (int i = 0; i < TS_POOL_WORKERS; i++) pthread_join(g_workers[i], NULL);
#endif
  /* Drain remaining completions */
  ts_completion_poll();
  g_pool_inited = 0;
}

/* Timer integration: use builtins when available; local stubs otherwise so
   thread_pool.c links even without TS_NEED_TIMERS / builtins.c. */
#if defined(TS_NEED_TIMERS)
extern int ts_timers_pending(void);
extern void ts_timers_run(void);
#else
static int ts_timers_pending_local(void) { return 0; }
static void ts_timers_run_local(void) {}
#define ts_timers_pending ts_timers_pending_local
#define ts_timers_run ts_timers_run_local
#endif

int ts_async_pending(void) {
  return ts_jobs_pending() || ts_timers_pending();
}

void ts_async_run(void) {
  while (ts_async_pending()) {
    int did = ts_completion_poll();
    if (ts_timers_pending()) {
      if (!ts_jobs_pending()) {
        ts_timers_run();
        break;
      }
    }
    if (!did && ts_jobs_pending()) {
      ts_completion_wait(50);
    } else if (!did && !ts_jobs_pending()) {
      break;
    }
    /* Opportunistic GC while waiting on async work */
    ts_gc_maybe_collect_idle();
  }
  if (ts_timers_pending()) ts_timers_run();
  ts_completion_poll();
  /* Event-loop drained: reclaim garbage if enough was allocated */
  ts_gc_maybe_collect_idle();
}
