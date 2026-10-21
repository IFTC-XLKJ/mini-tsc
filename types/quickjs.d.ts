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

  export { createContext, eval, evalFile, setGlobal, getGlobal, call, dispose, setMemoryLimit, getMemoryUsage };
}