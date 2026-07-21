#define _CRT_SECURE_NO_WARNINGS
#include "runtime.h"
#include <stdio.h>

static TSPromise* as_promise(Value v) {
  if (v.tag != TAG_OBJECT || !v.as.object) return NULL;
  TSPromise* p = (TSPromise*)v.as.object;
  if (p->type_tag != PROMISE_TAG) return NULL;
  return p;
}

int ts_value_is_promise(Value v) {
  return as_promise(v) != NULL;
}

Value ts_promise_new(void) {
  TSPromise* p = (TSPromise*)ts_gc_alloc_kind(sizeof(TSPromise), GC_KIND_PROMISE);
  if (!p) return ts_value_undefined();
  p->type_tag = PROMISE_TAG;
  p->refcount = 1;
  p->state = PROMISE_PENDING;
  p->result = ts_value_undefined();
  p->onFulfilled = ts_value_undefined();
  p->onRejected = ts_value_undefined();
  p->onFinally = ts_value_undefined();
  p->then_promise = NULL;
  return ts_value_object(p);
}

typedef Value (*TsPromiseCb1)(Value arg);
typedef Value (*TsPromiseCb0)(void);

static void run_handlers(TSPromise* p) {
  if (!p || p->state == PROMISE_PENDING) return;

  if (p->state == PROMISE_FULFILLED) {
    if (p->onFulfilled.tag == TAG_FUNCTION && p->onFulfilled.as.function) {
      TsPromiseCb1 fn = (TsPromiseCb1)p->onFulfilled.as.function;
      Value out = fn(p->result);
      if (p->then_promise) {
        ts_promise_resolve(ts_value_object(p->then_promise), out);
      }
    } else if (p->then_promise) {
      ts_promise_resolve(ts_value_object(p->then_promise), p->result);
    }
  } else {
    if (p->onRejected.tag == TAG_FUNCTION && p->onRejected.as.function) {
      TsPromiseCb1 fn = (TsPromiseCb1)p->onRejected.as.function;
      Value out = fn(p->result);
      if (p->then_promise) {
        ts_promise_resolve(ts_value_object(p->then_promise), out);
      }
    } else if (p->then_promise) {
      ts_promise_reject(ts_value_object(p->then_promise), p->result);
    }
  }

  if (p->onFinally.tag == TAG_FUNCTION && p->onFinally.as.function) {
    TsPromiseCb0 fn = (TsPromiseCb0)p->onFinally.as.function;
    fn();
  }
}

Value ts_promise_resolve(Value pv, Value v) {
  TSPromise* p = as_promise(pv);
  if (!p) return pv;
  if (p->state != PROMISE_PENDING) return pv;
  p->state = PROMISE_FULFILLED;
  p->result = v;
  run_handlers(p);
  return pv;
}

Value ts_promise_reject(Value pv, Value err) {
  TSPromise* p = as_promise(pv);
  if (!p) return pv;
  if (p->state != PROMISE_PENDING) return pv;
  p->state = PROMISE_REJECTED;
  p->result = err;
  run_handlers(p);
  return pv;
}

Value ts_promise_then(Value pv, Value onFulfilled, Value onRejected) {
  TSPromise* p = as_promise(pv);
  Value next = ts_promise_new();
  if (!p) {
    ts_promise_resolve(next, pv);
    return next;
  }
  p->onFulfilled = onFulfilled;
  p->onRejected = onRejected;
  p->then_promise = as_promise(next);
  if (p->state != PROMISE_PENDING) {
    run_handlers(p);
  }
  return next;
}

Value ts_promise_catch(Value pv, Value onRejected) {
  return ts_promise_then(pv, ts_value_undefined(), onRejected);
}

Value ts_promise_finally(Value pv, Value onFinally) {
  TSPromise* p = as_promise(pv);
  if (!p) return pv;
  p->onFinally = onFinally;
  if (p->state != PROMISE_PENDING) {
    run_handlers(p);
  }
  return pv;
}

/* Declared in builtins.c / runtime.h */
extern int ts_timers_pending(void);
extern int ts_timers_poll(void);

Value ts_await(Value v) {
  TSPromise* p = as_promise(v);
  if (!p) return v;

  while (p->state == PROMISE_PENDING) {
    /* Prefer timers first (setTimeout(resolve)); then I/O completions */
    int t = 0;
    if (ts_timers_pending()) {
      t = ts_timers_poll();
    }
    if (p->state != PROMISE_PENDING) break;
    int n = ts_completion_poll();
    if (p->state != PROMISE_PENDING) break;
    if (n == 0 && t == 0) {
      if (ts_jobs_pending()) {
        ts_completion_wait(50);
      } else if (ts_timers_pending()) {
        /* Sleep until next timer (poll sleeps up to 50ms) */
        ts_timers_poll();
      } else {
        /* Nothing will settle this promise */
        break;
      }
    }
  }

  if (p->state == PROMISE_REJECTED) {
    TS_THROW(p->result);
    return ts_value_undefined();
  }
  if (p->state == PROMISE_FULFILLED) return p->result;
  return ts_value_undefined();
}

static Value make_resolver(TSPromise* p, int is_reject) {
  PromiseResolver* r = (PromiseResolver*)malloc(sizeof(PromiseResolver));
  if (!r) return ts_value_undefined();
  r->type_tag = PROMISE_RESOLVE_TAG;
  r->promise = p;
  r->is_reject = is_reject;
  return ts_value_object(r);
}

/* Promise constructor: new Promise((resolve, reject) => { ... })
 * Executor is invoked synchronously with resolve/reject binders.
 * Binders are TAG_OBJECT (PROMISE_RESOLVE_TAG) and work as setTimeout callbacks. */
Value Promise_constructor(Value executor) {
  Value p = ts_promise_new();
  TSPromise* pp = as_promise(p);
  if (!pp) return p;

  Value resolve = make_resolver(pp, 0);
  Value reject = make_resolver(pp, 1);

  /* BoundFn (captures) or plain function — pass resolve as first call arg */
  if (executor.tag == TAG_FUNCTION ||
      (executor.tag == TAG_OBJECT && executor.as.object &&
       *((int32_t*)executor.as.object) == BOUND_FN_TAG)) {
    Value args[2] = { resolve, reject };
    ts_value_call(executor, args, 2);
  }
  return p;
}
