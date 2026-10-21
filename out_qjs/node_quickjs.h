#ifndef NODE_QUICKJS_H
#define NODE_QUICKJS_H

#include "runtime.h"

/* QuickJS-backed JavaScript evaluation.
 *
 * A QjsContext wraps a JSRuntime + JSContext pair. Values crossing the
 * boundary are converted between QuickJS JSValue and mini-tsc Value
 * (number/string/boolean/null/array/object). A JS function surfaces as an
 * opaque callable handle (see node_quickjs_call); other exotic values become
 * undefined. A TS function passed to setGlobal is callable from JS.
 */

/* createContext(): new isolated JS context. Returns an opaque QjsContext. */
Value node_quickjs_createContext(void);
/* eval(ctx, script): evaluate a script string, return the completion value. */
Value node_quickjs_eval(Value ctx, Value code);
/* evalFile(ctx, path): evaluate a script from a file on disk. */
Value node_quickjs_evalFile(Value ctx, Value path);
/* setGlobal(ctx, name, value): define/replace a global binding. */
Value node_quickjs_setGlobal(Value ctx, Value name, Value value);
/* getGlobal(ctx, name): read a global binding. */
Value node_quickjs_getGlobal(Value ctx, Value name);
/* call(ctx, fn, args): invoke a JS function handle with an optional args array. */
Value node_quickjs_call(Value ctx, Value fn, Value args);
/* dispose(ctx): release the runtime. Idempotent. */
Value node_quickjs_dispose(Value ctx);
/* setMemoryLimit(ctx, bytes): cap the runtime's total malloc'd bytes.
 * 0/negative = unlimited. Exceeding it throws a JS "out of memory" error. */
Value node_quickjs_setMemoryLimit(Value ctx, Value bytes);
/* getMemoryUsage(ctx): snapshot of the runtime memory accounting as an object. */
Value node_quickjs_getMemoryUsage(Value ctx);

/* --- Engine control --- */

/* setTimeLimit(ctx, ms): abort a single eval/call exceeding `ms` wall-clock
 * milliseconds. 0/negative removes the limit; the aborted run throws. */
Value node_quickjs_setTimeLimit(Value ctx, Value ms);
/* getTimeLimit(ctx): the configured per-run limit in ms (0 = none). */
Value node_quickjs_getTimeLimit(Value ctx);
/* gc(ctx): run a full garbage collection cycle. */
Value node_quickjs_gc(Value ctx);
/* setGCThreshold(ctx, bytes): auto-GC once malloc'd bytes exceed `bytes`. */
Value node_quickjs_setGCThreshold(Value ctx, Value bytes);
/* setMaxStackSize(ctx, bytes): cap JS call-stack usage. */
Value node_quickjs_setMaxStackSize(Value ctx, Value bytes);
/* setRuntimeInfo(ctx, info): attach a descriptive string for diagnostics. */
Value node_quickjs_setRuntimeInfo(Value ctx, Value info);
/* version(): the embedded QuickJS engine version string. */
Value node_quickjs_version(void);
/* runPendingJobs(ctx): drain the promise microtask queue; returns jobs run. */
Value node_quickjs_runPendingJobs(Value ctx);

/* --- Evaluation variants --- */

/* evalModule(ctx, code): evaluate `code` as an ES module. */
Value node_quickjs_evalModule(Value ctx, Value code);
/* compile(ctx, code): compile a script to a callable handle without running it. */
Value node_quickjs_compile(Value ctx, Value code);
/* evalRef(ctx, code): like eval, but object/array results are live handles. */
Value node_quickjs_evalRef(Value ctx, Value code);
/* evalJSON(ctx, code): evaluate then deep-copy the result via JSON. */
Value node_quickjs_evalJSON(Value ctx, Value code);

/* --- JSON helpers --- */

/* parseJSON(ctx, json): parse a JSON string via the engine. */
Value node_quickjs_parseJSON(Value ctx, Value json);
/* stringify(ctx, value): deep JSON.stringify of a TS value / handle. */
Value node_quickjs_stringify(Value ctx, Value value);

/* --- Global reflection / JSON transfer --- */

/* hasGlobal(ctx, name): whether a global binding exists. */
Value node_quickjs_hasGlobal(Value ctx, Value name);
/* deleteGlobal(ctx, name): remove a global binding. */
Value node_quickjs_deleteGlobal(Value ctx, Value name);
/* globalKeys(ctx): array of enumerable global property names. */
Value node_quickjs_globalKeys(Value ctx);
/* typeofGlobal(ctx, name): JS `typeof` of a global, as a string. */
Value node_quickjs_typeofGlobal(Value ctx, Value name);
/* setGlobalJSON(ctx, name, value): define a global via a deep JSON round-trip. */
Value node_quickjs_setGlobalJSON(Value ctx, Value name, Value value);
/* getGlobalJSON(ctx, name): read a global as deep JSON text. */
Value node_quickjs_getGlobalJSON(Value ctx, Value name);

/* --- Function handles --- */

/* apply(ctx, fn, thisVal, args): call a function handle with explicit `this`. */
Value node_quickjs_apply(Value ctx, Value fn, Value thisVal, Value args);
/* newInstance(ctx, ctor, args): call a constructor handle with `new`. */
Value node_quickjs_newInstance(Value ctx, Value ctor, Value args);
/* isCallable(v): whether `v` is callable (JS or TS function). */
Value node_quickjs_isCallable(Value v);

/* --- Value handles --- */

/* ref(ctx, value): make an opaque live JS handle from a TS value. */
Value node_quickjs_ref(Value ctx, Value value);
/* deref(ctx, handle): copy a live JS handle back into a plain TS value. */
Value node_quickjs_deref(Value ctx, Value handle);
/* releaseHandle(ctx, handle): drop a handle's engine reference early. */
Value node_quickjs_releaseHandle(Value ctx, Value handle);
/* getProperty(ctx, obj, key): read a property of a handle/TS value. */
Value node_quickjs_getProperty(Value ctx, Value obj, Value key);
/* setProperty(ctx, obj, key, value): set a property on a handle. */
Value node_quickjs_setProperty(Value ctx, Value obj, Value key, Value value);
/* hasProperty(ctx, obj, key): `key in obj` for a handle. */
Value node_quickjs_hasProperty(Value ctx, Value obj, Value key);
/* deleteProperty(ctx, obj, key): delete a property from a handle. */
Value node_quickjs_deleteProperty(Value ctx, Value obj, Value key);
/* keys(ctx, obj): own enumerable string keys of a handle, as an array. */
Value node_quickjs_getKeys(Value ctx, Value obj);
/* values(ctx, obj): own enumerable string values of a handle, as an array. */
Value node_quickjs_getValues(Value ctx, Value obj);
/* entries(ctx, obj): [key, value] pairs of a handle, as an array. */
Value node_quickjs_getEntries(Value ctx, Value obj);
/* createObject(ctx, props?): a new JS object handle. */
Value node_quickjs_createObject(Value ctx, Value props);
/* createArray(ctx, items?): a new JS array handle seeded from a TS array. */
Value node_quickjs_createArray(Value ctx, Value items);
/* arrayPush(ctx, handle, value): append to a JS array handle. */
Value node_quickjs_arrayPush(Value ctx, Value arr, Value value);
/* arrayLength(ctx, handle): length of a JS array/string handle. */
Value node_quickjs_arrayLength(Value ctx, Value arr);

/* --- Introspection and coercion --- */

/* typeofValue(v): JS `typeof` of a TS value / handle, as a string. */
Value node_quickjs_typeofValue(Value v);
Value node_quickjs_isArray(Value v);
Value node_quickjs_isObject(Value v);
Value node_quickjs_isString(Value v);
Value node_quickjs_isNumber(Value v);
Value node_quickjs_isBoolean(Value v);
/* toNumber/toString/toBoolean(ctx, v): coercion honouring handle semantics. */
Value node_quickjs_toNumber(Value ctx, Value v);
Value node_quickjs_toStringValue(Value ctx, Value v);
Value node_quickjs_toBoolean(Value ctx, Value v);
/* strictEqual(ctx, a, b): JS `===` between two values/handles. */
Value node_quickjs_strictEqual(Value ctx, Value a, Value b);

#endif /* NODE_QUICKJS_H */