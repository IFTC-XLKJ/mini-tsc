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

#endif /* NODE_QUICKJS_H */