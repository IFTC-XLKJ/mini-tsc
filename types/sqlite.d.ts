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

  /**
   * Field operators (Mongo-style). Plain values mean `$eq`.
   *
   * ```ts
   * { age: 30 }                         // age = 30
   * { age: { $gt: 18, $lte: 60 } }       // age > 18 AND age <= 60
   * { name: { $like: "A%" } }            // name LIKE 'A%'
   * { name: { $in: ["Alice", "Bob"] } }  // name IN (...)
   * { name: { $ne: "Bob" } }             // name != 'Bob'
   * { email: { $null: true } }           // email IS NULL
   * { email: { $null: false } }          // email IS NOT NULL
   * ```
   */
  interface FieldOps {
    $eq?: any;
    $ne?: any;
    $gt?: any;
    $gte?: any;
    $lt?: any;
    $lte?: any;
    $like?: string;
    $in?: any[];
    $nin?: any[];
    $null?: boolean;
    /** aliases without $ */
    eq?: any;
    ne?: any;
    gt?: any;
    gte?: any;
    lt?: any;
    lte?: any;
    like?: string;
    in?: any[];
    nin?: any[];
    null?: boolean;
  }

  /** Where clause: column → value or FieldOps. Multiple columns are AND-ed. */
  type Where = { [column: string]: any | FieldOps };

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
    find(table: string, where?: Where): any;
    findAll(table: string, where?: Where): any;
    update(table: string, set: { [key: string]: any }, where?: Where): RunResult;
    remove(table: string, where?: Where): RunResult;
    count(table: string, where?: Where): any;
  }

  function open(filename?: string): Database;
  function Database(filename?: string): Database;

  export { open, Database, Statement, RunResult, ColumnType, FieldOps, Where };
}
