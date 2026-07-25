#ifndef NODE_SQLITE_H
#define NODE_SQLITE_H

#include "runtime.h"

/* Module-level */
Value node_sqlite_open(Value filename);
Value node_sqlite_Database(Value filename);

/* Database methods */
Value node_sqlite_exec(Value dbVal, Value sql);
Value node_sqlite_prepare(Value dbVal, Value sql);
Value node_sqlite_close(Value dbVal);
Value node_sqlite_pragma(Value dbVal, Value source);

/* Statement methods */
Value node_sqlite_run(Value stmtVal, Value params);
Value node_sqlite_get(Value stmtVal, Value params);
Value node_sqlite_all(Value stmtVal, Value params);
Value node_sqlite_iterate(Value stmtVal, Value params);
Value node_sqlite_finalize(Value stmtVal);

#endif /* NODE_SQLITE_H */
