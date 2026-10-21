/** mini-tsc ambient types for the `quickjs` module (embed a JavaScript engine). */
declare module "quickjs" {
  /** An isolated QuickJS context. Values are converted between JS and TS. */
  export type Context = unknown;

  /** Create a new isolated JavaScript context. */
  function createContext(): Context;

  /** Evaluate a script string in the context; returns the completion value.
   * A JS function result comes back as a callable handle for `call`. */
  function eval(ctx: Context, code: string): any;

  /** Evaluate a script read from a file on disk. */
  function evalFile(ctx: Context, path: string): any;

  /** Define or replace a global binding in the context. A function value is
   * exposed to JS as a callable function. */
  function setGlobal(ctx: Context, name: string, value: any): void;

  /** Read a global binding from the context. */
  function getGlobal(ctx: Context, name: string): any;

  /** Invoke a JS function handle (from `eval`/`getGlobal`) with an optional
   * array of arguments; the JS return value is converted back to TS. */
  function call(ctx: Context, fn: any, args?: any[]): any;

  /** Release the context and its runtime. Idempotent. */
  function dispose(ctx: Context): void;

  /** Cap the runtime's total malloc'd bytes. A value `<= 0` (or non-finite)
   * removes the limit. Once exceeded, the engine throws a JS "out of memory"
   * error on the next allocation, surfacing as a TS exception. */
  function setMemoryLimit(ctx: Context, bytes: number): void;

  /** A snapshot of the runtime's memory accounting. Byte fields unless the name
   * ends in `Count` (object counts). Useful for observing `setMemoryLimit`. */
  function getMemoryUsage(ctx: Context): {
    mallocSize: number;
    mallocLimit: number;
    memoryUsedSize: number;
    mallocCount: number;
    memoryUsedCount: number;
    atomCount: number;
    strCount: number;
    strSize: number;
    objCount: number;
    objSize: number;
  };

  // -------------------------------------------------------------------------
  // Engine control
  // -------------------------------------------------------------------------

  /** Abort any single `eval`/`evalModule`/`call` that runs longer than `ms`
   * wall-clock milliseconds. `0`/negative/non-finite removes the limit. The
   * aborted run throws a TS error. */
  function setTimeLimit(ctx: Context, ms: number): void;

  /** The configured per-run time limit in milliseconds (0 = none). */
  function getTimeLimit(ctx: Context): any;

  /** Run a full garbage collection cycle. */
  function gc(ctx: Context): void;

  /** Trigger an automatic GC once malloc'd bytes since the last GC exceed
   * `bytes`. `0`/negative restores the engine default. */
  function setGCThreshold(ctx: Context, bytes: number): void;

  /** Cap the JS call-stack usage in bytes. */
  function setMaxStackSize(ctx: Context, bytes: number): void;

  /** Attach a descriptive string shown in engine diagnostics. */
  function setRuntimeInfo(ctx: Context, info: string): void;

  /** The embedded QuickJS engine version string. */
  function version(): string;

  /** Drain the promise microtask queue; returns the number of jobs executed. */
  function runPendingJobs(ctx: Context): any;

  // -------------------------------------------------------------------------
  // Evaluation variants
  // -------------------------------------------------------------------------

  /** Evaluate `code` as an ES module (for its side effects / global bindings). */
  function evalModule(ctx: Context, code: string): any;

  /** Compile a script into a callable handle without running it yet. Invoke the
   * returned handle with `call`. Throws on a syntax error. */
  function compile(ctx: Context, code: string): any;

  /** Like `eval`, but object/array results come back as live opaque handles
   * (for the reflect APIs) rather than being copied. */
  function evalRef(ctx: Context, code: string): any;

  /** Evaluate `code`, then deep-copy the result across the boundary via JSON.
   * Throws if the result is not JSON-serialisable. */
  function evalJSON(ctx: Context, code: string): any;

  // -------------------------------------------------------------------------
  // JSON helpers
  // -------------------------------------------------------------------------

  /** Parse a JSON string through the engine into a TS value. */
  function parseJSON(ctx: Context, json: string): any;

  /** Deep `JSON.stringify` of a TS value or handle. Returns the JSON text, or
   * `undefined` when the value is not serialisable. */
  function stringify(ctx: Context, value: any): string | undefined;

  // -------------------------------------------------------------------------
  // Global reflection / JSON transfer
  // -------------------------------------------------------------------------

  /** Whether a global binding exists. */
  function hasGlobal(ctx: Context, name: string): boolean;

  /** Remove a global binding. Returns whether the deletion succeeded. */
  function deleteGlobal(ctx: Context, name: string): boolean;

  /** An array of the enumerable global property names. */
  function globalKeys(ctx: Context): any;

  /** The JS `typeof` of a global, as a string. */
  function typeofGlobal(ctx: Context, name: string): string;

  /** Define a global from a TS value via a deep, isolated JSON round-trip. */
  function setGlobalJSON(ctx: Context, name: string, value: any): void;

  /** Read a global as deep JSON text. */
  function getGlobalJSON(ctx: Context, name: string): string | undefined;

  // -------------------------------------------------------------------------
  // Function handles
  // -------------------------------------------------------------------------

  /** Call a JS function handle with an explicit `this` and an optional array of
   * arguments (up to 8). */
  function apply(ctx: Context, fn: any, thisVal: any, args?: any[]): any;

  /** Call a constructor handle with `new` and an optional array of arguments. */
  function newInstance(ctx: Context, ctor: any, args?: any[]): any;

  /** Whether `v` is callable (a JS function handle or a TS function). */
  function isCallable(v: any): boolean;

  // -------------------------------------------------------------------------
  // Value handles: reflection over live JS objects/arrays
  // -------------------------------------------------------------------------

  /** Make an opaque live JS handle from a TS value. */
  function ref(ctx: Context, value: any): any;

  /** Copy a live JS handle back into a plain TS value. */
  function deref(ctx: Context, handle: any): any;

  /** Drop a handle's engine reference early (before `dispose`). */
  function releaseHandle(ctx: Context, handle: any): void;

  /** Read a property of a handle (or a plain TS value). */
  function getProperty(ctx: Context, obj: any, key: any): any;

  /** Set a property on a handle; returns whether the write succeeded. */
  function setProperty(ctx: Context, obj: any, key: any, value: any): boolean;

  /** `key in obj` for a handle. */
  function hasProperty(ctx: Context, obj: any, key: any): boolean;

  /** Delete a property from a handle. */
  function deleteProperty(ctx: Context, obj: any, key: any): boolean;

  /** Own enumerable string keys of a handle, as an array. */
  function getKeys(ctx: Context, obj: any): any;

  /** Own enumerable string values of a handle, as an array. */
  function getValues(ctx: Context, obj: any): any;

  /** `[key, value]` pairs of a handle, as an array. */
  function getEntries(ctx: Context, obj: any): any;

  /** A new JS object handle; `props` (a TS object) seeds its properties. */
  function createObject(ctx: Context, props?: any): any;

  /** A new JS array handle seeded from a TS array. */
  function createArray(ctx: Context, items?: any[]): any;

  /** Append to a JS array handle; returns the new length. */
  function arrayPush(ctx: Context, arr: any, value: any): any;

  /** Length of a JS array (or string) handle. */
  function arrayLength(ctx: Context, arr: any): any;

  // -------------------------------------------------------------------------
  // Introspection and coercion
  // -------------------------------------------------------------------------

  /** JS `typeof` of a TS value / handle, as a string. */
  function typeofValue(v: any): string;
  function isArray(v: any): boolean;
  function isObject(v: any): boolean;
  function isFunction(v: any): boolean;
  function isString(v: any): boolean;
  function isNumber(v: any): boolean;
  function isBoolean(v: any): boolean;
  function isNull(v: any): boolean;
  function isUndefined(v: any): boolean;

  /** Coercion to number, honouring a handle's own `valueOf`. */
  function toNumber(ctx: Context, v: any): number;
  /** Coercion to string, honouring a handle's own `toString`. */
  function toStringValue(ctx: Context, v: any): string;
  /** Coercion to boolean (JS truthiness). */
  function toBoolean(ctx: Context, v: any): boolean;

  /** JS `===` between two values/handles. */
  function strictEqual(ctx: Context, a: any, b: any): boolean;

  export {
    createContext, eval, evalFile, setGlobal, getGlobal, call, dispose,
    setMemoryLimit, getMemoryUsage,
    setTimeLimit, getTimeLimit, gc, setGCThreshold, setMaxStackSize,
    setRuntimeInfo, version, runPendingJobs,
    evalModule, compile, evalRef, evalJSON,
    parseJSON, stringify,
    hasGlobal, deleteGlobal, globalKeys, typeofGlobal, setGlobalJSON, getGlobalJSON,
    apply, newInstance, isCallable,
    ref, deref, releaseHandle,
    getProperty, setProperty, hasProperty, deleteProperty,
    getKeys, getValues, getEntries, createObject, createArray, arrayPush, arrayLength,
    typeofValue, isArray, isObject, isFunction, isString, isNumber, isBoolean,
    isNull, isUndefined, toNumber, toStringValue, toBoolean, strictEqual,
  };
}