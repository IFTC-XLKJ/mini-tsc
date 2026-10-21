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

  export { createContext, eval, evalFile, setGlobal, getGlobal, call, dispose };
}