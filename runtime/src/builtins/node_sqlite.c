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

/* ---- Simple CRUD helpers (no hand-written SQL) ---- */

#define SQLITE_CRUD_MAX_COLS 64
#define SQLITE_CRUD_SQL_CAP  8192
#define SQLITE_CRUD_IDENT_CAP 128

static int is_ident_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

/* Copy identifier into out; returns 0 on invalid. */
static int sanitize_ident(const char* in, char* out, size_t outLen) {
  if (!in || !in[0] || outLen < 2) return 0;
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 1 < outLen; i++) {
    if (!is_ident_char(in[i])) return 0;
    out[j++] = in[i];
  }
  if (j == 0) return 0;
  out[j] = '\0';
  return 1;
}

static int value_as_ident(Value v, char* out, size_t outLen) {
  char numBuf[64];
  const char* s = value_cstr(v, numBuf, sizeof(numBuf));
  return sanitize_ident(s ? s : "", out, outLen);
}

static TSHashMap* as_map(Value v) {
  if (v.tag != TAG_OBJECT || !v.as.object) return NULL;
  return (TSHashMap*)v.as.object;
}

typedef struct {
  char names[SQLITE_CRUD_MAX_COLS][SQLITE_CRUD_IDENT_CAP];
  Value values[SQLITE_CRUD_MAX_COLS];
  int count;
} CrudCols;

static void crud_collect_kv(TSString* key, Value value, void* ctx) {
  CrudCols* c = (CrudCols*)ctx;
  if (!c || c->count >= SQLITE_CRUD_MAX_COLS) return;
  if (!key || !key->data) return;
  if (!sanitize_ident(key->data, c->names[c->count], SQLITE_CRUD_IDENT_CAP)) return;
  c->values[c->count] = value;
  c->count++;
}

static int crud_from_map(Value obj, CrudCols* out) {
  out->count = 0;
  TSHashMap* map = as_map(obj);
  if (!map) return 0;
  ts_hashmap_for_each(map, crud_collect_kv, out);
  return out->count > 0;
}

static int bind_one(sqlite3_stmt* stmt, int idx, Value p) {
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

static int bind_crud_values(sqlite3_stmt* stmt, Value* vals, int n, int startIdx) {
  for (int i = 0; i < n; i++) {
    int rc = bind_one(stmt, startIdx + i, vals[i]);
    if (rc != SQLITE_OK) return rc;
  }
  return SQLITE_OK;
}

/* Append WHERE col1 = ? AND col2 = ? … ; returns bind count or -1. */
static int append_where(char* sql, size_t cap, size_t* pos, CrudCols* where) {
  if (!where || where->count == 0) return 0;
  int n = snprintf(sql + *pos, cap - *pos, " WHERE ");
  if (n < 0 || (size_t)n >= cap - *pos) return -1;
  *pos += (size_t)n;
  for (int i = 0; i < where->count; i++) {
    n = snprintf(sql + *pos, cap - *pos, "%s\"%s\" = ?%s",
                 i ? " AND " : "", where->names[i], "");
    if (n < 0 || (size_t)n >= cap - *pos) return -1;
    *pos += (size_t)n;
  }
  return where->count;
}

/* Map column type strings → SQLite type affinity */
static const char* map_col_type(const char* t) {
  if (!t || !t[0]) return "TEXT";
  if (strcmp(t, "int") == 0 || strcmp(t, "integer") == 0 ||
      strcmp(t, "number") == 0 || strcmp(t, "INTEGER") == 0 ||
      strcmp(t, "INT") == 0) return "INTEGER";
  if (strcmp(t, "real") == 0 || strcmp(t, "float") == 0 ||
      strcmp(t, "double") == 0 || strcmp(t, "REAL") == 0) return "REAL";
  if (strcmp(t, "blob") == 0 || strcmp(t, "BLOB") == 0) return "BLOB";
  if (strcmp(t, "bool") == 0 || strcmp(t, "boolean") == 0 ||
      strcmp(t, "BOOLEAN") == 0) return "INTEGER";
  if (strcmp(t, "text") == 0 || strcmp(t, "string") == 0 ||
      strcmp(t, "TEXT") == 0 || strcmp(t, "STRING") == 0) return "TEXT";
  /* Allow free-form SQLite types like "INTEGER PRIMARY KEY" if only safe chars */
  for (const char* p = t; *p; p++) {
    if (!(is_ident_char(*p) || *p == ' ' || *p == '(' || *p == ')')) return "TEXT";
  }
  return t;
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

/* columns: { name: "text", age: "int", id: "integer primary key" } */
Value node_sqlite_createTable(Value dbVal, Value table, Value columns) {
  SqliteDb* h = as_db(dbVal);
  if (!h || h->closed || !h->db) return sqlite_throw("database is not open");

  char tableName[SQLITE_CRUD_IDENT_CAP];
  if (!value_as_ident(table, tableName, sizeof(tableName)))
    return sqlite_throw("invalid table name");

  CrudCols cols;
  if (!crud_from_map(columns, &cols))
    return sqlite_throw("createTable requires a columns object");

  char sql[SQLITE_CRUD_SQL_CAP];
  size_t pos = 0;
  int n = snprintf(sql, sizeof(sql), "CREATE TABLE IF NOT EXISTS \"%s\" (", tableName);
  if (n < 0 || (size_t)n >= sizeof(sql)) return sqlite_throw("sql too long");
  pos = (size_t)n;

  for (int i = 0; i < cols.count; i++) {
    char typeBuf[256];
    const char* tsrc = "";
    if (cols.values[i].tag == TAG_STRING && cols.values[i].as.string && cols.values[i].as.string->data)
      tsrc = cols.values[i].as.string->data;
    else {
      TSString* s = ts_to_string(cols.values[i]);
      tsrc = (s && s->data) ? s->data : "TEXT";
    }
    const char* typ = map_col_type(tsrc);
    n = snprintf(sql + pos, sizeof(sql) - pos, "%s\"%s\" %s",
                 i ? ", " : "", cols.names[i], typ);
    if (n < 0 || (size_t)n >= sizeof(sql) - pos) return sqlite_throw("sql too long");
    pos += (size_t)n;
  }
  n = snprintf(sql + pos, sizeof(sql) - pos, ")");
  if (n < 0 || (size_t)n >= sizeof(sql) - pos) return sqlite_throw("sql too long");

  char* errMsg = NULL;
  int rc = sqlite3_exec(h->db, sql, NULL, NULL, &errMsg);
  if (rc != SQLITE_OK) {
    const char* msg = errMsg ? errMsg : sqlite3_errmsg(h->db);
    TSString* s = ts_string_new(msg ? msg : "createTable failed");
    if (errMsg) sqlite3_free(errMsg);
    TS_THROW(ts_value_string(s));
    return ts_value_undefined();
  }
  return ts_value_undefined();
}

Value node_sqlite_dropTable(Value dbVal, Value table) {
  SqliteDb* h = as_db(dbVal);
  if (!h || h->closed || !h->db) return sqlite_throw("database is not open");

  char tableName[SQLITE_CRUD_IDENT_CAP];
  if (!value_as_ident(table, tableName, sizeof(tableName)))
    return sqlite_throw("invalid table name");

  char sql[512];
  snprintf(sql, sizeof(sql), "DROP TABLE IF EXISTS \"%s\"", tableName);
  char* errMsg = NULL;
  int rc = sqlite3_exec(h->db, sql, NULL, NULL, &errMsg);
  if (rc != SQLITE_OK) {
    const char* msg = errMsg ? errMsg : sqlite3_errmsg(h->db);
    TSString* s = ts_string_new(msg ? msg : "dropTable failed");
    if (errMsg) sqlite3_free(errMsg);
    TS_THROW(ts_value_string(s));
    return ts_value_undefined();
  }
  return ts_value_undefined();
}

Value node_sqlite_insert(Value dbVal, Value table, Value row) {
  SqliteDb* h = as_db(dbVal);
  if (!h || h->closed || !h->db) return sqlite_throw("database is not open");

  char tableName[SQLITE_CRUD_IDENT_CAP];
  if (!value_as_ident(table, tableName, sizeof(tableName)))
    return sqlite_throw("invalid table name");

  CrudCols cols;
  if (!crud_from_map(row, &cols))
    return sqlite_throw("insert requires a row object");

  char sql[SQLITE_CRUD_SQL_CAP];
  size_t pos = 0;
  int n = snprintf(sql, sizeof(sql), "INSERT INTO \"%s\" (", tableName);
  if (n < 0 || (size_t)n >= sizeof(sql)) return sqlite_throw("sql too long");
  pos = (size_t)n;
  for (int i = 0; i < cols.count; i++) {
    n = snprintf(sql + pos, sizeof(sql) - pos, "%s\"%s\"", i ? ", " : "", cols.names[i]);
    if (n < 0 || (size_t)n >= sizeof(sql) - pos) return sqlite_throw("sql too long");
    pos += (size_t)n;
  }
  n = snprintf(sql + pos, sizeof(sql) - pos, ") VALUES (");
  if (n < 0 || (size_t)n >= sizeof(sql) - pos) return sqlite_throw("sql too long");
  pos += (size_t)n;
  for (int i = 0; i < cols.count; i++) {
    n = snprintf(sql + pos, sizeof(sql) - pos, "%s?", i ? ", " : "");
    if (n < 0 || (size_t)n >= sizeof(sql) - pos) return sqlite_throw("sql too long");
    pos += (size_t)n;
  }
  n = snprintf(sql + pos, sizeof(sql) - pos, ")");
  if (n < 0 || (size_t)n >= sizeof(sql) - pos) return sqlite_throw("sql too long");

  sqlite3_stmt* stmt = NULL;
  int rc = sqlite3_prepare_v2(h->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK || !stmt) return sqlite_throw_db(h->db, "insert prepare failed");

  rc = bind_crud_values(stmt, cols.values, cols.count, 1);
  if (rc != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return sqlite_throw_db(h->db, "insert bind failed");
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return sqlite_throw_db(h->db, "insert failed");
  }
  sqlite3_finalize(stmt);
  return make_run_result(h->db);
}

static Value crud_select(SqliteDb* h, const char* tableName, CrudCols* where, int firstOnly) {
  char sql[SQLITE_CRUD_SQL_CAP];
  size_t pos = 0;
  int n = snprintf(sql, sizeof(sql), "SELECT * FROM \"%s\"", tableName);
  if (n < 0 || (size_t)n >= sizeof(sql)) return sqlite_throw("sql too long");
  pos = (size_t)n;
  int wcount = append_where(sql, sizeof(sql), &pos, where);
  if (wcount < 0) return sqlite_throw("sql too long");
  if (firstOnly) {
    n = snprintf(sql + pos, sizeof(sql) - pos, " LIMIT 1");
    if (n < 0 || (size_t)n >= sizeof(sql) - pos) return sqlite_throw("sql too long");
  }

  sqlite3_stmt* stmt = NULL;
  int rc = sqlite3_prepare_v2(h->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK || !stmt) return sqlite_throw_db(h->db, "select prepare failed");

  if (where && where->count > 0) {
    rc = bind_crud_values(stmt, where->values, where->count, 1);
    if (rc != SQLITE_OK) {
      sqlite3_finalize(stmt);
      return sqlite_throw_db(h->db, "select bind failed");
    }
  }

  if (firstOnly) {
    Value result = ts_value_null();
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      result = row_to_object(stmt);
    } else if (rc != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      return sqlite_throw_db(h->db, "find failed");
    }
    sqlite3_finalize(stmt);
    return result;
  }

  TSArray* rows = ts_array_new();
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    ts_array_push(rows, row_to_object(stmt));
  }
  if (rc != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return sqlite_throw_db(h->db, "findAll failed");
  }
  sqlite3_finalize(stmt);
  return ts_value_array(rows);
}

Value node_sqlite_find(Value dbVal, Value table, Value where) {
  SqliteDb* h = as_db(dbVal);
  if (!h || h->closed || !h->db) return sqlite_throw("database is not open");

  char tableName[SQLITE_CRUD_IDENT_CAP];
  if (!value_as_ident(table, tableName, sizeof(tableName)))
    return sqlite_throw("invalid table name");

  CrudCols wcols;
  wcols.count = 0;
  if (where.tag != TAG_NULL && as_map(where))
    crud_from_map(where, &wcols);

  return crud_select(h, tableName, &wcols, 1);
}

Value node_sqlite_findAll(Value dbVal, Value table, Value where) {
  SqliteDb* h = as_db(dbVal);
  if (!h || h->closed || !h->db) return sqlite_throw("database is not open");

  char tableName[SQLITE_CRUD_IDENT_CAP];
  if (!value_as_ident(table, tableName, sizeof(tableName)))
    return sqlite_throw("invalid table name");

  CrudCols wcols;
  wcols.count = 0;
  if (where.tag != TAG_NULL && as_map(where))
    crud_from_map(where, &wcols);

  return crud_select(h, tableName, &wcols, 0);
}

Value node_sqlite_update(Value dbVal, Value table, Value setVals, Value where) {
  SqliteDb* h = as_db(dbVal);
  if (!h || h->closed || !h->db) return sqlite_throw("database is not open");

  char tableName[SQLITE_CRUD_IDENT_CAP];
  if (!value_as_ident(table, tableName, sizeof(tableName)))
    return sqlite_throw("invalid table name");

  CrudCols sets;
  if (!crud_from_map(setVals, &sets))
    return sqlite_throw("update requires a set object");

  CrudCols wcols;
  wcols.count = 0;
  if (where.tag != TAG_NULL && as_map(where))
    crud_from_map(where, &wcols);

  char sql[SQLITE_CRUD_SQL_CAP];
  size_t pos = 0;
  int n = snprintf(sql, sizeof(sql), "UPDATE \"%s\" SET ", tableName);
  if (n < 0 || (size_t)n >= sizeof(sql)) return sqlite_throw("sql too long");
  pos = (size_t)n;
  for (int i = 0; i < sets.count; i++) {
    n = snprintf(sql + pos, sizeof(sql) - pos, "%s\"%s\" = ?", i ? ", " : "", sets.names[i]);
    if (n < 0 || (size_t)n >= sizeof(sql) - pos) return sqlite_throw("sql too long");
    pos += (size_t)n;
  }
  int wcount = append_where(sql, sizeof(sql), &pos, &wcols);
  if (wcount < 0) return sqlite_throw("sql too long");

  sqlite3_stmt* stmt = NULL;
  int rc = sqlite3_prepare_v2(h->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK || !stmt) return sqlite_throw_db(h->db, "update prepare failed");

  rc = bind_crud_values(stmt, sets.values, sets.count, 1);
  if (rc != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return sqlite_throw_db(h->db, "update bind failed");
  }
  if (wcols.count > 0) {
    rc = bind_crud_values(stmt, wcols.values, wcols.count, sets.count + 1);
    if (rc != SQLITE_OK) {
      sqlite3_finalize(stmt);
      return sqlite_throw_db(h->db, "update where bind failed");
    }
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return sqlite_throw_db(h->db, "update failed");
  }
  sqlite3_finalize(stmt);
  return make_run_result(h->db);
}

Value node_sqlite_remove(Value dbVal, Value table, Value where) {
  SqliteDb* h = as_db(dbVal);
  if (!h || h->closed || !h->db) return sqlite_throw("database is not open");

  char tableName[SQLITE_CRUD_IDENT_CAP];
  if (!value_as_ident(table, tableName, sizeof(tableName)))
    return sqlite_throw("invalid table name");

  CrudCols wcols;
  wcols.count = 0;
  if (where.tag != TAG_NULL && as_map(where))
    crud_from_map(where, &wcols);

  char sql[SQLITE_CRUD_SQL_CAP];
  size_t pos = 0;
  int n = snprintf(sql, sizeof(sql), "DELETE FROM \"%s\"", tableName);
  if (n < 0 || (size_t)n >= sizeof(sql)) return sqlite_throw("sql too long");
  pos = (size_t)n;
  int wcount = append_where(sql, sizeof(sql), &pos, &wcols);
  if (wcount < 0) return sqlite_throw("sql too long");

  sqlite3_stmt* stmt = NULL;
  int rc = sqlite3_prepare_v2(h->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK || !stmt) return sqlite_throw_db(h->db, "remove prepare failed");

  if (wcols.count > 0) {
    rc = bind_crud_values(stmt, wcols.values, wcols.count, 1);
    if (rc != SQLITE_OK) {
      sqlite3_finalize(stmt);
      return sqlite_throw_db(h->db, "remove bind failed");
    }
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return sqlite_throw_db(h->db, "remove failed");
  }
  sqlite3_finalize(stmt);
  return make_run_result(h->db);
}

Value node_sqlite_count(Value dbVal, Value table, Value where) {
  SqliteDb* h = as_db(dbVal);
  if (!h || h->closed || !h->db) return sqlite_throw("database is not open");

  char tableName[SQLITE_CRUD_IDENT_CAP];
  if (!value_as_ident(table, tableName, sizeof(tableName)))
    return sqlite_throw("invalid table name");

  CrudCols wcols;
  wcols.count = 0;
  if (where.tag != TAG_NULL && as_map(where))
    crud_from_map(where, &wcols);

  char sql[SQLITE_CRUD_SQL_CAP];
  size_t pos = 0;
  int n = snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM \"%s\"", tableName);
  if (n < 0 || (size_t)n >= sizeof(sql)) return sqlite_throw("sql too long");
  pos = (size_t)n;
  int wcount = append_where(sql, sizeof(sql), &pos, &wcols);
  if (wcount < 0) return sqlite_throw("sql too long");

  sqlite3_stmt* stmt = NULL;
  int rc = sqlite3_prepare_v2(h->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK || !stmt) return sqlite_throw_db(h->db, "count prepare failed");

  if (wcols.count > 0) {
    rc = bind_crud_values(stmt, wcols.values, wcols.count, 1);
    if (rc != SQLITE_OK) {
      sqlite3_finalize(stmt);
      return sqlite_throw_db(h->db, "count bind failed");
    }
  }

  double cnt = 0;
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    cnt = (double)sqlite3_column_int64(stmt, 0);
  } else if (rc != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return sqlite_throw_db(h->db, "count failed");
  }
  sqlite3_finalize(stmt);
  return ts_value_number(cnt);
}
