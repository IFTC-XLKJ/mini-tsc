#include "node_worker_threads.h"
#include "node_events.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

/* ==================== Types ==================== */

#define WORKER_TAG       0x57524B52  /* 'WRKR' */
#define MSGPORT_TAG      0x4D505254  /* 'MPRT' */
#define MSGCHANNEL_TAG   0x4D43484E  /* 'MCHN' */
#define BROADCAST_TAG    0x4243484E  /* 'BCHN' */

/* Message queue entry */
typedef struct TsMsgEntry {
  Value data;
  struct TsMsgEntry* next;
} TsMsgEntry;

/* Message queue */
typedef struct TsMsgQueue {
  TsMsgEntry* head;
  TsMsgEntry* tail;
  int count;
#ifdef _WIN32
  CRITICAL_SECTION mu;
  CONDITION_VARIABLE cv;
#else
  pthread_mutex_t mu;
  pthread_cond_t cv;
#endif
} TsMsgQueue;

/* MessagePort object */
typedef struct {
  int32_t type_tag;
  int32_t refcount;
  TsMsgQueue* inbox;
  Value onmessage;
  Value onmessageerror;
  int closed;
  int refed;
} TsMsgPort;

/* Worker object */
typedef struct {
  int32_t type_tag;
  int32_t refcount;
  int thread_id;
  int thread_name;
  TSString* filename;
  TsMsgPort* port;           /* port to communicate with worker */
  Value workerData;
  Value onmessage;
  Value onmessageerror;
  Value onerror;
  Value onexit;
  int started;
  int terminated;
  int refed;
#ifdef _WIN32
  HANDLE thread_handle;
#else
  pthread_t thread_handle;
#endif
} TsWorker;

/* BroadcastChannel object */
typedef struct {
  int32_t type_tag;
  int32_t refcount;
  TSString* name;
  Value onmessage;
  Value onmessageerror;
} TsBroadcast;

/* Worker thread context (passed to thread function) */
typedef struct {
  TsWorker* worker;
  TsWorkerEntryFn entry_fn;
} WorkerThreadCtx;

/* ==================== Globals ==================== */

static int g_is_main_thread = 1;
static int g_next_thread_id = 1;
static TsMsgPort* g_parent_port = NULL;
static Value g_worker_data = {TAG_NULL, {0}};
static int g_current_thread_id = 0;
static TSString* g_current_thread_name = NULL;

/* Entry function pointer (set by main()) */
static TsWorkerEntryFn g_entry_fn = NULL;

/* Thread-local storage for current worker */
#ifdef _WIN32
static __declspec(thread) TsWorker* tls_current_worker = NULL;
#else
static __thread TsWorker* tls_current_worker = NULL;
#endif

/* ==================== Message Queue ==================== */

static TsMsgQueue* mq_create(void) {
  TsMsgQueue* q = (TsMsgQueue*)malloc(sizeof(TsMsgQueue));
  if (!q) return NULL;
  q->head = q->tail = NULL;
  q->count = 0;
#ifdef _WIN32
  InitializeCriticalSection(&q->mu);
  InitializeConditionVariable(&q->cv);
#else
  pthread_mutex_init(&q->mu, NULL);
  pthread_cond_init(&q->cv, NULL);
#endif
  return q;
}

static void mq_push(TsMsgQueue* q, Value data) {
  if (!q) return;
  TsMsgEntry* entry = (TsMsgEntry*)malloc(sizeof(TsMsgEntry));
  if (!entry) return;
  entry->data = data;
  entry->next = NULL;

#ifdef _WIN32
  EnterCriticalSection(&q->mu);
#else
  pthread_mutex_lock(&q->mu);
#endif

  if (q->tail) q->tail->next = entry;
  else q->head = entry;
  q->tail = entry;
  q->count++;

#ifdef _WIN32
  WakeConditionVariable(&q->cv);
  LeaveCriticalSection(&q->mu);
#else
  pthread_cond_signal(&q->cv);
  pthread_mutex_unlock(&q->mu);
#endif
}

static Value mq_pop(TsMsgQueue* q, int timeout_ms) {
  if (!q) return ts_value_undefined();

#ifdef _WIN32
  EnterCriticalSection(&q->mu);
#else
  pthread_mutex_lock(&q->mu);
#endif

  while (!q->head) {
    if (timeout_ms == 0) {
#ifdef _WIN32
      LeaveCriticalSection(&q->mu);
#else
      pthread_mutex_unlock(&q->mu);
#endif
      return ts_value_undefined();
    }
    if (timeout_ms < 0) {
#ifdef _WIN32
      SleepConditionVariableCS(&q->cv, &q->mu, INFINITE);
#else
      pthread_cond_wait(&q->cv, &q->mu);
#endif
    } else {
#ifdef _WIN32
      if (!SleepConditionVariableCS(&q->cv, &q->mu, (DWORD)timeout_ms)) {
        if (!q->head) {
          LeaveCriticalSection(&q->mu);
          return ts_value_undefined();
        }
      }
#else
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_sec += timeout_ms / 1000;
      ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
      if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
      }
      int rc = pthread_cond_timedwait(&q->cv, &q->mu, &ts);
      if (rc == ETIMEDOUT && !q->head) {
        pthread_mutex_unlock(&q->mu);
        return ts_value_undefined();
      }
#endif
    }
  }

  TsMsgEntry* entry = q->head;
  q->head = entry->next;
  if (!q->head) q->tail = NULL;
  q->count--;

#ifdef _WIN32
  LeaveCriticalSection(&q->mu);
#else
  pthread_mutex_unlock(&q->mu);
#endif

  Value data = entry->data;
  free(entry);
  return data;
}

static int mq_pending(TsMsgQueue* q) {
  if (!q) return 0;
  int n;
#ifdef _WIN32
  EnterCriticalSection(&q->mu);
  n = q->count;
  LeaveCriticalSection(&q->mu);
#else
  pthread_mutex_lock(&q->mu);
  n = q->count;
  pthread_mutex_unlock(&q->mu);
#endif
  return n;
}

/* ==================== MessagePort ==================== */

static TsMsgPort* port_create(void) {
  TsMsgPort* port = (TsMsgPort*)malloc(sizeof(TsMsgPort));
  if (!port) return NULL;
  port->type_tag = MSGPORT_TAG;
  port->refcount = 1;
  port->inbox = mq_create();
  port->onmessage = ts_value_undefined();
  port->onmessageerror = ts_value_undefined();
  port->closed = 0;
  port->refed = 1;
  return port;
}

static void port_free(TsMsgPort* port) {
  if (!port) return;
  port->refcount--;
  if (port->refcount > 0) return;
  /* Drain inbox */
  while (mq_pending(port->inbox)) {
    mq_pop(port->inbox, 0);
  }
  free(port->inbox);
  free(port);
}

Value node_worker_threads_MessagePort(void) {
  TsMsgPort* port = port_create();
  if (!port) return ts_value_undefined();
  return ts_value_object(port);
}

/* ==================== MessageChannel ==================== */

static struct {
  TsMsgPort* port1;
  TsMsgPort* port2;
} MessageChannel_pair;

Value node_worker_threads_MessageChannel(void) {
  TsMsgPort* port1 = port_create();
  TsMsgPort* port2 = port_create();
  if (!port1 || !port2) {
    port_free(port1);
    port_free(port2);
    return ts_value_undefined();
  }

  /* Create channel object */
  TSHashMap* channel = ts_hashmap_new();
  ts_hashmap_set(channel, ts_string_new("_type"), ts_value_string(ts_string_new("MessageChannel")));
  ts_hashmap_set(channel, ts_string_new("port1"), ts_value_object(port1));
  ts_hashmap_set(channel, ts_string_new("port2"), ts_value_object(port2));

  return ts_value_object(channel);
}

/* ==================== Worker ==================== */

static TsWorker* worker_create(void) {
  TsWorker* w = (TsWorker*)malloc(sizeof(TsWorker));
  if (!w) return NULL;
  memset(w, 0, sizeof(TsWorker));
  w->type_tag = WORKER_TAG;
  w->refcount = 1;
  w->thread_id = g_next_thread_id++;
  w->port = port_create();
  w->workerData = ts_value_undefined();
  w->onmessage = ts_value_undefined();
  w->onmessageerror = ts_value_undefined();
  w->onerror = ts_value_undefined();
  w->onexit = ts_value_undefined();
  w->started = 0;
  w->terminated = 0;
  w->refed = 1;
  return w;
}

static void worker_free(TsWorker* w) {
  if (!w) return;
  w->refcount--;
  if (w->refcount > 0) return;
  port_free(w->port);
  free(w->filename);
  free(w);
}

#ifdef _WIN32
static DWORD WINAPI worker_thread_main(LPVOID arg) {
  WorkerThreadCtx* ctx = (WorkerThreadCtx*)arg;
  TsWorker* worker = ctx->worker;

  /* Set thread-local state */
  tls_current_worker = worker;
  g_is_main_thread = 0;
  g_current_thread_id = worker->thread_id;

  /* Set parent port for child */
  g_parent_port = worker->port;

  /* Set worker data */
  g_worker_data = worker->workerData;

  /* Run entry function if set */
  if (g_entry_fn) {
    g_entry_fn();
  }

  free(ctx);
  return 0;
}
#else
static void* worker_thread_main(void* arg) {
  WorkerThreadCtx* ctx = (WorkerThreadCtx*)arg;
  TsWorker* worker = ctx->worker;

  /* Set thread-local state */
  tls_current_worker = worker;
  g_is_main_thread = 0;
  g_current_thread_id = worker->thread_id;

  /* Set parent port for child */
  g_parent_port = worker->port;

  /* Set worker data */
  g_worker_data = worker->workerData;

  /* Run entry function if set */
  if (g_entry_fn) {
    g_entry_fn();
  }

  free(ctx);
  return NULL;
}
#endif

Value node_worker_threads_Worker(Value filename, Value options) {
  TSString* fn = ts_to_string(filename);
  if (!fn) return ts_value_undefined();

  TsWorker* worker = worker_create();
  if (!worker) return ts_value_undefined();
  worker->filename = fn;

  /* Parse options */
  if (options.tag == TAG_OBJECT && options.as.object) {
    TSHashMap* opts = (TSHashMap*)options.as.object;
    Value data = ts_hashmap_get(opts, ts_string_new("workerData"));
    if (data.tag != TAG_NULL) {
      worker->workerData = data;
    }
    Value name = ts_hashmap_get(opts, ts_string_new("name"));
    if (name.tag == TAG_STRING && name.as.string) {
      worker->thread_name = 1;  /* Mark as named */
    }
  }

  return ts_value_object(worker);
}

Value node_worker_threads_start(Value self) {
  if (self.tag != TAG_OBJECT || !self.as.object) return ts_value_undefined();
  TsWorker* worker = (TsWorker*)self.as.object;
  if (worker->type_tag != WORKER_TAG) return ts_value_undefined();
  if (worker->started) return ts_value_undefined();

  worker->started = 1;

  WorkerThreadCtx* ctx = (WorkerThreadCtx*)malloc(sizeof(WorkerThreadCtx));
  if (!ctx) return ts_value_undefined();
  ctx->worker = worker;
  ctx->entry_fn = g_entry_fn;

#ifdef _WIN32
  worker->thread_handle = CreateThread(NULL, 0, worker_thread_main, ctx, 0, NULL);
#else
  pthread_create(&worker->thread_handle, NULL, worker_thread_main, ctx);
#endif

  return ts_value_undefined();
}

Value node_worker_threads_terminate(Value self) {
  if (self.tag != TAG_OBJECT || !self.as.object) return ts_value_undefined();
  TsWorker* worker = (TsWorker*)self.as.object;
  if (worker->type_tag != WORKER_TAG) return ts_value_undefined();
  if (!worker->started || worker->terminated) return ts_value_undefined();

  worker->terminated = 1;

#ifdef _WIN32
  if (worker->thread_handle) {
    TerminateThread(worker->thread_handle, 0);
    CloseHandle(worker->thread_handle);
    worker->thread_handle = NULL;
  }
#else
  pthread_cancel(worker->thread_handle);
  pthread_join(worker->thread_handle, NULL);
#endif

  return ts_value_undefined();
}

Value node_worker_threads_close(Value self) {
  if (self.tag != TAG_OBJECT || !self.as.object) return ts_value_undefined();
  TsWorker* worker = (TsWorker*)self.as.object;
  if (worker->type_tag != WORKER_TAG) return ts_value_undefined();

  worker->port->closed = 1;
  return ts_value_undefined();
}

Value node_worker_threads_ref(Value self) {
  if (self.tag != TAG_OBJECT || !self.as.object) return ts_value_undefined();
  TsWorker* worker = (TsWorker*)self.as.object;
  if (worker->type_tag == WORKER_TAG) {
    worker->refed = 1;
  } else if (worker->type_tag == MSGPORT_TAG) {
    ((TsMsgPort*)self.as.object)->refed = 1;
  }
  return ts_value_undefined();
}

Value node_worker_threads_unref(Value self) {
  if (self.tag != TAG_OBJECT || !self.as.object) return ts_value_undefined();
  TsWorker* worker = (TsWorker*)self.as.object;
  if (worker->type_tag == WORKER_TAG) {
    worker->refed = 0;
  } else if (worker->type_tag == MSGPORT_TAG) {
    ((TsMsgPort*)self.as.object)->refed = 0;
  }
  return ts_value_undefined();
}

Value node_worker_threads_get_threadId(Value self) {
  if (self.tag != TAG_OBJECT || !self.as.object) return ts_value_number(0);
  TsWorker* worker = (TsWorker*)self.as.object;
  if (worker->type_tag == WORKER_TAG) {
    return ts_value_number((double)worker->thread_id);
  }
  return ts_value_number(0);
}

Value node_worker_threads_get_threadName(Value self) {
  if (self.tag != TAG_OBJECT || !self.as.object) return ts_value_string(ts_string_new(""));
  TsWorker* worker = (TsWorker*)self.as.object;
  if (worker->type_tag == WORKER_TAG && worker->thread_name) {
    return ts_value_string(ts_string_new("worker"));
  }
  return ts_value_string(ts_string_new(""));
}

/* ==================== Event Handling ==================== */

Value node_worker_threads_on(Value self, Value event, Value callback) {
  if (self.tag != TAG_OBJECT || !self.as.object) return self;
  void* obj = self.as.object;

  /* Check type tag */
  int32_t tag = *(int32_t*)obj;
  if (tag == WORKER_TAG) {
    TsWorker* w = (TsWorker*)obj;
    TSString* evName = ts_to_string(event);
    if (strcmp(evName->data, "message") == 0) {
      w->onmessage = callback;
    } else if (strcmp(evName->data, "error") == 0) {
      w->onerror = callback;
    } else if (strcmp(evName->data, "exit") == 0) {
      w->onexit = callback;
    }
  } else if (tag == MSGPORT_TAG) {
    TsMsgPort* p = (TsMsgPort*)obj;
    TSString* evName = ts_to_string(event);
    if (strcmp(evName->data, "message") == 0) {
      p->onmessage = callback;
    } else if (strcmp(evName->data, "messageerror") == 0) {
      p->onmessageerror = callback;
    }
  } else if (tag == BROADCAST_TAG) {
    TsBroadcast* b = (TsBroadcast*)obj;
    TSString* evName = ts_to_string(event);
    if (strcmp(evName->data, "message") == 0) {
      b->onmessage = callback;
    } else if (strcmp(evName->data, "messageerror") == 0) {
      b->onmessageerror = callback;
    }
  }

  return self;
}

Value node_worker_threads_once(Value self, Value event, Value callback) {
  /* For simplicity, treat once as on (full implementation would track once flag) */
  return node_worker_threads_on(self, event, callback);
}

Value node_worker_threads_off(Value self, Value event, Value callback) {
  if (self.tag != TAG_OBJECT || !self.as.object) return self;
  void* obj = self.as.object;

  int32_t tag = *(int32_t*)obj;
  if (tag == WORKER_TAG) {
    TsWorker* w = (TsWorker*)obj;
    TSString* evName = ts_to_string(event);
    if (strcmp(evName->data, "message") == 0) {
      w->onmessage = ts_value_undefined();
    } else if (strcmp(evName->data, "error") == 0) {
      w->onerror = ts_value_undefined();
    } else if (strcmp(evName->data, "exit") == 0) {
      w->onexit = ts_value_undefined();
    }
  } else if (tag == MSGPORT_TAG) {
    TsMsgPort* p = (TsMsgPort*)obj;
    TSString* evName = ts_to_string(event);
    if (strcmp(evName->data, "message") == 0) {
      p->onmessage = ts_value_undefined();
    } else if (strcmp(evName->data, "messageerror") == 0) {
      p->onmessageerror = ts_value_undefined();
    }
  } else if (tag == BROADCAST_TAG) {
    TsBroadcast* b = (TsBroadcast*)obj;
    TSString* evName = ts_to_string(event);
    if (strcmp(evName->data, "message") == 0) {
      b->onmessage = ts_value_undefined();
    } else if (strcmp(evName->data, "messageerror") == 0) {
      b->onmessageerror = ts_value_undefined();
    }
  }

  return self;
}

Value node_worker_threads_addListener(Value self, Value event, Value callback) {
  return node_worker_threads_on(self, event, callback);
}

Value node_worker_threads_removeListener(Value self, Value event, Value callback) {
  return node_worker_threads_off(self, event, callback);
}

/* ==================== Message Passing ==================== */

Value node_worker_threads_postMessage(Value self, Value value, Value transferList) {
  if (self.tag != TAG_OBJECT || !self.as.object) return ts_value_undefined();
  void* obj = self.as.object;

  int32_t tag = *(int32_t*)obj;
  if (tag == WORKER_TAG) {
    TsWorker* w = (TsWorker*)obj;
    if (w->port) {
      mq_push(w->port->inbox, value);
      /* Trigger onmessage on worker side */
      if (w->port->onmessage.tag == TAG_FUNCTION) {
        /* Would need to schedule callback on worker thread */
      }
    }
  } else if (tag == MSGPORT_TAG) {
    TsMsgPort* p = (TsMsgPort*)obj;
    mq_push(p->inbox, value);
    /* Trigger onmessage */
    if (p->onmessage.tag == TAG_FUNCTION) {
      /* Would need to schedule callback */
    }
  }

  return ts_value_undefined();
}

Value node_worker_threads_postMessageToThread(Value threadId, Value value, Value transferList) {
  /* Post message to a specific thread by ID */
  /* Simplified implementation - would need thread registry */
  return ts_value_undefined();
}

Value node_worker_threads_receiveMessageOnPort(Value port) {
  if (port.tag != TAG_OBJECT || !port.as.object) return ts_value_undefined();
  TsMsgPort* p = (TsMsgPort*)port.as.object;
  if (p->type_tag != MSGPORT_TAG) return ts_value_undefined();

  Value msg = mq_pop(p->inbox, 0);
  if (msg.tag == TAG_NULL) return ts_value_undefined();

  /* Return { message: data } */
  TSHashMap* result = ts_hashmap_new();
  ts_hashmap_set(result, ts_string_new("message"), msg);
  return ts_value_object(result);
}

/* ==================== Module Getters ==================== */

Value node_worker_threads_isMainThread(void) {
  return ts_value_boolean(g_is_main_thread);
}

Value node_worker_threads_parentPort(void) {
  if (!g_parent_port) {
    g_parent_port = port_create();
  }
  return ts_value_object(g_parent_port);
}

Value node_worker_threads_workerData(void) {
  return g_worker_data;
}

Value node_worker_threads_threadId(void) {
  return ts_value_number((double)g_current_thread_id);
}

Value node_worker_threads_threadName(void) {
  if (g_current_thread_name) {
    return ts_value_string(g_current_thread_name);
  }
  return ts_value_string(ts_string_new(""));
}

Value node_worker_threads_isInternalThread(void) {
  return ts_value_boolean(0);
}

Value node_worker_threads_SHARE_ENV(void) {
  /* Return special symbol for sharing environment */
  return ts_value_number(-1);
}

Value node_worker_threads_resourceLimits(void) {
  /* Return default resource limits */
  TSHashMap* limits = ts_hashmap_new();
  ts_hashmap_set(limits, ts_string_new("maxYoungGenerationSizeMb"), ts_value_number(64));
  ts_hashmap_set(limits, ts_string_new("maxOldGenerationSizeMb"), ts_value_number(512));
  ts_hashmap_set(limits, ts_string_new("codeRangeSizeMb"), ts_value_number(32));
  ts_hashmap_set(limits, ts_string_new("stackSizeMb"), ts_value_number(4));
  return ts_value_object(limits);
}

Value node_worker_threads_locks(void) {
  /* Return locks API object */
  TSHashMap* locks = ts_hashmap_new();
  ts_hashmap_set(locks, ts_string_new("_type"), ts_value_string(ts_string_new("locks")));
  return ts_value_object(locks);
}

/* ==================== BroadcastChannel ==================== */

Value node_worker_threads_BroadcastChannel(Value name) {
  TSString* n = ts_to_string(name);
  if (!n) return ts_value_undefined();

  TsBroadcast* bc = (TsBroadcast*)malloc(sizeof(TsBroadcast));
  if (!bc) return ts_value_undefined();
  bc->type_tag = BROADCAST_TAG;
  bc->refcount = 1;
  bc->name = n;
  bc->onmessage = ts_value_undefined();
  bc->onmessageerror = ts_value_undefined();

  return ts_value_object(bc);
}

/* ==================== Environment Data ==================== */

static TSHashMap* g_env_data = NULL;

Value node_worker_threads_getEnvironmentData(Value key) {
  if (!g_env_data) return ts_value_undefined();
  TSString* k = ts_to_string(key);
  return ts_hashmap_get(g_env_data, k);
}

Value node_worker_threads_setEnvironmentData(Value key, Value value) {
  if (!g_env_data) {
    g_env_data = ts_hashmap_new();
  }
  TSString* k = ts_to_string(key);
  ts_hashmap_set(g_env_data, k, value);
  return ts_value_undefined();
}

/* ==================== Utility Functions ==================== */

Value node_worker_threads_markAsUntransferable(Value object) {
  /* Mark object as untransferable (simplified) */
  if (object.tag == TAG_OBJECT && object.as.object) {
    TSHashMap* obj = (TSHashMap*)object.as.object;
    ts_hashmap_set(obj, ts_string_new("_untransferable"), ts_value_boolean(1));
  }
  return object;
}

Value node_worker_threads_isMarkedAsUntransferable(Value object) {
  if (object.tag == TAG_OBJECT && object.as.object) {
    TSHashMap* obj = (TSHashMap*)object.as.object;
    Value val = ts_hashmap_get(obj, ts_string_new("_untransferable"));
    return ts_value_boolean(ts_to_boolean(val));
  }
  return ts_value_boolean(0);
}

Value node_worker_threads_markAsUncloneable(Value object) {
  /* Mark object as uncloneable (simplified) */
  if (object.tag == TAG_OBJECT && object.as.object) {
    TSHashMap* obj = (TSHashMap*)object.as.object;
    ts_hashmap_set(obj, ts_string_new("_uncloneable"), ts_value_boolean(1));
  }
  return object;
}

Value node_worker_threads_moveMessagePortToContext(Value port, Value context) {
  /* Move port to a different context (simplified - just return port) */
  return port;
}

/* ==================== Entry Point ==================== */

void node_worker_threads_set_entry(TsWorkerEntryFn fn) {
  g_entry_fn = fn;
}

/* ==================== Worker Registry for Event Loop ==================== */

/* ==================== Event Loop Integration ==================== */

int ts_worker_pending(void) {
  /* Check if any workers have pending messages */
  return 0;  /* Simplified */
}

int ts_worker_poll(void) {
  /* Poll for worker messages and trigger callbacks */
  return 0;  /* Simplified */
}
