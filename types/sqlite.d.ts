/** mini-tsc ambient types for the `sqlite` module (better-sqlite3-like sync API). */
declare module "sqlite" {
  interface RunResult {
    changes: number;
    lastInsertRowid: number;
  }

  interface Statement {
    run(...params: any[]): RunResult;
    get(...params: any[]): any;
    all(...params: any[]): any;
    iterate(...params: any[]): any;
    finalize(): void;
  }

  interface Database {
    exec(sql: string): void;
    prepare(sql: string): Statement;
    close(): void;
    pragma(source: string): any;
  }

  function open(filename?: string): Database;
  function Database(filename?: string): Database;

  export { open, Database, Statement, RunResult };
}
