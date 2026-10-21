/** mini-tsc ambient types for the `quickjs` module (embed a JavaScript engine). */
declare module "quickjs" {
  /** An isolated QuickJS context. Values are converted between JS and TS. */
  export type Context = unknown;

  /** Create a new isolated JavaScript context. */
  function createContext(): Context;

  /** Evaluate a script string in the context; returns the completion value. */
  function eval(ctx: Context, code: string): any;

  /** Evaluate a script read from a file on disk. */
  function evalFile(ctx: Context, path: string): any;

  /** Define or replace a global binding in the context. */
  function setGlobal(ctx: Context, name: string, value: any): void;

  /** Read a global binding from the context. */
  function getGlobal(ctx: Context, name: string): any;

  /** Release the context and its runtime. Idempotent. */
  function dispose(ctx: Context): void;

  export { createContext, eval, evalFile, setGlobal, getGlobal, dispose };
}