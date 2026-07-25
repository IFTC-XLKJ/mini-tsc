/** mini-tsc ambient types for the `sqlite` module (SQL + simple CRUD). */
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

  /** Column types: "text" | "string" | "int" | "integer" | "number" | "real" | "float" | "bool" | "blob"
   *  or free-form SQLite type like "INTEGER PRIMARY KEY". */
  type ColumnType = string;

  interface Database {
    /* SQL API */
    exec(sql: string): void;
    prepare(sql: string): Statement;
    close(): void;
    pragma(source: string): any;

    /* Simple CRUD — no SQL required */
    createTable(table: string, columns: { [name: string]: ColumnType }): void;
    dropTable(table: string): void;
    insert(table: string, row: { [key: string]: any }): RunResult;
    find(table: string, where?: { [key: string]: any }): any;
    findAll(table: string, where?: { [key: string]: any }): any;
    update(table: string, set: { [key: string]: any }, where?: { [key: string]: any }): RunResult;
    remove(table: string, where?: { [key: string]: any }): RunResult;
    count(table: string, where?: { [key: string]: any }): any;
  }

  function open(filename?: string): Database;
  function Database(filename?: string): Database;

  export { open, Database, Statement, RunResult, ColumnType };
}
