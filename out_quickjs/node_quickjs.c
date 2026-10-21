#define _CRT_SECURE_NO_WARNINGS
#include "node_quickjs.h"
#include "ts_features.h"
#include "quickjs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define QJS_CONTEXT_TAG 0x514A5343  /* 'QJSC' */

/* Opaque handle stored as TAG_OBJECT. JSRuntime/JSContext are raw QuickJS
 * objects; lifetime is owned here and released in dispose(). */
typedef struct {
  int32_t type_tag;
  JSRuntime* rt;
  JSContext* ctx;
  int disposed;
} QjsContext;

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

static const char* value_cstr(Value v) {
  if (v.tag == TAG_STRING && v.as.string && v.as.string->data) return v.as.string->data;
  TSString* s = ts_to_string(v);
  return (s && s->data) ? s->data : "";
}

/* Convert a QuickJS JSValue to a mini-tsc Value. Objects/arrays are copied
 * structurally; functions and other exotic values become undefined. */
static Value js_to_value(JSContext* ctx, JSValueConst js, int depth);

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
          /* ts_hashmap_set stores the key pointer (no retain) — transfer ownership. */
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

#if defined(TS_NEED_node_quickjs_createContext)
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
  return ts_value_object(c);
}
#endif /* TS_NEED_node_quickjs_createContext */

#if defined(TS_NEED_node_quickjs_eval)
Value node_quickjs_eval(Value ctxVal, Value codeVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.eval: invalid or disposed context");
  const char* code = value_cstr(codeVal);
  JSValue result = JS_Eval(c->ctx, code, strlen(code), "<eval>", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(result)) {
    return throw_pending_exception(c->ctx);
  }
  Value out = js_to_value(c->ctx, result, 0);
  JS_FreeValue(c->ctx, result);
  return out;
}
#endif /* TS_NEED_node_quickjs_eval */

#if defined(TS_NEED_node_quickjs_evalFile)
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
  Value out = js_to_value(c->ctx, result, 0);
  JS_FreeValue(c->ctx, result);
  return out;
}
#endif /* TS_NEED_node_quickjs_evalFile */

#if defined(TS_NEED_node_quickjs_setGlobal)
Value node_quickjs_setGlobal(Value ctxVal, Value nameVal, Value valueVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return qjs_throw("quickjs.setGlobal: invalid or disposed context");
  const char* name = value_cstr(nameVal);
  JSValue global = JS_GetGlobalObject(c->ctx);
  JSValue js = value_to_js(c->ctx, valueVal);
  JS_SetPropertyStr(c->ctx, global, name, js);
  JS_FreeValue(c->ctx, global);
  return ts_value_undefined();
}
#endif /* TS_NEED_node_quickjs_setGlobal */

#if defined(TS_NEED_node_quickjs_getGlobal)
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
  Value out = js_to_value(c->ctx, val, 0);
  JS_FreeValue(c->ctx, val);
  return out;
}
#endif /* TS_NEED_node_quickjs_getGlobal */

#if defined(TS_NEED_node_quickjs_dispose)
Value node_quickjs_dispose(Value ctxVal) {
  QjsContext* c = as_context(ctxVal);
  if (!c || c->disposed) return ts_value_undefined();
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
#endif /* TS_NEED_node_quickjs_dispose */