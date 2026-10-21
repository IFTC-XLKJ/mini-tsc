#define _CRT_SECURE_NO_WARNINGS
#include "node_quickjs.h"
#include "quickjs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define QJS_CONTEXT_TAG 0x514A5343  /* 'QJSC' */
#define QJS_FUNCTION_TAG 0x514A5346 /* 'QJSF' */

/* Opaque handle stored as TAG_OBJECT. JSRuntime/JSContext are raw QuickJS
 * objects; lifetime is owned here and released in dispose(). Function handles
 * created under this context are tracked so dispose() can release their
 * JSValue references. */
typedef struct QjsFunction QjsFunction;
typedef struct {
  int32_t type_tag;
  JSRuntime* rt;
  JSContext* ctx;
  int disposed;
  QjsFunction* functions; /* singly-linked list of live function handles */
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
  return ts_value_object(c);
}

Value node_quickjs_eval(Value ctxVal, Value codeVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.eval: invalid or disposed context");
  const char* code = value_cstr(codeVal);
  JSValue result = JS_Eval(c->ctx, code, strlen(code), "<eval>", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(result)) {
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
  JSValue result = JS_Eval(c->ctx, buf, rd, path, JS_EVAL_TYPE_GLOBAL);
  free(buf);
  if (JS_IsException(result)) {
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
  JSValue result = JS_Call(c->ctx, f->fn, JS_UNDEFINED, argc, argv);
  for (int i = 0; i < argc; i++) JS_FreeValue(c->ctx, argv[i]);
  if (JS_IsException(result)) {
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
    /* 0/negative/NaN/Infinity → unlimited (matches JS_SetMemoryLimit). */
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