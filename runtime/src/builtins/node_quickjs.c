#define _CRT_SECURE_NO_WARNINGS
#include "node_quickjs.h"
#include "quickjs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define QJS_CONTEXT_TAG 0x514A5343  /* 'QJSC' */
#define QJS_FUNCTION_TAG 0x514A5346 /* 'QJSF' */
#define QJS_VALUE_TAG 0x514A5356    /* 'QJSV' */

/* Opaque handle stored as TAG_OBJECT. JSRuntime/JSContext are raw QuickJS
 * objects; lifetime is owned here and released in dispose(). Function handles
 * created under this context are tracked so dispose() can release their
 * JSValue references. */
typedef struct QjsFunction QjsFunction;
typedef struct QjsValueHandle QjsValueHandle;
typedef struct {
  int32_t type_tag;
  JSRuntime* rt;
  JSContext* ctx;
  int disposed;
  QjsFunction* functions;     /* singly-linked list of live function handles */
  QjsValueHandle* values;     /* singly-linked list of live value handles */
  /* Optional wall-clock budget for a single eval/call, enforced through the
   * engine's interrupt handler. 0 = no limit. */
  double time_limit_ms;
  double deadline_ms;         /* absolute deadline for the current run */
  int timed_out;              /* set by the interrupt handler */
} QjsContext;

/* Opaque handle for a JS function value, so TS can hold and later call it.
 * Holds a JSValue (a reference owned by this handle) and links into the
 * owning context's list for release at dispose(). */
struct QjsFunction {
  int32_t type_tag;
  Value ctx; /* owning context handle, kept alive */
  JSValue fn;
  QjsFunction* next;
};

/* Opaque handle for any non-function JS value (object/array/etc). Lets TS keep
 * a live reference into the engine and manipulate it through the reflect APIs
 * (getProperty/setProperty/keys/...). Owned by the context; released on
 * dispose() or by releaseHandle(). */
struct QjsValueHandle {
  int32_t type_tag;
  Value ctx;
  JSValue value;
  QjsValueHandle* next;
};

static Value qjs_throw(const char* msg) {
  TS_THROW(ts_value_string(ts_string_new(msg ? msg : "quickjs error")));
  return ts_value_undefined();
}

static QjsContext* as_context(Value v) {
  if (v.tag != TAG_OBJECT || !v.as.object) return NULL;
  QjsContext* c = (QjsContext*)v.as.object;
  if (c->type_tag != QJS_CONTEXT_TAG) return NULL;
  return c;
}

static QjsFunction* as_function(Value v) {
  if (v.tag != TAG_OBJECT || !v.as.object) return NULL;
  QjsFunction* f = (QjsFunction*)v.as.object;
  if (f->type_tag != QJS_FUNCTION_TAG) return NULL;
  return f;
}

static QjsValueHandle* as_value_handle(Value v) {
  if (v.tag != TAG_OBJECT || !v.as.object) return NULL;
  QjsValueHandle* h = (QjsValueHandle*)v.as.object;
  if (h->type_tag != QJS_VALUE_TAG) return NULL;
  return h;
}

/* Unwrap a TS value that may be a JS function handle or a general value handle
 * into the underlying JSValue. Returns JS_UNDEFINED when the argument is a
 * plain TS value (the caller then converts with value_to_js). `*ok` reports
 * whether a JSValue was produced (already a reference the caller must free). */
static JSValue handle_to_js(JSContext* ctx, Value v, int* ok) {
  QjsFunction* f = as_function(v);
  if (f) {
    *ok = 1;
    return JS_DupValue(ctx, f->fn);
  }
  QjsValueHandle* h = as_value_handle(v);
  if (h) {
    *ok = 1;
    return JS_DupValue(ctx, h->value);
  }
  *ok = 0;
  return JS_UNDEFINED;
}

/* Convert any TS value or JS handle into a live JSValue (caller frees). */
static JSValue value_to_js(JSContext* ctx, Value v);
static JSValue any_to_js(JSContext* ctx, Value v) {
  int ok = 0;
  JSValue js = handle_to_js(ctx, v, &ok);
  return ok ? js : value_to_js(ctx, v);
}

/* Wall-clock interrupt handler installed on the runtime: trips once the
 * current run exceeds the context's deadline, aborting runaway scripts. */
static int qjs_interrupt(JSRuntime* rt, void* opaque) {
  (void)rt;
  QjsContext* c = (QjsContext*)opaque;
  if (!c || c->deadline_ms <= 0.0) return 0;
  struct timespec ts;
#ifdef _WIN32
  timespec_get(&ts, TIME_UTC);
#else
  clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
  double now = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
  if (now >= c->deadline_ms) {
    c->timed_out = 1;
    return 1; /* non-zero aborts execution */
  }
  return 0;
}

static double qjs_now_ms(void) {
  struct timespec ts;
#ifdef _WIN32
  timespec_get(&ts, TIME_UTC);
#else
  clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

/* Arm the per-run wall-clock budget (if any) before an engine entry point. */
static void begin_run(QjsContext* c) {
  c->timed_out = 0;
  c->deadline_ms = (c->time_limit_ms > 0.0) ? qjs_now_ms() + c->time_limit_ms : 0.0;
}

static void end_run(QjsContext* c) {
  c->deadline_ms = 0.0;
}

/* Distinct error message when the interrupt handler aborted the run. */
static Value throw_timeout(JSContext* ctx, QjsContext* c) {
  JSValue ex = JS_GetException(ctx);
  JS_FreeValue(ctx, ex);
  Value out = ts_value_string(ts_string_new("quickjs: execution time limit exceeded"));
  (void)c;
  TS_THROW(out);
  return ts_value_undefined();
}

/* Convert a QuickJS JSValue to a mini-tsc Value. Objects/arrays are copied
 * structurally; functions and other exotic values become undefined. */
static Value js_to_value(JSContext* ctx, JSValueConst js, int depth);

/* Wrap a JS function as an opaque TS handle. The handle owns a JSValue
 * reference; it is released in dispose() along with the context. */
static Value make_function_handle(Value ctxVal, JSValueConst js) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return ts_value_undefined();
  QjsFunction* f = (QjsFunction*)calloc(1, sizeof(QjsFunction));
  if (!f) return ts_value_undefined();
  f->type_tag = QJS_FUNCTION_TAG;
  f->ctx = ctxVal;
  f->fn = JS_DupValue(c->ctx, js);
  f->next = c->functions;
  c->functions = f;
  return ts_value_object(f);
}

/* Top-level JS->TS conversion: like js_to_value but represents JS functions as
 * callable handles. Used by eval/evalFile/getGlobal/call, where the owning
 * context handle is known. */
static Value js_to_value_top(Value ctxVal, JSContext* ctx, JSValueConst js) {
  if (JS_IsFunction(ctx, js)) return make_function_handle(ctxVal, js);
  return js_to_value(ctx, js, 0);
}

/* Wrap an arbitrary JS value as an opaque TS handle that keeps the engine
 * reference alive. Used by ref()/evalRef() and the reflect APIs. */
static Value make_value_handle(Value ctxVal, JSValueConst js) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return ts_value_undefined();
  QjsValueHandle* h = (QjsValueHandle*)calloc(1, sizeof(QjsValueHandle));
  if (!h) return ts_value_undefined();
  h->type_tag = QJS_VALUE_TAG;
  h->ctx = ctxVal;
  h->value = JS_DupValue(c->ctx, js);
  h->next = c->values;
  c->values = h;
  return ts_value_object(h);
}

static const char* value_cstr(Value v) {
  if (v.tag == TAG_STRING && v.as.string && v.as.string->data) return v.as.string->data;
  TSString* s = ts_to_string(v);
  return (s && s->data) ? s->data : "";
}

/* Convert a QuickJS JSValue to a mini-tsc Value. Objects/arrays are copied
 * structurally; functions and other exotic values become undefined. */
static Value js_to_value(JSContext* ctx, JSValueConst js, int depth) {
  if (depth > 32) return ts_value_undefined();
  if (JS_IsException(js)) return ts_value_undefined();
  if (JS_IsUndefined(js) || JS_IsNull(js)) return ts_value_undefined();
  if (JS_IsBool(js)) return ts_value_boolean(JS_ToBool(ctx, js) == 1);
  if (JS_IsNumber(js)) {
    double d = 0;
    JS_ToFloat64(ctx, &d, js);
    return ts_value_number(d);
  }
  if (JS_IsString(js)) {
    size_t len = 0;
    const char* s = JS_ToCStringLen(ctx, &len, js);
    if (!s) return ts_value_undefined();
    TSString* out = ts_string_new_len(s, (int32_t)len);
    JS_FreeCString(ctx, s);
    return ts_value_string(out);
  }
  if (JS_IsArray(ctx, js)) {
    JSValue lenVal = JS_GetPropertyStr(ctx, js, "length");
    int32_t len = 0;
    JS_ToInt32(ctx, &len, lenVal);
    JS_FreeValue(ctx, lenVal);
    TSArray* arr = ts_array_new();
    for (int32_t i = 0; i < len; i++) {
      JSValue item = JS_GetPropertyUint32(ctx, js, (uint32_t)i);
      ts_array_push(arr, js_to_value(ctx, item, depth + 1));
      JS_FreeValue(ctx, item);
    }
    return ts_value_array(arr);
  }
  if (JS_IsObject(js)) {
    TSHashMap* map = ts_hashmap_new();
    JSPropertyEnum* props = NULL;
    uint32_t nprops = 0;
    int flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
    if (JS_GetOwnPropertyNames(ctx, &props, &nprops, js, flags) == 0) {
      for (uint32_t i = 0; i < nprops; i++) {
        const char* key = JS_AtomToCString(ctx, props[i].atom);
        if (key) {
          JSValue item = JS_GetProperty(ctx, js, props[i].atom);
          /* ts_hashmap_set stores the key pointer (no retain) �?transfer ownership. */
          ts_hashmap_set(map, ts_string_new(key), js_to_value(ctx, item, depth + 1));
          JS_FreeValue(ctx, item);
          JS_FreeCString(ctx, key);
        }
      }
      JS_FreePropertyEnum(ctx, props, nprops);
    }
    return ts_value_object(map);
  }
  return ts_value_undefined();
}

/* JS `typeof` for a live JSValue, as a TS string. */
static Value js_typeof(JSContext* ctx, JSValueConst js) {
  if (JS_IsFunction(ctx, js)) return ts_value_string(ts_string_new("function"));
  if (JS_IsUndefined(js)) return ts_value_string(ts_string_new("undefined"));
  if (JS_IsNull(js)) return ts_value_string(ts_string_new("object"));
  if (JS_IsBool(js)) return ts_value_string(ts_string_new("boolean"));
  if (JS_IsNumber(js)) return ts_value_string(ts_string_new("number"));
  if (JS_IsString(js)) return ts_value_string(ts_string_new("string"));
  if (JS_IsObject(js)) return ts_value_string(ts_string_new("object"));
  return ts_value_string(ts_string_new("undefined"));
}

/* Convert a mini-tsc Value to a QuickJS JSValue (caller frees). */
static JSValue value_to_js(JSContext* ctx, Value v) {
  switch (v.tag) {
    case TAG_NUMBER: return JS_NewFloat64(ctx, v.as.number);
    case TAG_BOOLEAN: return JS_NewBool(ctx, v.as.boolean);
    case TAG_STRING:
      if (v.as.string && v.as.string->data) {
        return JS_NewStringLen(ctx, v.as.string->data, (size_t)v.as.string->length);
      }
      return JS_NewStringLen(ctx, "", 0);
    case TAG_ARRAY: {
      JSValue arr = JS_NewArray(ctx);
      if (v.as.array) {
        for (int32_t i = 0; i < v.as.array->length; i++) {
          JSValue item = value_to_js(ctx, v.as.array->items[i]);
          JS_SetPropertyUint32(ctx, arr, (uint32_t)i, item);
        }
      }
      return arr;
    }
    case TAG_OBJECT: {
      /* Plain hashmap objects cross as JS objects with string keys. */
      TSHashMap* map = (TSHashMap*)v.as.object;
      JSValue obj = JS_NewObject(ctx);
      if (map && map->entries) {
        for (int32_t i = 0; i < map->capacity; i++) {
          HashEntry* e = &map->entries[i];
          if (!e->occupied || !e->key || !e->key->data) continue;
          JS_SetPropertyStr(ctx, obj, e->key->data, value_to_js(ctx, e->value));
        }
      }
      return obj;
    }
    case TAG_NULL:
    default:
      return JS_UNDEFINED;
  }
}

/* The TS callback a JS-side wrapper carries. Stored on the C heap and owned by
 * the JSValue returned from JS_NewCFunctionData: our generic_callback frees it. */
typedef struct {
  Value fn; /* TAG_FUNCTION or BoundFn */
} QjsCallback;

static JSClassID callback_class_id;

/* Trampoline: JS calls the wrapper we handed it, and we forward to the TS
 * function stored in func_data[0]'s opaque payload. Mirrors the runtime's
 * 4-arg ceiling and has no `this`. */
static JSValue generic_callback(JSContext* ctx, JSValueConst this_val,
                                int argc, JSValueConst* argv, int magic,
                                JSValue* func_data) {
  (void)this_val;
  (void)magic;
  QjsCallback* cb = (QjsCallback*)JS_GetOpaque(func_data[0], callback_class_id);
  if (!cb) return JS_UNDEFINED;
  Value args[4];
  for (int i = 0; i < 4; i++) {
    args[i] = (i < argc) ? js_to_value(ctx, argv[i], 0) : ts_value_undefined();
  }
  Value result = ts_value_call(cb->fn, args, argc > 4 ? 4 : argc);
  return value_to_js(ctx, result);
}

static void callback_finalizer(JSRuntime* rt, JSValue val) {
  (void)rt;
  QjsCallback* cb = (QjsCallback*)JS_GetOpaque(val, callback_class_id);
  if (cb) {
    JS_SetOpaque(val, NULL);
    free(cb);
  }
}

static JSClassDef callback_class = {
    "TSCallback",
    callback_finalizer,
};

/* Allocate the class id + register the class on the runtime once. */
static int ensure_callback_class(JSRuntime* rt) {
  if (callback_class_id) return 0;
  JS_NewClassID(&callback_class_id);
  if (JS_NewClass(rt, callback_class_id, &callback_class) < 0) return -1;
  return 0;
}

/* True for a plain generated function or a bound-function object. */
static int is_callable(Value v) {
  if (v.tag == TAG_FUNCTION && v.as.function) return 1;
  if (v.tag == TAG_OBJECT && v.as.object &&
      *(int32_t*)v.as.object == BOUND_FN_TAG) {
    return 1;
  }
  return 0;
}

/* Wrap a TS function as a JS function. Returns JS_UNDEFINED when `v` is not
 * callable, so callers can fall back to the plain data conversion. */
static JSValue wrap_callback(JSContext* ctx, Value v) {
  if (!is_callable(v)) return JS_UNDEFINED;
  QjsCallback* cb = (QjsCallback*)malloc(sizeof(QjsCallback));
  if (!cb) return JS_UNDEFINED;
  cb->fn = v;
  JSValue data = JS_NewObjectClass(ctx, callback_class_id);
  if (JS_IsException(data)) {
    free(cb);
    return JS_UNDEFINED;
  }
  JS_SetOpaque(data, cb);
  JSValue fn = JS_NewCFunctionData(ctx, generic_callback, 0, 0, 1, &data);
  JS_FreeValue(ctx, data);
  return fn;
}

/* Read the pending exception into a TSString and throw it as a mini-tsc error. */
static Value throw_pending_exception(JSContext* ctx) {
  JSValue ex = JS_GetException(ctx);
  const char* msg = NULL;
  /* Prefer .message on Error objects, else stringify. */
  JSValue msgVal = JS_GetPropertyStr(ctx, ex, "message");
  if (!JS_IsUndefined(msgVal) && !JS_IsNull(msgVal)) {
    msg = JS_ToCString(ctx, msgVal);
  }
  if (!msg) msg = JS_ToCString(ctx, ex);
  Value out = ts_value_string(ts_string_new(msg ? msg : "quickjs evaluation error"));
  if (msg) JS_FreeCString(ctx, msg);
  JS_FreeValue(ctx, msgVal);
  JS_FreeValue(ctx, ex);
  TS_THROW(out);
  return ts_value_undefined();
}

Value node_quickjs_createContext(void) {
  QjsContext* c = (QjsContext*)calloc(1, sizeof(QjsContext));
  if (!c) return qjs_throw("quickjs: out of memory");
  c->type_tag = QJS_CONTEXT_TAG;
  c->rt = JS_NewRuntime();
  if (!c->rt) {
    free(c);
    return qjs_throw("quickjs: failed to create runtime");
  }
  c->ctx = JS_NewContext(c->rt);
  if (!c->ctx) {
    JS_FreeRuntime(c->rt);
    free(c);
    return qjs_throw("quickjs: failed to create context");
  }
  if (ensure_callback_class(c->rt) < 0) {
    JS_FreeContext(c->ctx);
    JS_FreeRuntime(c->rt);
    free(c);
    return qjs_throw("quickjs: failed to register callback class");
  }
  /* Install the wall-clock interrupt handler; a no-op until a time limit is set. */
  JS_SetInterruptHandler(c->rt, qjs_interrupt, c);
  return ts_value_object(c);
}

Value node_quickjs_eval(Value ctxVal, Value codeVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.eval: invalid or disposed context");
  const char* code = value_cstr(codeVal);
  begin_run(c);
  JSValue result = JS_Eval(c->ctx, code, strlen(code), "<eval>", JS_EVAL_TYPE_GLOBAL);
  end_run(c);
  if (JS_IsException(result)) {
    if (c->timed_out) return throw_timeout(c->ctx, c);
    return throw_pending_exception(c->ctx);
  }
  Value out = js_to_value_top(ctxVal, c->ctx, result);
  JS_FreeValue(c->ctx, result);
  return out;
}

Value node_quickjs_evalFile(Value ctxVal, Value pathVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.evalFile: invalid or disposed context");
  const char* path = value_cstr(pathVal);
  FILE* f = fopen(path, "rb");
  if (!f) return qjs_throw("quickjs.evalFile: cannot open file");
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size < 0) {
    fclose(f);
    return qjs_throw("quickjs.evalFile: cannot read file");
  }
  char* buf = (char*)malloc((size_t)size + 1);
  if (!buf) {
    fclose(f);
    return qjs_throw("quickjs.evalFile: out of memory");
  }
  size_t rd = fread(buf, 1, (size_t)size, f);
  fclose(f);
  buf[rd] = '\0';
  begin_run(c);
  JSValue result = JS_Eval(c->ctx, buf, rd, path, JS_EVAL_TYPE_GLOBAL);
  end_run(c);
  free(buf);
  if (JS_IsException(result)) {
    if (c->timed_out) return throw_timeout(c->ctx, c);
    return throw_pending_exception(c->ctx);
  }
  Value out = js_to_value_top(ctxVal, c->ctx, result);
  JS_FreeValue(c->ctx, result);
  return out;
}

Value node_quickjs_setGlobal(Value ctxVal, Value nameVal, Value valueVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.setGlobal: invalid or disposed context");
  const char* name = value_cstr(nameVal);
  JSValue global = JS_GetGlobalObject(c->ctx);
  /* A TS function crosses as a callable JS function so JS can invoke it. */
  JSValue js = wrap_callback(c->ctx, valueVal);
  if (JS_IsUndefined(js)) js = value_to_js(c->ctx, valueVal);
  JS_SetPropertyStr(c->ctx, global, name, js);
  JS_FreeValue(c->ctx, global);
  return ts_value_undefined();
}

Value node_quickjs_getGlobal(Value ctxVal, Value nameVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.getGlobal: invalid or disposed context");
  const char* name = value_cstr(nameVal);
  JSValue global = JS_GetGlobalObject(c->ctx);
  JSValue val = JS_GetPropertyStr(c->ctx, global, name);
  JS_FreeValue(c->ctx, global);
  if (JS_IsException(val)) {
    return throw_pending_exception(c->ctx);
  }
  Value out = js_to_value_top(ctxVal, c->ctx, val);
  JS_FreeValue(c->ctx, val);
  return out;
}

/* Call a JS function. `fnVal` is a handle from eval/evalFile/getGlobal whose
 * result was a function; `argsVal` is an optional TS array of arguments
 * (missing/undefined = no args). The JS return value is converted back. */
Value node_quickjs_call(Value ctxVal, Value fnVal, Value argsVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.call: invalid or disposed context");
  QjsFunction* f = as_function(fnVal);
  if (!f) return qjs_throw("quickjs.call: argument is not a JS function");
  JSValue argv[8];
  int argc = 0;
  if (argsVal.tag == TAG_ARRAY && argsVal.as.array) {
    TSArray* arr = argsVal.as.array;
    for (int32_t i = 0; i < arr->length && argc < 8; i++) {
      argv[argc++] = value_to_js(c->ctx, arr->items[i]);
    }
  }
  begin_run(c);
  JSValue result = JS_Call(c->ctx, f->fn, JS_UNDEFINED, argc, argv);
  for (int i = 0; i < argc; i++) JS_FreeValue(c->ctx, argv[i]);
  end_run(c);
  if (JS_IsException(result)) {
    if (c->timed_out) return throw_timeout(c->ctx, c);
    return throw_pending_exception(c->ctx);
  }
  Value out = js_to_value_top(ctxVal, c->ctx, result);
  JS_FreeValue(c->ctx, result);
  return out;
}

Value node_quickjs_dispose(Value ctxVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return ts_value_undefined();
  /* Release JSValue refs held by function handles before tearing down. */
  QjsFunction* f = c->functions;
  while (f) {
    QjsFunction* next = f->next;
    if (c->ctx) JS_FreeValue(c->ctx, f->fn);
    f->type_tag = 0;
    free(f);
    f = next;
  }
  c->functions = NULL;
  QjsValueHandle* h = c->values;
  while (h) {
    QjsValueHandle* next = h->next;
    if (c->ctx) JS_FreeValue(c->ctx, h->value);
    h->type_tag = 0;
    free(h);
    h = next;
  }
  c->values = NULL;
  if (c->ctx) {
    JS_FreeContext(c->ctx);
    c->ctx = NULL;
  }
  if (c->rt) {
    JS_FreeRuntime(c->rt);
    c->rt = NULL;
  }
  c->disposed = 1;
  return ts_value_undefined();
}

/* setMemoryLimit(ctx, bytes): cap the runtime's total malloc'd bytes. A
 * negative or non-finite value disables the limit (0 = unlimited, matching
 * JS_SetMemoryLimit). Once the cap is exceeded, the engine throws a JS
 * "out of memory" error on the next allocation, which surfaces as a TS
 * exception via eval/call. */
Value node_quickjs_setMemoryLimit(Value ctxVal, Value bytesVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.setMemoryLimit: invalid or disposed context");
  double n = ts_to_number(bytesVal);
  if (!(n > 0.0)) {
    /* 0/negative/NaN/Infinity �?unlimited (matches JS_SetMemoryLimit). */
    JS_SetMemoryLimit(c->rt, (size_t)-1);
    return ts_value_undefined();
  }
  JS_SetMemoryLimit(c->rt, (size_t)n);
  return ts_value_undefined();
}

/* getMemoryUsage(ctx): a snapshot of the runtime's memory accounting as a TS
 * object with the most useful JSMemoryUsage fields (bytes unless named
 * `*_count`). Handy for observing setMemoryLimit in action. */
Value node_quickjs_getMemoryUsage(Value ctxVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.getMemoryUsage: invalid or disposed context");
  JSMemoryUsage u;
  memset(&u, 0, sizeof(u));
  JS_ComputeMemoryUsage(c->rt, &u);
  TSHashMap* map = ts_hashmap_new();
  ts_hashmap_set(map, ts_string_new("mallocSize"), ts_value_number((double)u.malloc_size));
  ts_hashmap_set(map, ts_string_new("mallocLimit"), ts_value_number((double)u.malloc_limit));
  ts_hashmap_set(map, ts_string_new("memoryUsedSize"), ts_value_number((double)u.memory_used_size));
  ts_hashmap_set(map, ts_string_new("mallocCount"), ts_value_number((double)u.malloc_count));
  ts_hashmap_set(map, ts_string_new("memoryUsedCount"), ts_value_number((double)u.memory_used_count));
  ts_hashmap_set(map, ts_string_new("atomCount"), ts_value_number((double)u.atom_count));
  ts_hashmap_set(map, ts_string_new("strCount"), ts_value_number((double)u.str_count));
  ts_hashmap_set(map, ts_string_new("strSize"), ts_value_number((double)u.str_size));
  ts_hashmap_set(map, ts_string_new("objCount"), ts_value_number((double)u.obj_count));
  ts_hashmap_set(map, ts_string_new("objSize"), ts_value_number((double)u.obj_size));
  return ts_value_object(map);
}

/* ------------------------------------------------------------------------- *
 * Engine control
 * ------------------------------------------------------------------------- */

/* setTimeLimit(ctx, ms): abort any single eval/call that runs longer than
 * `ms` wall-clock milliseconds. 0/negative/non-finite removes the limit. The
 * aborted run throws a TS exception. */
Value node_quickjs_setTimeLimit(Value ctxVal, Value msVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.setTimeLimit: invalid or disposed context");
  double ms = ts_to_number(msVal);
  c->time_limit_ms = (ms > 0.0) ? ms : 0.0;
  return ts_value_undefined();
}

/* getTimeLimit(ctx): the currently configured per-run limit in ms (0 = none). */
Value node_quickjs_getTimeLimit(Value ctxVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.getTimeLimit: invalid or disposed context");
  return ts_value_number(c->time_limit_ms);
}

/* gc(ctx): run a full garbage collection cycle. */
Value node_quickjs_gc(Value ctxVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.gc: invalid or disposed context");
  JS_RunGC(c->rt);
  return ts_value_undefined();
}

/* setGCThreshold(ctx, bytes): trigger an automatic GC when malloc'd bytes since
 * the last GC exceed `bytes`. 0/negative restores the engine default. */
Value node_quickjs_setGCThreshold(Value ctxVal, Value bytesVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.setGCThreshold: invalid or disposed context");
  double n = ts_to_number(bytesVal);
  JS_SetGCThreshold(c->rt, (n > 0.0) ? (size_t)n : (size_t)-1);
  return ts_value_undefined();
}

/* setMaxStackSize(ctx, bytes): cap the JS call stack usage. */
Value node_quickjs_setMaxStackSize(Value ctxVal, Value bytesVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.setMaxStackSize: invalid or disposed context");
  double n = ts_to_number(bytesVal);
  if (n > 0.0) JS_SetMaxStackSize(c->rt, (size_t)n);
  return ts_value_undefined();
}

/* setRuntimeInfo(ctx, info): attach a descriptive string shown in diagnostics. */
Value node_quickjs_setRuntimeInfo(Value ctxVal, Value infoVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.setRuntimeInfo: invalid or disposed context");
  JS_SetRuntimeInfo(c->rt, value_cstr(infoVal));
  return ts_value_undefined();
}

/* version(): the embedded QuickJS engine version string. */
Value node_quickjs_version(void) {
  return ts_value_string(ts_string_new(CONFIG_VERSION));
}

/* runPendingJobs(ctx): drive the promise microtask queue to completion.
 * Returns the number of jobs executed. Useful after evaluating code that
 * schedules promise reactions (`Promise.resolve().then(...)`). */
Value node_quickjs_runPendingJobs(Value ctxVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.runPendingJobs: invalid or disposed context");
  int count = 0;
  JSContext* jobCtx = NULL;
  while (JS_ExecutePendingJob(c->rt, &jobCtx) > 0) {
    count++;
    if (count > 1000000) break; /* guard against self-scheduling loops */
  }
  return ts_value_number((double)count);
}

/* ------------------------------------------------------------------------- *
 * Evaluation variants
 * ------------------------------------------------------------------------- */

/* evalModule(ctx, code): evaluate `code` as an ES module. The completion value
 * of a module body is undefined; use it for its side effects / global bindings. */
Value node_quickjs_evalModule(Value ctxVal, Value codeVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.evalModule: invalid or disposed context");
  const char* code = value_cstr(codeVal);
  begin_run(c);
  JSValue result = JS_Eval(c->ctx, code, strlen(code), "<module>", JS_EVAL_TYPE_MODULE);
  end_run(c);
  if (JS_IsException(result)) {
    if (c->timed_out) return throw_timeout(c->ctx, c);
    return throw_pending_exception(c->ctx);
  }
  Value out = js_to_value_top(ctxVal, c->ctx, result);
  JS_FreeValue(c->ctx, result);
  return out;
}

/* compile(ctx, code): compile a script into a callable handle without running
 * it yet. Call the returned handle with quickjs.call. Throws on syntax error. */
Value node_quickjs_compile(Value ctxVal, Value codeVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.compile: invalid or disposed context");
  const char* code = value_cstr(codeVal);
  JSValue fn = JS_Eval(c->ctx, code, strlen(code), "<compile>",
                       JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(fn)) return throw_pending_exception(c->ctx);
  JSValue global = JS_GetGlobalObject(c->ctx);
  JSValue result = JS_EvalFunction(c->ctx, fn);
  JS_FreeValue(c->ctx, global);
  if (JS_IsException(result)) return throw_pending_exception(c->ctx);
  Value out = js_to_value_top(ctxVal, c->ctx, result);
  JS_FreeValue(c->ctx, result);
  return out;
}

/* evalRef(ctx, code): like eval, but object/array results come back as live
 * opaque handles (for the reflect APIs) instead of being copied. */
Value node_quickjs_evalRef(Value ctxVal, Value codeVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.evalRef: invalid or disposed context");
  const char* code = value_cstr(codeVal);
  begin_run(c);
  JSValue result = JS_Eval(c->ctx, code, strlen(code), "<evalRef>", JS_EVAL_TYPE_GLOBAL);
  end_run(c);
  if (JS_IsException(result)) {
    if (c->timed_out) return throw_timeout(c->ctx, c);
    return throw_pending_exception(c->ctx);
  }
  Value out = JS_IsFunction(c->ctx, result) ? make_function_handle(ctxVal, result)
                                            : make_value_handle(ctxVal, result);
  JS_FreeValue(c->ctx, result);
  return out;
}

/* evalJSON(ctx, code): evaluate `code`, then JSON.stringify the result and
 * parse it back �?a deep, dependency-free copy across the boundary that also
 * realises lazy values. Throws if the result is not JSON-serialisable. */
Value node_quickjs_evalJSON(Value ctxVal, Value codeVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.evalJSON: invalid or disposed context");
  const char* code = value_cstr(codeVal);
  begin_run(c);
  JSValue result = JS_Eval(c->ctx, code, strlen(code), "<evalJSON>", JS_EVAL_TYPE_GLOBAL);
  end_run(c);
  if (JS_IsException(result)) {
    if (c->timed_out) return throw_timeout(c->ctx, c);
    return throw_pending_exception(c->ctx);
  }
  JSValue json = JS_JSONStringify(c->ctx, result, JS_UNDEFINED, JS_UNDEFINED);
  JS_FreeValue(c->ctx, result);
  if (JS_IsException(json)) return throw_pending_exception(c->ctx);
  if (JS_IsUndefined(json)) {
    JS_FreeValue(c->ctx, json);
    return ts_value_undefined();
  }
  size_t len = 0;
  const char* s = JS_ToCStringLen(c->ctx, &len, json);
  JS_FreeValue(c->ctx, json);
  if (!s) return ts_value_undefined();
  TSString* out = ts_string_new_len(s, (int32_t)len);
  JS_FreeCString(c->ctx, s);
  return ts_value_string(out);
}

/* ------------------------------------------------------------------------- *
 * JSON helpers (operate through the engine so nested structures survive)
 * ------------------------------------------------------------------------- */

/* parseJSON(ctx, json): parse a JSON string via the engine into a TS value. */
Value node_quickjs_parseJSON(Value ctxVal, Value jsonVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.parseJSON: invalid or disposed context");
  const char* json = value_cstr(jsonVal);
  JSValue val = JS_ParseJSON(c->ctx, json, strlen(json), "<json>");
  if (JS_IsException(val)) return throw_pending_exception(c->ctx);
  Value out = js_to_value_top(ctxVal, c->ctx, val);
  JS_FreeValue(c->ctx, val);
  return out;
}

/* stringify(ctx, value): JSON.stringify a TS value (deep). Returns the JSON
 * text, or undefined when the value is not serialisable. */
Value node_quickjs_stringify(Value ctxVal, Value valueVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.stringify: invalid or disposed context");
  JSValue v = any_to_js(c->ctx, valueVal);
  JSValue json = JS_JSONStringify(c->ctx, v, JS_UNDEFINED, JS_UNDEFINED);
  JS_FreeValue(c->ctx, v);
  if (JS_IsException(json)) return throw_pending_exception(c->ctx);
  if (JS_IsUndefined(json)) {
    JS_FreeValue(c->ctx, json);
    return ts_value_undefined();
  }
  size_t len = 0;
  const char* s = JS_ToCStringLen(c->ctx, &len, json);
  JS_FreeValue(c->ctx, json);
  if (!s) return ts_value_undefined();
  TSString* out = ts_string_new_len(s, (int32_t)len);
  JS_FreeCString(c->ctx, s);
  return ts_value_string(out);
}

/* ------------------------------------------------------------------------- *
 * Globals: reflection and JSON transfer
 * ------------------------------------------------------------------------- */

/* hasGlobal(ctx, name): whether a global binding exists. */
Value node_quickjs_hasGlobal(Value ctxVal, Value nameVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.hasGlobal: invalid or disposed context");
  const char* name = value_cstr(nameVal);
  JSValue global = JS_GetGlobalObject(c->ctx);
  JSAtom atom = JS_NewAtom(c->ctx, name);
  int has = JS_HasProperty(c->ctx, global, atom);
  JS_FreeAtom(c->ctx, atom);
  JS_FreeValue(c->ctx, global);
  return ts_value_boolean(has == 1);
}

/* deleteGlobal(ctx, name): remove a global binding. Returns whether it existed. */
Value node_quickjs_deleteGlobal(Value ctxVal, Value nameVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.deleteGlobal: invalid or disposed context");
  const char* name = value_cstr(nameVal);
  JSValue global = JS_GetGlobalObject(c->ctx);
  JSAtom atom = JS_NewAtom(c->ctx, name);
  int ok = JS_DeleteProperty(c->ctx, global, atom, 0);
  JS_FreeAtom(c->ctx, atom);
  JS_FreeValue(c->ctx, global);
  return ts_value_boolean(ok >= 0);
}

/* globalKeys(ctx): an array of the enumerable global property names. */
Value node_quickjs_globalKeys(Value ctxVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.globalKeys: invalid or disposed context");
  JSValue global = JS_GetGlobalObject(c->ctx);
  JSPropertyEnum* props = NULL;
  uint32_t nprops = 0;
  int flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
  TSArray* arr = ts_array_new();
  if (JS_GetOwnPropertyNames(c->ctx, &props, &nprops, global, flags) == 0) {
    for (uint32_t i = 0; i < nprops; i++) {
      const char* key = JS_AtomToCString(c->ctx, props[i].atom);
      if (key) {
        ts_array_push(arr, ts_value_string(ts_string_new(key)));
        JS_FreeCString(c->ctx, key);
      }
    }
    JS_FreePropertyEnum(c->ctx, props, nprops);
  }
  JS_FreeValue(c->ctx, global);
  return ts_value_array(arr);
}

/* typeofGlobal(ctx, name): the JS `typeof` of a global (a string). */
Value node_quickjs_typeofGlobal(Value ctxVal, Value nameVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.typeofGlobal: invalid or disposed context");
  const char* name = value_cstr(nameVal);
  JSValue global = JS_GetGlobalObject(c->ctx);
  JSAtom atom = JS_NewAtom(c->ctx, name);
  JSValue tv = JS_GetProperty(c->ctx, global, atom);
  JS_FreeAtom(c->ctx, atom);
  JS_FreeValue(c->ctx, global);
  if (JS_IsException(tv)) return throw_pending_exception(c->ctx);
  Value out = js_typeof(c->ctx, tv);
  JS_FreeValue(c->ctx, tv);
  return out;
}

/* setGlobalJSON(ctx, name, value): define a global from a TS value by
 * round-tripping it through JSON �?deep, isolated, no cross-heap aliasing. */
Value node_quickjs_setGlobalJSON(Value ctxVal, Value nameVal, Value valueVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.setGlobalJSON: invalid or disposed context");
  const char* name = value_cstr(nameVal);
  JSValue v = value_to_js(c->ctx, valueVal);
  JSValue json = JS_JSONStringify(c->ctx, v, JS_UNDEFINED, JS_UNDEFINED);
  JS_FreeValue(c->ctx, v);
  if (JS_IsException(json)) return throw_pending_exception(c->ctx);
  if (JS_IsUndefined(json)) {
    JS_FreeValue(c->ctx, json);
    return qjs_throw("quickjs.setGlobalJSON: value is not JSON-serialisable");
  }
  size_t len = 0;
  const char* s = JS_ToCStringLen(c->ctx, &len, json);
  JS_FreeValue(c->ctx, json);
  if (!s) return qjs_throw("quickjs.setGlobalJSON: serialisation failed");
  JSValue parsed = JS_ParseJSON(c->ctx, s, len, name);
  JS_FreeCString(c->ctx, s);
  if (JS_IsException(parsed)) return throw_pending_exception(c->ctx);
  JSValue global = JS_GetGlobalObject(c->ctx);
  JS_SetPropertyStr(c->ctx, global, name, parsed);
  JS_FreeValue(c->ctx, global);
  return ts_value_undefined();
}

/* getGlobalJSON(ctx, name): read a global as a deep JSON-then-TS value. */
Value node_quickjs_getGlobalJSON(Value ctxVal, Value nameVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.getGlobalJSON: invalid or disposed context");
  const char* name = value_cstr(nameVal);
  JSValue global = JS_GetGlobalObject(c->ctx);
  JSValue val = JS_GetPropertyStr(c->ctx, global, name);
  JS_FreeValue(c->ctx, global);
  if (JS_IsException(val)) return throw_pending_exception(c->ctx);
  JSValue json = JS_JSONStringify(c->ctx, val, JS_UNDEFINED, JS_UNDEFINED);
  JS_FreeValue(c->ctx, val);
  if (JS_IsException(json)) return throw_pending_exception(c->ctx);
  if (JS_IsUndefined(json)) {
    JS_FreeValue(c->ctx, json);
    return ts_value_undefined();
  }
  size_t len = 0;
  const char* s = JS_ToCStringLen(c->ctx, &len, json);
  JS_FreeValue(c->ctx, json);
  if (!s) return ts_value_undefined();
  TSString* out = ts_string_new_len(s, (int32_t)len);
  JS_FreeCString(c->ctx, s);
  return ts_value_string(out);
}

/* ------------------------------------------------------------------------- *
 * Function handles: richer invocation
 * ------------------------------------------------------------------------- */

/* apply(ctx, fn, thisVal, args): call a JS function handle with an explicit
 * `this` and an optional TS array of arguments (up to 8). */
Value node_quickjs_apply(Value ctxVal, Value fnVal, Value thisVal, Value argsVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.apply: invalid or disposed context");
  QjsFunction* f = as_function(fnVal);
  if (!f) return qjs_throw("quickjs.apply: argument is not a JS function");
  JSValue thisJs = any_to_js(c->ctx, thisVal);
  JSValue argv[8];
  int argc = 0;
  if (argsVal.tag == TAG_ARRAY && argsVal.as.array) {
    TSArray* arr = argsVal.as.array;
    for (int32_t i = 0; i < arr->length && argc < 8; i++) {
      argv[argc++] = any_to_js(c->ctx, arr->items[i]);
    }
  }
  begin_run(c);
  JSValue result = JS_Call(c->ctx, f->fn, thisJs, argc, argv);
  end_run(c);
  for (int i = 0; i < argc; i++) JS_FreeValue(c->ctx, argv[i]);
  JS_FreeValue(c->ctx, thisJs);
  if (JS_IsException(result)) {
    if (c->timed_out) return throw_timeout(c->ctx, c);
    return throw_pending_exception(c->ctx);
  }
  Value out = js_to_value_top(ctxVal, c->ctx, result);
  JS_FreeValue(c->ctx, result);
  return out;
}

/* newInstance(ctx, ctor, args): call a constructor handle with `new`. */
Value node_quickjs_newInstance(Value ctxVal, Value ctorVal, Value argsVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.newInstance: invalid or disposed context");
  QjsFunction* f = as_function(ctorVal);
  if (!f) return qjs_throw("quickjs.newInstance: argument is not a JS constructor");
  JSValue argv[8];
  int argc = 0;
  if (argsVal.tag == TAG_ARRAY && argsVal.as.array) {
    TSArray* arr = argsVal.as.array;
    for (int32_t i = 0; i < arr->length && argc < 8; i++) {
      argv[argc++] = any_to_js(c->ctx, arr->items[i]);
    }
  }
  begin_run(c);
  JSValue result = JS_CallConstructor(c->ctx, f->fn, argc, argv);
  end_run(c);
  for (int i = 0; i < argc; i++) JS_FreeValue(c->ctx, argv[i]);
  if (JS_IsException(result)) {
    if (c->timed_out) return throw_timeout(c->ctx, c);
    return throw_pending_exception(c->ctx);
  }
  Value out = js_to_value_top(ctxVal, c->ctx, result);
  JS_FreeValue(c->ctx, result);
  return out;
}

/* isCallable(v): true when `v` is a JS function handle (or a TS function). */
Value node_quickjs_isCallable(Value v) {
  return ts_value_boolean(is_callable(v) || as_function(v) != NULL);
}

/* ------------------------------------------------------------------------- *
 * Value handles: reflection over live JS objects/arrays
 * ------------------------------------------------------------------------- */

/* ref(ctx, value): turn a TS value into an opaque live JS handle (or, for a JS
 * function result, a function handle). Counterpart: deref(). */
Value node_quickjs_ref(Value ctxVal, Value valueVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.ref: invalid or disposed context");
  /* Already a handle �?return as-is. */
  if (as_function(valueVal) || as_value_handle(valueVal)) return valueVal;
  JSValue js = value_to_js(c->ctx, valueVal);
  Value out = make_value_handle(ctxVal, js);
  JS_FreeValue(c->ctx, js);
  return out;
}

/* deref(ctx, handle): copy a live JS handle back into a plain TS value. */
Value node_quickjs_deref(Value ctxVal, Value handleVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.deref: invalid or disposed context");
  int ok = 0;
  JSValue js = handle_to_js(c->ctx, handleVal, &ok);
  if (!ok) return handleVal; /* plain values pass through unchanged */
  Value out = JS_IsFunction(c->ctx, js) ? make_function_handle(ctxVal, js)
                                        : js_to_value(c->ctx, js, 0);
  JS_FreeValue(c->ctx, js);
  return out;
}

/* releaseHandle(ctx, handle): drop a value/function handle's engine reference
 * before the context is disposed. Idempotent; the handle becomes inert. */
Value node_quickjs_releaseHandle(Value ctxVal, Value handleVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return ts_value_undefined();
  QjsValueHandle* h = as_value_handle(handleVal);
  if (h) {
    QjsValueHandle** pp = &c->values;
    while (*pp && *pp != h) pp = &(*pp)->next;
    if (*pp == h) *pp = h->next;
    if (c->ctx) JS_FreeValue(c->ctx, h->value);
    h->type_tag = 0;
    free(h);
  }
  return ts_value_undefined();
}

/* getProperty(ctx, obj, key): read a property of a JS handle (or TS value). */
Value node_quickjs_getProperty(Value ctxVal, Value objVal, Value keyVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.getProperty: invalid or disposed context");
  JSValue obj = any_to_js(c->ctx, objVal);
  JSValue key = any_to_js(c->ctx, keyVal);
  JSAtom atom = JS_ValueToAtom(c->ctx, key);
  JS_FreeValue(c->ctx, key);
  JSValue val = (atom != JS_ATOM_NULL) ? JS_GetProperty(c->ctx, obj, atom)
                                       : JS_UNDEFINED;
  if (atom != JS_ATOM_NULL) JS_FreeAtom(c->ctx, atom);
  JS_FreeValue(c->ctx, obj);
  if (JS_IsException(val)) return throw_pending_exception(c->ctx);
  Value out = js_to_value_top(ctxVal, c->ctx, val);
  JS_FreeValue(c->ctx, val);
  return out;
}

/* setProperty(ctx, obj, key, value): set a property on a JS handle. Returns
 * whether the write succeeded. */
Value node_quickjs_setProperty(Value ctxVal, Value objVal, Value keyVal, Value valueVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.setProperty: invalid or disposed context");
  JSValue obj = any_to_js(c->ctx, objVal);
  JSValue key = any_to_js(c->ctx, keyVal);
  JSValue val = any_to_js(c->ctx, valueVal);
  JSAtom atom = JS_ValueToAtom(c->ctx, key);
  JS_FreeValue(c->ctx, key);
  int rc = -1;
  if (atom != JS_ATOM_NULL) {
    /* JS_SetProperty consumes the `val` reference on success. */
    rc = JS_SetProperty(c->ctx, obj, atom, val);
    JS_FreeAtom(c->ctx, atom);
    if (rc < 0) {
      JS_FreeValue(c->ctx, obj);
      return throw_pending_exception(c->ctx);
    }
    JS_FreeValue(c->ctx, obj);
    return ts_value_boolean(rc == 1);
  }
  JS_FreeValue(c->ctx, val);
  JS_FreeValue(c->ctx, obj);
  return ts_value_boolean(0);
}

/* hasProperty(ctx, obj, key): `key in obj` for a JS handle. */
Value node_quickjs_hasProperty(Value ctxVal, Value objVal, Value keyVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.hasProperty: invalid or disposed context");
  JSValue obj = any_to_js(c->ctx, objVal);
  JSValue key = any_to_js(c->ctx, keyVal);
  JSAtom atom = JS_ValueToAtom(c->ctx, key);
  JS_FreeValue(c->ctx, key);
  int has = 0;
  if (atom != JS_ATOM_NULL) {
    has = JS_HasProperty(c->ctx, obj, atom);
    JS_FreeAtom(c->ctx, atom);
  }
  JS_FreeValue(c->ctx, obj);
  return ts_value_boolean(has == 1);
}

/* deleteProperty(ctx, obj, key): delete a property from a JS handle. */
Value node_quickjs_deleteProperty(Value ctxVal, Value objVal, Value keyVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.deleteProperty: invalid or disposed context");
  JSValue obj = any_to_js(c->ctx, objVal);
  JSValue key = any_to_js(c->ctx, keyVal);
  JSAtom atom = JS_ValueToAtom(c->ctx, key);
  JS_FreeValue(c->ctx, key);
  int rc = -1;
  if (atom != JS_ATOM_NULL) {
    rc = JS_DeleteProperty(c->ctx, obj, atom, 0);
    JS_FreeAtom(c->ctx, atom);
  }
  JS_FreeValue(c->ctx, obj);
  return ts_value_boolean(rc >= 0);
}

/* keys(ctx, obj): own enumerable string keys of a JS handle, as an array. */
Value node_quickjs_getKeys(Value ctxVal, Value objVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.keys: invalid or disposed context");
  JSValue obj = any_to_js(c->ctx, objVal);
  JSPropertyEnum* props = NULL;
  uint32_t nprops = 0;
  int flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
  TSArray* arr = ts_array_new();
  if (JS_GetOwnPropertyNames(c->ctx, &props, &nprops, obj, flags) == 0) {
    for (uint32_t i = 0; i < nprops; i++) {
      const char* key = JS_AtomToCString(c->ctx, props[i].atom);
      if (key) {
        ts_array_push(arr, ts_value_string(ts_string_new(key)));
        JS_FreeCString(c->ctx, key);
      }
    }
    JS_FreePropertyEnum(c->ctx, props, nprops);
  }
  JS_FreeValue(c->ctx, obj);
  return ts_value_array(arr);
}

/* values(ctx, obj): own enumerable string values of a JS handle, as an array. */
Value node_quickjs_getValues(Value ctxVal, Value objVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.values: invalid or disposed context");
  JSValue obj = any_to_js(c->ctx, objVal);
  JSPropertyEnum* props = NULL;
  uint32_t nprops = 0;
  int flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
  TSArray* arr = ts_array_new();
  if (JS_GetOwnPropertyNames(c->ctx, &props, &nprops, obj, flags) == 0) {
    for (uint32_t i = 0; i < nprops; i++) {
      JSValue item = JS_GetProperty(c->ctx, obj, props[i].atom);
      ts_array_push(arr, js_to_value_top(ctxVal, c->ctx, item));
      JS_FreeValue(c->ctx, item);
    }
    JS_FreePropertyEnum(c->ctx, props, nprops);
  }
  JS_FreeValue(c->ctx, obj);
  return ts_value_array(arr);
}

/* entries(ctx, obj): [key, value] pairs of a JS handle, as an array. */
Value node_quickjs_getEntries(Value ctxVal, Value objVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.entries: invalid or disposed context");
  JSValue obj = any_to_js(c->ctx, objVal);
  JSPropertyEnum* props = NULL;
  uint32_t nprops = 0;
  int flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
  TSArray* arr = ts_array_new();
  if (JS_GetOwnPropertyNames(c->ctx, &props, &nprops, obj, flags) == 0) {
    for (uint32_t i = 0; i < nprops; i++) {
      const char* key = JS_AtomToCString(c->ctx, props[i].atom);
      if (key) {
        JSValue item = JS_GetProperty(c->ctx, obj, props[i].atom);
        TSArray* pair = ts_array_new();
        ts_array_push(pair, ts_value_string(ts_string_new(key)));
        ts_array_push(pair, js_to_value(c->ctx, item, 0));
        ts_array_push(arr, ts_value_array(pair));
        JS_FreeValue(c->ctx, item);
        JS_FreeCString(c->ctx, key);
      }
    }
    JS_FreePropertyEnum(c->ctx, props, nprops);
  }
  JS_FreeValue(c->ctx, obj);
  return ts_value_array(arr);
}

/* createObject(ctx, props?): a new JS object handle; when `props` is given it
 * is a TS object whose fields become the new object's properties. */
Value node_quickjs_createObject(Value ctxVal, Value propsVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.createObject: invalid or disposed context");
  JSValue obj = JS_NewObject(c->ctx);
  if (propsVal.tag == TAG_OBJECT && propsVal.as.object) {
    TSHashMap* map = (TSHashMap*)propsVal.as.object;
    if (map && map->entries) {
      for (int32_t i = 0; i < map->capacity; i++) {
        HashEntry* e = &map->entries[i];
        if (!e->occupied || !e->key || !e->key->data) continue;
        JSValue v = any_to_js(c->ctx, e->value);
        JS_SetPropertyStr(c->ctx, obj, e->key->data, v);
      }
    }
  }
  Value out = make_value_handle(ctxVal, obj);
  JS_FreeValue(c->ctx, obj);
  return out;
}

/* createArray(ctx, items?): a new JS array handle seeded from a TS array. */
Value node_quickjs_createArray(Value ctxVal, Value itemsVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.createArray: invalid or disposed context");
  JSValue arr = JS_NewArray(c->ctx);
  if (itemsVal.tag == TAG_ARRAY && itemsVal.as.array) {
    TSArray* items = itemsVal.as.array;
    for (int32_t i = 0; i < items->length; i++) {
      JSValue v = any_to_js(c->ctx, items->items[i]);
      JS_SetPropertyUint32(c->ctx, arr, (uint32_t)i, v);
    }
  }
  Value out = make_value_handle(ctxVal, arr);
  JS_FreeValue(c->ctx, arr);
  return out;
}

/* arrayPush(ctx, handle, value): append to a JS array handle; returns new length. */
Value node_quickjs_arrayPush(Value ctxVal, Value arrVal, Value valueVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.arrayPush: invalid or disposed context");
  JSValue arr = any_to_js(c->ctx, arrVal);
  JSValue lenVal = JS_GetPropertyStr(c->ctx, arr, "length");
  int32_t len = 0;
  JS_ToInt32(c->ctx, &len, lenVal);
  JS_FreeValue(c->ctx, lenVal);
  JSValue v = any_to_js(c->ctx, valueVal);
  /* JS_SetPropertyUint32 consumes the `v` reference. */
  JS_SetPropertyUint32(c->ctx, arr, (uint32_t)len, v);
  JS_FreeValue(c->ctx, arr);
  return ts_value_number((double)(len + 1));
}

/* arrayLength(ctx, handle): length of a JS array (or string) handle. */
Value node_quickjs_arrayLength(Value ctxVal, Value arrVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.arrayLength: invalid or disposed context");
  JSValue arr = any_to_js(c->ctx, arrVal);
  JSValue lenVal = JS_GetPropertyStr(c->ctx, arr, "length");
  int32_t len = 0;
  JS_ToInt32(c->ctx, &len, lenVal);
  JS_FreeValue(c->ctx, lenVal);
  JS_FreeValue(c->ctx, arr);
  return ts_value_number((double)len);
}

/* ------------------------------------------------------------------------- *
 * Introspection and coercion over TS values
 * ------------------------------------------------------------------------- */

/* typeofValue(v): JS `typeof` semantics for a TS value. Handles report
 * "function" (a function handle) or "object" (a value handle). */
Value node_quickjs_typeofValue(Value v) {
  if (as_function(v)) return ts_value_string(ts_string_new("function"));
  if (as_value_handle(v)) return ts_value_string(ts_string_new("object"));
  switch (v.tag) {
    case TAG_NUMBER: return ts_value_string(ts_string_new("number"));
    case TAG_STRING: return ts_value_string(ts_string_new("string"));
    case TAG_BOOLEAN: return ts_value_string(ts_string_new("boolean"));
    case TAG_ARRAY: return ts_value_string(ts_string_new("object"));
    case TAG_FUNCTION: return ts_value_string(ts_string_new("function"));
    case TAG_OBJECT: return ts_value_string(ts_string_new("object"));
    default: return ts_value_string(ts_string_new("undefined"));
  }
}

/* isArray(v), isObject(v), isFunction(v), isString(v), isNumber(v),
 * isBoolean(v): predicate helpers mirroring the JS `Array.isArray` style. */
Value node_quickjs_isArray(Value v) {
  if (as_value_handle(v)) {
    QjsContext* c = as_context(((QjsValueHandle*)v.as.object)->ctx);
    if (c && !c->disposed) return ts_value_boolean(JS_IsArray(c->ctx, ((QjsValueHandle*)v.as.object)->value) == 1);
  }
  return ts_value_boolean(v.tag == TAG_ARRAY);
}

Value node_quickjs_isObject(Value v) {
  if (as_value_handle(v)) return ts_value_boolean(1);
  return ts_value_boolean(v.tag == TAG_OBJECT || v.tag == TAG_ARRAY);
}

Value node_quickjs_isFunction(Value v) {
  return ts_value_boolean(is_callable(v) || as_function(v) != NULL);
}

Value node_quickjs_isString(Value v) {
  return ts_value_boolean(v.tag == TAG_STRING);
}

Value node_quickjs_isNumber(Value v) {
  return ts_value_boolean(v.tag == TAG_NUMBER);
}

Value node_quickjs_isBoolean(Value v) {
  return ts_value_boolean(v.tag == TAG_BOOLEAN);
}

Value node_quickjs_isNull(Value v) {
  return ts_value_boolean(v.tag == TAG_NULL);
}

Value node_quickjs_isUndefined(Value v) {
  return ts_value_boolean(v.tag != TAG_NUMBER && v.tag != TAG_STRING &&
                         v.tag != TAG_BOOLEAN && v.tag != TAG_ARRAY &&
                         v.tag != TAG_OBJECT && v.tag != TAG_FUNCTION &&
                         v.tag != TAG_NULL);
}

/* toNumber(ctx, v) / toString(ctx, v) / toBoolean(ctx, v): coercion helpers
 * that honour a JS handle's own valueOf/toString when applicable. */
Value node_quickjs_toNumber(Value ctxVal, Value v) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return ts_value_number(ts_to_number(v));
  int ok = 0;
  JSValue js = handle_to_js(c->ctx, v, &ok);
  if (!ok) return ts_value_number(ts_to_number(v));
  double d = 0;
  JS_ToFloat64(c->ctx, &d, js);
  JS_FreeValue(c->ctx, js);
  return ts_value_number(d);
}

Value node_quickjs_toStringValue(Value ctxVal, Value v) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return ts_value_string(ts_to_string(v));
  int ok = 0;
  JSValue js = handle_to_js(c->ctx, v, &ok);
  if (!ok) return ts_value_string(ts_to_string(v));
  const char* s = JS_ToCString(c->ctx, js);
  JS_FreeValue(c->ctx, js);
  if (!s) return ts_value_string(ts_string_new(""));
  Value out = ts_value_string(ts_string_new(s));
  JS_FreeCString(c->ctx, s);
  return out;
}

Value node_quickjs_toBoolean(Value ctxVal, Value v) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return ts_value_boolean(ts_to_boolean(v));
  int ok = 0;
  JSValue js = handle_to_js(c->ctx, v, &ok);
  if (!ok) return ts_value_boolean(ts_to_boolean(v));
  int b = JS_ToBool(c->ctx, js);
  JS_FreeValue(c->ctx, js);
  return ts_value_boolean(b == 1);
}

/* strictEqual(ctx, a, b): JS `===` between two values/handles. */
Value node_quickjs_strictEqual(Value ctxVal, Value aVal, Value bVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return ts_value_boolean(0);
  JSValue a = any_to_js(c->ctx, aVal);
  JSValue b = any_to_js(c->ctx, bVal);
  int eq = JS_StrictEq(c->ctx, a, b);
  JS_FreeValue(c->ctx, a);
  JS_FreeValue(c->ctx, b);
  return ts_value_boolean(eq == 1);
}