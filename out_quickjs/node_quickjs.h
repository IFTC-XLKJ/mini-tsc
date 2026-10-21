#ifndef NODE_QUICKJS_H
#define NODE_QUICKJS_H

#include "runtime.h"

/* QuickJS-backed JavaScript evaluation.
 *
 * A QjsContext wraps a JSRuntime + JSContext pair. Values crossing the
 * boundary are converted between QuickJS JSValue and mini-tsc Value
 * (number/string/boolean/null/array/object). Functions and other exotic
 * JS values surface as undefined.
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
/* dispose(ctx): release the runtime. Idempotent. */
Value node_quickjs_dispose(Value ctx);

#endif /* NODE_QUICKJS_H */