#ifndef NODE_SQLITE_H
#define NODE_SQLITE_H

#include "runtime.h"

/* Module-level */
Value node_sqlite_open(Value filename);
Value node_sqlite_Database(Value filename);

/* Database methods (SQL) */
Value node_sqlite_exec(Value dbVal, Value sql);
Value node_sqlite_prepare(Value dbVal, Value sql);
Value node_sqlite_close(Value dbVal);
Value node_sqlite_pragma(Value dbVal, Value source);

/* Database methods (simple CRUD — no SQL required) */
Value node_sqlite_createTable(Value dbVal, Value table, Value columns);
Value node_sqlite_dropTable(Value dbVal, Value table);
Value node_sqlite_insert(Value dbVal, Value table, Value row);
Value node_sqlite_find(Value dbVal, Value table, Value where, Value options);
Value node_sqlite_findAll(Value dbVal, Value table, Value where, Value options);
/* findAndCount: page rows + total matching where (ignores limit/offset for total) */
Value node_sqlite_findAndCount(Value dbVal, Value table, Value where, Value options);
Value node_sqlite_update(Value dbVal, Value table, Value setVals, Value where);
Value node_sqlite_remove(Value dbVal, Value table, Value where);
Value node_sqlite_count(Value dbVal, Value table, Value where);

/* Statement methods */
Value node_sqlite_run(Value stmtVal, Value params);
Value node_sqlite_get(Value stmtVal, Value params);
Value node_sqlite_all(Value stmtVal, Value params);
Value node_sqlite_iterate(Value stmtVal, Value params);
Value node_sqlite_finalize(Value stmtVal);

#endif /* NODE_SQLITE_H */
