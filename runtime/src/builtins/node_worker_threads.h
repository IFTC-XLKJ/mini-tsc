#ifndef NODE_WORKER_THREADS_H
#define NODE_WORKER_THREADS_H

#include "runtime.h"

/* Entry hook: main() registers the program entry so worker threads can re-enter it. */
typedef void (*TsWorkerEntryFn)(void);
void node_worker_threads_set_entry(TsWorkerEntryFn fn);

/* Event loop integration (main thread) */
int  ts_worker_pending(void);
int  ts_worker_poll(void);

/* Module-level getters / constructors */
Value node_worker_threads_isMainThread(void);
Value node_worker_threads_parentPort(void);
Value node_worker_threads_workerData(void);
Value node_worker_threads_threadId(void);
Value node_worker_threads_threadName(void);
Value node_worker_threads_isInternalThread(void);
Value node_worker_threads_SHARE_ENV(void);
Value node_worker_threads_resourceLimits(void);
Value node_worker_threads_locks(void);

Value node_worker_threads_Worker(Value filename, Value options);
Value node_worker_threads_MessageChannel(void);
Value node_worker_threads_MessagePort(void);
Value node_worker_threads_BroadcastChannel(Value name);

Value node_worker_threads_getEnvironmentData(Value key);
Value node_worker_threads_setEnvironmentData(Value key, Value value);
Value node_worker_threads_receiveMessageOnPort(Value port);
Value node_worker_threads_markAsUntransferable(Value object);
Value node_worker_threads_isMarkedAsUntransferable(Value object);
Value node_worker_threads_markAsUncloneable(Value object);
Value node_worker_threads_moveMessagePortToContext(Value port, Value context);
Value node_worker_threads_postMessageToThread(Value threadId, Value value, Value transferList);

/* Instance helpers (first arg = self) */
Value node_worker_threads_postMessage(Value self, Value value, Value transferList);
Value node_worker_threads_on(Value self, Value event, Value callback);
Value node_worker_threads_once(Value self, Value event, Value callback);
Value node_worker_threads_off(Value self, Value event, Value callback);
Value node_worker_threads_addListener(Value self, Value event, Value callback);
Value node_worker_threads_removeListener(Value self, Value event, Value callback);
Value node_worker_threads_terminate(Value self);
Value node_worker_threads_close(Value self);
Value node_worker_threads_start(Value self);
Value node_worker_threads_ref(Value self);
Value node_worker_threads_unref(Value self);
Value node_worker_threads_get_threadId(Value self);
Value node_worker_threads_get_threadName(Value self);

#endif /* NODE_WORKER_THREADS_H */
