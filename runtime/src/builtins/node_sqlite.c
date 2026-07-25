#define _CRT_SECURE_NO_WARNINGS
#include "node_sqlite.h"
#include "sqlite3.h"
#include <stdio.h>
#include <string.h>

/* Opaque handles stored as TAG_OBJECT */
typedef struct {
  sqlite3* db;
  int closed;
} SqliteDb;

typedef struct {
  sqlite3_stmt* stmt;
  SqliteDb* owner;
  int finalized;
} SqliteStmt;

static Value sqlite_throw(const char* msg) {
  TS_THROW(ts_value_string(ts_string_new(msg ? msg : "sqlite error")));
  return ts_value_undefined();
}

static Value sqlite_throw_db(sqlite3* db, const char* fallback) {
  const char* msg = db ? sqlite3_errmsg(db) : fallback;
  return sqlite_throw(msg ? msg : fallback);
}

static SqliteDb* as_db(Value v) {
  if (v.tag != TAG_OBJECT || !v.as.object) return NULL;
  return (SqliteDb*)v.as.object;
}

static SqliteStmt* as_stmt(Value v) {
  if (v.tag != TAG_OBJECT || !v.as.object) return NULL;
  return (SqliteStmt*)v.as.object;
}

static const char* value_cstr(Value v, char* numBuf, size_t numBufLen) {
  if (v.tag == TAG_STRING && v.as.string && v.as.string->data) return v.as.string->data;
  if (v.tag == TAG_NUMBER) {
    snprintf(numBuf, numBufLen, "%.17g", v.as.number);
    return numBuf;
  }
  if (v.tag == TAG_BOOLEAN) return v.as.boolean ? "1" : "0";
  if (v.tag == TAG_NULL) return NULL;
  TSString* s = ts_to_string(v);
  return (s && s->data) ? s->data : "";
}

/* Bind params: array of values, or single value, or null/undefined = no binds */
static int bind_params(sqlite3_stmt* stmt, Value params) {
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);

  if (params.tag == TAG_NULL) return SQLITE_OK;

  if (params.tag == TAG_ARRAY && params.as.array) {
    TSArray* arr = params.as.array;
    for (int i = 0; i < arr->length; i++) {
      Value p = arr->items[i];
      int idx = i + 1;
      if (p.tag == TAG_NULL) {
        int rc = sqlite3_bind_null(stmt, idx);
        if (rc != SQLITE_OK) return rc;
      } else if (p.tag == TAG_NUMBER) {
        double n = p.as.number;
        if (n == (double)(sqlite3_int64)n && n >= -9007199254740992.0 && n <= 9007199254740992.0) {
          int rc = sqlite3_bind_int64(stmt, idx, (sqlite3_int64)n);
          if (rc != SQLITE_OK) return rc;
        } else {
          int rc = sqlite3_bind_double(stmt, idx, n);
          if (rc != SQLITE_OK) return rc;
        }
      } else if (p.tag == TAG_BOOLEAN) {
        int rc = sqlite3_bind_int(stmt, idx, p.as.boolean ? 1 : 0);
        if (rc != SQLITE_OK) return rc;
      } else {
        TSString* s = ts_to_string(p);
        const char* c = (s && s->data) ? s->data : "";
        int rc = sqlite3_bind_text(stmt, idx, c, -1, SQLITE_TRANSIENT);
        if (rc != SQLITE_OK) return rc;
      }
    }
    return SQLITE_OK;
  }

  /* Single scalar bind as ?1 */
  {
    Value p = params;
    int idx = 1;
    if (p.tag == TAG_NULL) return sqlite3_bind_null(stmt, idx);
    if (p.tag == TAG_NUMBER) {
      double n = p.as.number;
      if (n == (double)(sqlite3_int64)n && n >= -9007199254740992.0 && n <= 9007199254740992.0)
        return sqlite3_bind_int64(stmt, idx, (sqlite3_int64)n);
      return sqlite3_bind_double(stmt, idx, n);
    }
    if (p.tag == TAG_BOOLEAN) return sqlite3_bind_int(stmt, idx, p.as.boolean ? 1 : 0);
    {
      TSString* s = ts_to_string(p);
      const char* c = (s && s->data) ? s->data : "";
      return sqlite3_bind_text(stmt, idx, c, -1, SQLITE_TRANSIENT);
    }
  }
}

static Value column_to_value(sqlite3_stmt* stmt, int col) {
  int type = sqlite3_column_type(stmt, col);
  switch (type) {
    case SQLITE_INTEGER: {
      sqlite3_int64 v = sqlite3_column_int64(stmt, col);
      return ts_value_number((double)v);
    }
    case SQLITE_FLOAT:
      return ts_value_number(sqlite3_column_double(stmt, col));
    case SQLITE_TEXT: {
      const unsigned char* t = sqlite3_column_text(stmt, col);
      return ts_value_string(ts_string_new(t ? (const char*)t : ""));
    }
    case SQLITE_BLOB: {
      const void* blob = sqlite3_column_blob(stmt, col);
      int n = sqlite3_column_bytes(stmt, col);
      if (!blob || n <= 0) return ts_value_string(ts_string_new(""));
      return ts_value_string(ts_string_new_len((const char*)blob, n));
    }
    case SQLITE_NULL:
    default:
      return ts_value_null();
  }
}

static Value row_to_object(sqlite3_stmt* stmt) {
  TSHashMap* map = ts_hashmap_new();
  int ncols = sqlite3_column_count(stmt);
  for (int i = 0; i < ncols; i++) {
    const char* name = sqlite3_column_name(stmt, i);
    if (!name) name = "";
    ts_hashmap_set(map, ts_string_new(name), column_to_value(stmt, i));
  }
  return ts_value_object(map);
}

static Value make_run_result(sqlite3* db) {
  TSHashMap* map = ts_hashmap_new();
  int changes = db ? sqlite3_changes(db) : 0;
  sqlite3_int64 rowid = db ? sqlite3_last_insert_rowid(db) : 0;
  ts_hashmap_set(map, ts_string_new("changes"), ts_value_number((double)changes));
  ts_hashmap_set(map, ts_string_new("lastInsertRowid"), ts_value_number((double)rowid));
  return ts_value_object(map);
}

/* Pack rest-args from codegen: either a single Value, or we receive params as array.
   When TS calls stmt.run(a, b, c), emitter may pass one array or multiple —
   our C API takes a single Value params (array preferred). */

Value node_sqlite_open(Value filename) {
  return node_sqlite_Database(filename);
}

Value node_sqlite_Database(Value filename) {
  char numBuf[64];
  const char* path = ":memory:";
  if (filename.tag != TAG_NULL) {
    const char* p = value_cstr(filename, numBuf, sizeof(numBuf));
    if (p && p[0]) path = p;
  }

  sqlite3* db = NULL;
  int rc = sqlite3_open(path, &db);
  if (rc != SQLITE_OK) {
    const char* msg = db ? sqlite3_errmsg(db) : "sqlite3_open failed";
    if (db) sqlite3_close(db);
    return sqlite_throw(msg);
  }

  SqliteDb* handle = (SqliteDb*)malloc(sizeof(SqliteDb));
  if (!handle) {
    sqlite3_close(db);
    return sqlite_throw("out of memory");
  }
  handle->db = db;
  handle->closed = 0;
  return ts_value_object(handle);
}

Value node_sqlite_exec(Value dbVal, Value sql) {
  SqliteDb* h = as_db(dbVal);
  if (!h || h->closed || !h->db) return sqlite_throw("database is not open");

  char numBuf[64];
  const char* sqlStr = value_cstr(sql, numBuf, sizeof(numBuf));
  if (!sqlStr) sqlStr = "";

  char* errMsg = NULL;
  int rc = sqlite3_exec(h->db, sqlStr, NULL, NULL, &errMsg);
  if (rc != SQLITE_OK) {
    const char* msg = errMsg ? errMsg : sqlite3_errmsg(h->db);
    TSString* s = ts_string_new(msg ? msg : "sqlite exec failed");
    if (errMsg) sqlite3_free(errMsg);
    TS_THROW(ts_value_string(s));
    return ts_value_undefined();
  }
  return ts_value_undefined();
}

Value node_sqlite_prepare(Value dbVal, Value sql) {
  SqliteDb* h = as_db(dbVal);
  if (!h || h->closed || !h->db) return sqlite_throw("database is not open");

  char numBuf[64];
  const char* sqlStr = value_cstr(sql, numBuf, sizeof(numBuf));
  if (!sqlStr) sqlStr = "";

  sqlite3_stmt* stmt = NULL;
  int rc = sqlite3_prepare_v2(h->db, sqlStr, -1, &stmt, NULL);
  if (rc != SQLITE_OK || !stmt) {
    return sqlite_throw_db(h->db, "sqlite prepare failed");
  }

  SqliteStmt* sh = (SqliteStmt*)malloc(sizeof(SqliteStmt));
  if (!sh) {
    sqlite3_finalize(stmt);
    return sqlite_throw("out of memory");
  }
  sh->stmt = stmt;
  sh->owner = h;
  sh->finalized = 0;
  return ts_value_object(sh);
}

Value node_sqlite_close(Value dbVal) {
  SqliteDb* h = as_db(dbVal);
  if (!h) return ts_value_undefined();
  if (!h->closed && h->db) {
    sqlite3_close(h->db);
    h->db = NULL;
    h->closed = 1;
  }
  return ts_value_undefined();
}

Value node_sqlite_pragma(Value dbVal, Value source) {
  SqliteDb* h = as_db(dbVal);
  if (!h || h->closed || !h->db) return sqlite_throw("database is not open");

  char numBuf[64];
  const char* src = value_cstr(source, numBuf, sizeof(numBuf));
  if (!src) src = "";

  char sql[1024];
  /* If source already starts with PRAGMA, use as-is; else wrap */
  if (strncmp(src, "PRAGMA", 6) == 0 || strncmp(src, "pragma", 6) == 0) {
    snprintf(sql, sizeof(sql), "%s", src);
  } else {
    snprintf(sql, sizeof(sql), "PRAGMA %s", src);
  }

  sqlite3_stmt* stmt = NULL;
  int rc = sqlite3_prepare_v2(h->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK || !stmt) return sqlite_throw_db(h->db, "pragma failed");

  rc = sqlite3_step(stmt);
  Value result = ts_value_undefined();
  if (rc == SQLITE_ROW) {
    /* Single-column pragmas return the value; multi-column return object */
    int ncols = sqlite3_column_count(stmt);
    if (ncols == 1) {
      result = column_to_value(stmt, 0);
    } else {
      result = row_to_object(stmt);
    }
  } else if (rc != SQLITE_DONE) {
    const char* msg = sqlite3_errmsg(h->db);
    sqlite3_finalize(stmt);
    return sqlite_throw(msg);
  }
  sqlite3_finalize(stmt);
  return result;
}

Value node_sqlite_run(Value stmtVal, Value params) {
  SqliteStmt* sh = as_stmt(stmtVal);
  if (!sh || sh->finalized || !sh->stmt) return sqlite_throw("statement is not valid");
  if (!sh->owner || sh->owner->closed || !sh->owner->db) return sqlite_throw("database is not open");

  int rc = bind_params(sh->stmt, params);
  if (rc != SQLITE_OK) return sqlite_throw_db(sh->owner->db, "bind failed");

  rc = sqlite3_step(sh->stmt);
  if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
    return sqlite_throw_db(sh->owner->db, "run failed");
  }
  /* Consume remaining rows if any */
  while (rc == SQLITE_ROW) {
    rc = sqlite3_step(sh->stmt);
  }
  sqlite3_reset(sh->stmt);
  return make_run_result(sh->owner->db);
}

Value node_sqlite_get(Value stmtVal, Value params) {
  SqliteStmt* sh = as_stmt(stmtVal);
  if (!sh || sh->finalized || !sh->stmt) return sqlite_throw("statement is not valid");
  if (!sh->owner || sh->owner->closed || !sh->owner->db) return sqlite_throw("database is not open");

  int rc = bind_params(sh->stmt, params);
  if (rc != SQLITE_OK) return sqlite_throw_db(sh->owner->db, "bind failed");

  rc = sqlite3_step(sh->stmt);
  Value result = ts_value_undefined();
  if (rc == SQLITE_ROW) {
    result = row_to_object(sh->stmt);
  } else if (rc != SQLITE_DONE) {
    sqlite3_reset(sh->stmt);
    return sqlite_throw_db(sh->owner->db, "get failed");
  }
  sqlite3_reset(sh->stmt);
  return result;
}

Value node_sqlite_all(Value stmtVal, Value params) {
  SqliteStmt* sh = as_stmt(stmtVal);
  if (!sh || sh->finalized || !sh->stmt) return sqlite_throw("statement is not valid");
  if (!sh->owner || sh->owner->closed || !sh->owner->db) return sqlite_throw("database is not open");

  int rc = bind_params(sh->stmt, params);
  if (rc != SQLITE_OK) return sqlite_throw_db(sh->owner->db, "bind failed");

  TSArray* rows = ts_array_new();
  while ((rc = sqlite3_step(sh->stmt)) == SQLITE_ROW) {
    ts_array_push(rows, row_to_object(sh->stmt));
  }
  if (rc != SQLITE_DONE) {
    sqlite3_reset(sh->stmt);
    return sqlite_throw_db(sh->owner->db, "all failed");
  }
  sqlite3_reset(sh->stmt);
  return ts_value_array(rows);
}

Value node_sqlite_iterate(Value stmtVal, Value params) {
  /* Same as all for now — returns array (no real iterator runtime) */
  return node_sqlite_all(stmtVal, params);
}

Value node_sqlite_finalize(Value stmtVal) {
  SqliteStmt* sh = as_stmt(stmtVal);
  if (!sh || sh->finalized) return ts_value_undefined();
  if (sh->stmt) {
    sqlite3_finalize(sh->stmt);
    sh->stmt = NULL;
  }
  sh->finalized = 1;
  return ts_value_undefined();
}
