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

/* WHERE predicates: col OP value(s). Multiple ops per column allowed. */
typedef enum {
  CRUD_OP_EQ = 0,
  CRUD_OP_NE,
  CRUD_OP_GT,
  CRUD_OP_GTE,
  CRUD_OP_LT,
  CRUD_OP_LTE,
  CRUD_OP_LIKE,
  CRUD_OP_IN,
  CRUD_OP_NIN,
  CRUD_OP_IS_NULL,
  CRUD_OP_IS_NOT_NULL
} CrudOp;

#define SQLITE_CRUD_MAX_PREDS 128
#define SQLITE_CRUD_MAX_BINDS 256

typedef struct {
  char col[SQLITE_CRUD_IDENT_CAP];
  CrudOp op;
  Value value; /* scalar, or TAG_ARRAY for $in/$nin */
} CrudPred;

typedef struct {
  CrudPred preds[SQLITE_CRUD_MAX_PREDS];
  int count;
  /* Flattened bind values in SQL order (IS NULL has none) */
  Value binds[SQLITE_CRUD_MAX_BINDS];
  int bind_count;
} CrudWhere;

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

/* Returns CrudOp, or -1 if unknown. */
static int parse_op_name(const char* op) {
  if (!op) return CRUD_OP_EQ;
  if (strcmp(op, "$eq") == 0 || strcmp(op, "eq") == 0) return CRUD_OP_EQ;
  if (strcmp(op, "$ne") == 0 || strcmp(op, "ne") == 0 || strcmp(op, "$neq") == 0) return CRUD_OP_NE;
  if (strcmp(op, "$gt") == 0 || strcmp(op, "gt") == 0) return CRUD_OP_GT;
  if (strcmp(op, "$gte") == 0 || strcmp(op, "gte") == 0 || strcmp(op, "$ge") == 0) return CRUD_OP_GTE;
  if (strcmp(op, "$lt") == 0 || strcmp(op, "lt") == 0) return CRUD_OP_LT;
  if (strcmp(op, "$lte") == 0 || strcmp(op, "lte") == 0 || strcmp(op, "$le") == 0) return CRUD_OP_LTE;
  if (strcmp(op, "$like") == 0 || strcmp(op, "like") == 0) return CRUD_OP_LIKE;
  if (strcmp(op, "$in") == 0 || strcmp(op, "in") == 0) return CRUD_OP_IN;
  if (strcmp(op, "$nin") == 0 || strcmp(op, "nin") == 0 || strcmp(op, "$notin") == 0) return CRUD_OP_NIN;
  if (strcmp(op, "$null") == 0 || strcmp(op, "null") == 0 ||
      strcmp(op, "$isNull") == 0 || strcmp(op, "isNull") == 0) return CRUD_OP_IS_NULL;
  return -1;
}

static int where_add_pred(CrudWhere* w, const char* col, CrudOp op, Value val) {
  if (!w || w->count >= SQLITE_CRUD_MAX_PREDS) return 0;
  if (!sanitize_ident(col, w->preds[w->count].col, SQLITE_CRUD_IDENT_CAP)) return 0;
  /* $null: true → IS NULL, false → IS NOT NULL */
  if (op == CRUD_OP_IS_NULL) {
    int isNull = 1;
    if (val.tag == TAG_BOOLEAN) isNull = val.as.boolean ? 1 : 0;
    else if (val.tag == TAG_NUMBER) isNull = (val.as.number != 0.0) ? 1 : 0;
    else if (val.tag == TAG_NULL) isNull = 1;
    w->preds[w->count].op = isNull ? CRUD_OP_IS_NULL : CRUD_OP_IS_NOT_NULL;
    w->preds[w->count].value = ts_value_null();
    w->count++;
    return 1;
  }
  if (op == CRUD_OP_IN || op == CRUD_OP_NIN) {
    if (val.tag != TAG_ARRAY || !val.as.array) return 0;
  }
  w->preds[w->count].op = op;
  w->preds[w->count].value = val;
  w->count++;
  return 1;
}

typedef struct {
  CrudWhere* where;
  char col[SQLITE_CRUD_IDENT_CAP];
  int ok;
} OpCollectCtx;

static void collect_op_kv(TSString* key, Value value, void* ctx) {
  OpCollectCtx* c = (OpCollectCtx*)ctx;
  if (!c || !c->ok || !key || !key->data) return;
  int op = parse_op_name(key->data);
  if (op < 0) {
    c->ok = 0;
    return;
  }
  if (!where_add_pred(c->where, c->col, (CrudOp)op, value))
    c->ok = 0;
}

typedef struct {
  CrudWhere* where;
  int ok;
} WhereCollectCtx;

static void collect_where_kv(TSString* key, Value value, void* ctx) {
  WhereCollectCtx* c = (WhereCollectCtx*)ctx;
  if (!c || !c->ok || !key || !key->data) return;
  char col[SQLITE_CRUD_IDENT_CAP];
  if (!sanitize_ident(key->data, col, sizeof(col))) {
    c->ok = 0;
    return;
  }
  /* Nested object → operator map: { age: { $gt: 25, $lt: 40 } } */
  TSHashMap* opMap = as_map(value);
  if (opMap) {
    OpCollectCtx oc;
    oc.where = c->where;
    snprintf(oc.col, sizeof(oc.col), "%s", col);
    oc.ok = 1;
    ts_hashmap_for_each(opMap, collect_op_kv, &oc);
    if (!oc.ok) c->ok = 0;
    return;
  }
  /* Plain value → equality */
  if (!where_add_pred(c->where, col, CRUD_OP_EQ, value))
    c->ok = 0;
}

/* Parse where object into predicates. Empty/null where → count 0, ok. */
static int crud_where_from_value(Value where, CrudWhere* out) {
  out->count = 0;
  out->bind_count = 0;
  if (where.tag == TAG_NULL) return 1;
  TSHashMap* map = as_map(where);
  if (!map) return 1; /* treat non-object as no filter */
  WhereCollectCtx c;
  c.where = out;
  c.ok = 1;
  ts_hashmap_for_each(map, collect_where_kv, &c);
  return c.ok;
}

static const char* op_sql(CrudOp op) {
  switch (op) {
    case CRUD_OP_EQ: return "=";
    case CRUD_OP_NE: return "!=";
    case CRUD_OP_GT: return ">";
    case CRUD_OP_GTE: return ">=";
    case CRUD_OP_LT: return "<";
    case CRUD_OP_LTE: return "<=";
    case CRUD_OP_LIKE: return "LIKE";
    default: return "=";
  }
}

/* Append WHERE … ; fill binds. Returns bind count, or -1 on error. */
static int append_where(char* sql, size_t cap, size_t* pos, CrudWhere* where) {
  if (!where || where->count == 0) {
    if (where) where->bind_count = 0;
    return 0;
  }
  where->bind_count = 0;
  int n = snprintf(sql + *pos, cap - *pos, " WHERE ");
  if (n < 0 || (size_t)n >= cap - *pos) return -1;
  *pos += (size_t)n;

  for (int i = 0; i < where->count; i++) {
    CrudPred* p = &where->preds[i];
    if (i > 0) {
      n = snprintf(sql + *pos, cap - *pos, " AND ");
      if (n < 0 || (size_t)n >= cap - *pos) return -1;
      *pos += (size_t)n;
    }

    if (p->op == CRUD_OP_IS_NULL) {
      n = snprintf(sql + *pos, cap - *pos, "\"%s\" IS NULL", p->col);
      if (n < 0 || (size_t)n >= cap - *pos) return -1;
      *pos += (size_t)n;
      continue;
    }
    if (p->op == CRUD_OP_IS_NOT_NULL) {
      n = snprintf(sql + *pos, cap - *pos, "\"%s\" IS NOT NULL", p->col);
      if (n < 0 || (size_t)n >= cap - *pos) return -1;
      *pos += (size_t)n;
      continue;
    }
    if (p->op == CRUD_OP_IN || p->op == CRUD_OP_NIN) {
      TSArray* arr = (p->value.tag == TAG_ARRAY) ? p->value.as.array : NULL;
      int len = arr ? arr->length : 0;
      if (len <= 0) {
        /* empty IN → always false; empty NOT IN → always true */
        n = snprintf(sql + *pos, cap - *pos, "%s",
                     p->op == CRUD_OP_IN ? "0" : "1");
        if (n < 0 || (size_t)n >= cap - *pos) return -1;
        *pos += (size_t)n;
        continue;
      }
      n = snprintf(sql + *pos, cap - *pos, "\"%s\" %s (",
                   p->col, p->op == CRUD_OP_IN ? "IN" : "NOT IN");
      if (n < 0 || (size_t)n >= cap - *pos) return -1;
      *pos += (size_t)n;
      for (int j = 0; j < len; j++) {
        n = snprintf(sql + *pos, cap - *pos, "%s?", j ? ", " : "");
        if (n < 0 || (size_t)n >= cap - *pos) return -1;
        *pos += (size_t)n;
        if (where->bind_count >= SQLITE_CRUD_MAX_BINDS) return -1;
        where->binds[where->bind_count++] = arr->items[j];
      }
      n = snprintf(sql + *pos, cap - *pos, ")");
      if (n < 0 || (size_t)n >= cap - *pos) return -1;
      *pos += (size_t)n;
      continue;
    }

    /* Scalar comparison */
    n = snprintf(sql + *pos, cap - *pos, "\"%s\" %s ?", p->col, op_sql(p->op));
    if (n < 0 || (size_t)n >= cap - *pos) return -1;
    *pos += (size_t)n;
    if (where->bind_count >= SQLITE_CRUD_MAX_BINDS) return -1;
    where->binds[where->bind_count++] = p->value;
  }
  return where->bind_count;
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

/* Query options: orderBy / limit / offset */
#define SQLITE_CRUD_MAX_ORDER 16

typedef struct {
  char col[SQLITE_CRUD_IDENT_CAP];
  int desc; /* 0=ASC, 1=DESC */
} CrudOrder;

typedef struct {
  CrudOrder order[SQLITE_CRUD_MAX_ORDER];
  int order_count;
  int has_limit;
  int limit;
  int has_offset;
  int offset;
} CrudOptions;

static int order_dir_from_value(Value v, int* out_desc) {
  if (v.tag == TAG_NUMBER) {
    *out_desc = (v.as.number < 0) ? 1 : 0;
    return 1;
  }
  if (v.tag == TAG_BOOLEAN) {
    *out_desc = v.as.boolean ? 1 : 0; /* true → DESC */
    return 1;
  }
  char numBuf[64];
  const char* s = value_cstr(v, numBuf, sizeof(numBuf));
  if (!s) return 0;
  if (strcmp(s, "desc") == 0 || strcmp(s, "DESC") == 0 ||
      strcmp(s, "descending") == 0 || strcmp(s, "-1") == 0) {
    *out_desc = 1;
    return 1;
  }
  if (strcmp(s, "asc") == 0 || strcmp(s, "ASC") == 0 ||
      strcmp(s, "ascending") == 0 || strcmp(s, "1") == 0 || s[0] == '\0') {
    *out_desc = 0;
    return 1;
  }
  return 0;
}

static int options_add_order(CrudOptions* opt, const char* colSpec, int desc) {
  if (!opt || opt->order_count >= SQLITE_CRUD_MAX_ORDER) return 0;
  char col[SQLITE_CRUD_IDENT_CAP];
  const char* src = colSpec ? colSpec : "";
  int d = desc;
  if (src[0] == '-') {
    d = 1;
    src++;
  } else if (src[0] == '+') {
    d = 0;
    src++;
  }
  if (!sanitize_ident(src, col, sizeof(col))) return 0;
  snprintf(opt->order[opt->order_count].col, SQLITE_CRUD_IDENT_CAP, "%s", col);
  opt->order[opt->order_count].desc = d;
  opt->order_count++;
  return 1;
}

typedef struct {
  CrudOptions* opt;
  int ok;
} OrderMapCtx;

static void collect_order_map_kv(TSString* key, Value value, void* ctx) {
  OrderMapCtx* c = (OrderMapCtx*)ctx;
  if (!c || !c->ok || !key || !key->data) return;
  int desc = 0;
  if (!order_dir_from_value(value, &desc)) {
    c->ok = 0;
    return;
  }
  if (!options_add_order(c->opt, key->data, desc))
    c->ok = 0;
}

static int parse_order_by(Value orderBy, CrudOptions* opt) {
  if (orderBy.tag == TAG_NULL) return 1;

  /* string: "age" | "-age" | "+name" */
  if (orderBy.tag == TAG_STRING) {
    char numBuf[64];
    const char* s = value_cstr(orderBy, numBuf, sizeof(numBuf));
    return options_add_order(opt, s ? s : "", 0);
  }

  /* array: ["age", "-name"] or [{ age: "desc" }, ...] */
  if (orderBy.tag == TAG_ARRAY && orderBy.as.array) {
    TSArray* arr = orderBy.as.array;
    for (int i = 0; i < arr->length; i++) {
      Value item = arr->items[i];
      if (item.tag == TAG_STRING) {
        char numBuf[64];
        const char* s = value_cstr(item, numBuf, sizeof(numBuf));
        if (!options_add_order(opt, s ? s : "", 0)) return 0;
      } else if (as_map(item)) {
        OrderMapCtx c;
        c.opt = opt;
        c.ok = 1;
        ts_hashmap_for_each(as_map(item), collect_order_map_kv, &c);
        if (!c.ok) return 0;
      } else {
        return 0;
      }
    }
    return 1;
  }

  /* object: { age: "desc", name: "asc" } */
  if (as_map(orderBy)) {
    OrderMapCtx c;
    c.opt = opt;
    c.ok = 1;
    ts_hashmap_for_each(as_map(orderBy), collect_order_map_kv, &c);
    return c.ok;
  }
  return 0;
}

static int crud_options_from_value(Value options, CrudOptions* out) {
  out->order_count = 0;
  out->has_limit = 0;
  out->limit = 0;
  out->has_offset = 0;
  out->offset = 0;
  if (options.tag == TAG_NULL) return 1;
  TSHashMap* map = as_map(options);
  if (!map) return 1; /* ignore non-object */

  Value orderBy = ts_hashmap_get(map, ts_string_new("orderBy"));
  if (orderBy.tag == TAG_NULL)
    orderBy = ts_hashmap_get(map, ts_string_new("order"));
  if (orderBy.tag == TAG_NULL)
    orderBy = ts_hashmap_get(map, ts_string_new("sort"));
  if (orderBy.tag != TAG_NULL) {
    if (!parse_order_by(orderBy, out)) return 0;
  }

  Value lim = ts_hashmap_get(map, ts_string_new("limit"));
  if (lim.tag == TAG_NULL) lim = ts_hashmap_get(map, ts_string_new("$limit"));
  if (lim.tag == TAG_NUMBER) {
    out->has_limit = 1;
    out->limit = (int)lim.as.number;
    if (out->limit < 0) out->limit = 0;
  }

  Value off = ts_hashmap_get(map, ts_string_new("offset"));
  if (off.tag == TAG_NULL) off = ts_hashmap_get(map, ts_string_new("skip"));
  if (off.tag == TAG_NULL) off = ts_hashmap_get(map, ts_string_new("$offset"));
  if (off.tag == TAG_NUMBER) {
    out->has_offset = 1;
    out->offset = (int)off.as.number;
    if (out->offset < 0) out->offset = 0;
  }

  /* page (1-based) + pageSize → limit/offset */
  Value page = ts_hashmap_get(map, ts_string_new("page"));
  Value pageSize = ts_hashmap_get(map, ts_string_new("pageSize"));
  if (pageSize.tag == TAG_NULL) pageSize = ts_hashmap_get(map, ts_string_new("page_size"));
  if (page.tag == TAG_NUMBER && pageSize.tag == TAG_NUMBER) {
    int ps = (int)pageSize.as.number;
    int pg = (int)page.as.number;
    if (ps < 0) ps = 0;
    if (pg < 1) pg = 1;
    out->has_limit = 1;
    out->limit = ps;
    out->has_offset = 1;
    out->offset = (pg - 1) * ps;
  }

  return 1;
}

/* Append ORDER BY / LIMIT / OFFSET. Returns 0 on overflow. */
static int append_order_limit(char* sql, size_t cap, size_t* pos,
                              CrudOptions* opt, int firstOnly) {
  int n;
  if (opt && opt->order_count > 0) {
    n = snprintf(sql + *pos, cap - *pos, " ORDER BY ");
    if (n < 0 || (size_t)n >= cap - *pos) return 0;
    *pos += (size_t)n;
    for (int i = 0; i < opt->order_count; i++) {
      n = snprintf(sql + *pos, cap - *pos, "%s\"%s\" %s",
                   i ? ", " : "",
                   opt->order[i].col,
                   opt->order[i].desc ? "DESC" : "ASC");
      if (n < 0 || (size_t)n >= cap - *pos) return 0;
      *pos += (size_t)n;
    }
  }

  if (firstOnly) {
    /* find(): LIMIT 1 unless caller set a larger limit (still cap at 1) */
    n = snprintf(sql + *pos, cap - *pos, " LIMIT 1");
    if (n < 0 || (size_t)n >= cap - *pos) return 0;
    *pos += (size_t)n;
    if (opt && opt->has_offset && opt->offset > 0) {
      n = snprintf(sql + *pos, cap - *pos, " OFFSET %d", opt->offset);
      if (n < 0 || (size_t)n >= cap - *pos) return 0;
      *pos += (size_t)n;
    }
    return 1;
  }

  if (opt && opt->has_limit) {
    n = snprintf(sql + *pos, cap - *pos, " LIMIT %d", opt->limit);
    if (n < 0 || (size_t)n >= cap - *pos) return 0;
    *pos += (size_t)n;
  }
  if (opt && opt->has_offset && opt->offset > 0) {
    /* SQLite allows OFFSET without LIMIT via LIMIT -1 OFFSET n */
    if (!(opt->has_limit)) {
      n = snprintf(sql + *pos, cap - *pos, " LIMIT -1");
      if (n < 0 || (size_t)n >= cap - *pos) return 0;
      *pos += (size_t)n;
    }
    n = snprintf(sql + *pos, cap - *pos, " OFFSET %d", opt->offset);
    if (n < 0 || (size_t)n >= cap - *pos) return 0;
    *pos += (size_t)n;
  }
  return 1;
}

static Value crud_select(SqliteDb* h, const char* tableName, CrudWhere* where,
                         CrudOptions* opt, int firstOnly) {
  char sql[SQLITE_CRUD_SQL_CAP];
  size_t pos = 0;
  int n = snprintf(sql, sizeof(sql), "SELECT * FROM \"%s\"", tableName);
  if (n < 0 || (size_t)n >= sizeof(sql)) return sqlite_throw("sql too long");
  pos = (size_t)n;
  int wcount = append_where(sql, sizeof(sql), &pos, where);
  if (wcount < 0) return sqlite_throw("sql too long");
  if (!append_order_limit(sql, sizeof(sql), &pos, opt, firstOnly))
    return sqlite_throw("sql too long");

  sqlite3_stmt* stmt = NULL;
  int rc = sqlite3_prepare_v2(h->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK || !stmt) return sqlite_throw_db(h->db, "select prepare failed");

  if (where && where->bind_count > 0) {
    rc = bind_crud_values(stmt, where->binds, where->bind_count, 1);
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

Value node_sqlite_find(Value dbVal, Value table, Value where, Value options) {
  SqliteDb* h = as_db(dbVal);
  if (!h || h->closed || !h->db) return sqlite_throw("database is not open");

  char tableName[SQLITE_CRUD_IDENT_CAP];
  if (!value_as_ident(table, tableName, sizeof(tableName)))
    return sqlite_throw("invalid table name");

  CrudWhere w;
  if (!crud_where_from_value(where, &w))
    return sqlite_throw("invalid where clause");

  CrudOptions opt;
  if (!crud_options_from_value(options, &opt))
    return sqlite_throw("invalid query options");

  return crud_select(h, tableName, &w, &opt, 1);
}

Value node_sqlite_findAll(Value dbVal, Value table, Value where, Value options) {
  SqliteDb* h = as_db(dbVal);
  if (!h || h->closed || !h->db) return sqlite_throw("database is not open");

  char tableName[SQLITE_CRUD_IDENT_CAP];
  if (!value_as_ident(table, tableName, sizeof(tableName)))
    return sqlite_throw("invalid table name");

  CrudWhere w;
  if (!crud_where_from_value(where, &w))
    return sqlite_throw("invalid where clause");

  CrudOptions opt;
  if (!crud_options_from_value(options, &opt))
    return sqlite_throw("invalid query options");

  return crud_select(h, tableName, &w, &opt, 0);
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

  CrudWhere w;
  if (!crud_where_from_value(where, &w))
    return sqlite_throw("invalid where clause");

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
  int wcount = append_where(sql, sizeof(sql), &pos, &w);
  if (wcount < 0) return sqlite_throw("sql too long");

  sqlite3_stmt* stmt = NULL;
  int rc = sqlite3_prepare_v2(h->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK || !stmt) return sqlite_throw_db(h->db, "update prepare failed");

  rc = bind_crud_values(stmt, sets.values, sets.count, 1);
  if (rc != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return sqlite_throw_db(h->db, "update bind failed");
  }
  if (w.bind_count > 0) {
    rc = bind_crud_values(stmt, w.binds, w.bind_count, sets.count + 1);
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

  CrudWhere w;
  if (!crud_where_from_value(where, &w))
    return sqlite_throw("invalid where clause");

  char sql[SQLITE_CRUD_SQL_CAP];
  size_t pos = 0;
  int n = snprintf(sql, sizeof(sql), "DELETE FROM \"%s\"", tableName);
  if (n < 0 || (size_t)n >= sizeof(sql)) return sqlite_throw("sql too long");
  pos = (size_t)n;
  int wcount = append_where(sql, sizeof(sql), &pos, &w);
  if (wcount < 0) return sqlite_throw("sql too long");

  sqlite3_stmt* stmt = NULL;
  int rc = sqlite3_prepare_v2(h->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK || !stmt) return sqlite_throw_db(h->db, "remove prepare failed");

  if (w.bind_count > 0) {
    rc = bind_crud_values(stmt, w.binds, w.bind_count, 1);
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

/* COUNT(*) with same where as find; no LIMIT/OFFSET. Returns number Value. */
static Value crud_count_only(SqliteDb* h, const char* tableName, CrudWhere* where) {
  char sql[SQLITE_CRUD_SQL_CAP];
  size_t pos = 0;
  int n = snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM \"%s\"", tableName);
  if (n < 0 || (size_t)n >= sizeof(sql)) return sqlite_throw("sql too long");
  pos = (size_t)n;
  int wcount = append_where(sql, sizeof(sql), &pos, where);
  if (wcount < 0) return sqlite_throw("sql too long");

  sqlite3_stmt* stmt = NULL;
  int rc = sqlite3_prepare_v2(h->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK || !stmt) return sqlite_throw_db(h->db, "count prepare failed");

  if (where && where->bind_count > 0) {
    rc = bind_crud_values(stmt, where->binds, where->bind_count, 1);
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

Value node_sqlite_count(Value dbVal, Value table, Value where) {
  SqliteDb* h = as_db(dbVal);
  if (!h || h->closed || !h->db) return sqlite_throw("database is not open");

  char tableName[SQLITE_CRUD_IDENT_CAP];
  if (!value_as_ident(table, tableName, sizeof(tableName)))
    return sqlite_throw("invalid table name");

  CrudWhere w;
  if (!crud_where_from_value(where, &w))
    return sqlite_throw("invalid where clause");

  return crud_count_only(h, tableName, &w);
}

/*
 * findAndCount(table, where?, options?) → { rows, total, page?, pageSize?, totalPages? }
 * total is COUNT matching where (limit/offset/order do not affect total).
 */
Value node_sqlite_findAndCount(Value dbVal, Value table, Value where, Value options) {
  SqliteDb* h = as_db(dbVal);
  if (!h || h->closed || !h->db) return sqlite_throw("database is not open");

  char tableName[SQLITE_CRUD_IDENT_CAP];
  if (!value_as_ident(table, tableName, sizeof(tableName)))
    return sqlite_throw("invalid table name");

  CrudWhere w;
  if (!crud_where_from_value(where, &w))
    return sqlite_throw("invalid where clause");

  CrudOptions opt;
  if (!crud_options_from_value(options, &opt))
    return sqlite_throw("invalid query options");

  Value totalVal = crud_count_only(h, tableName, &w);
  /* crud_count_only throws on error via TS_THROW; if we get here total is a number */
  Value rowsVal = crud_select(h, tableName, &w, &opt, 0);

  TSHashMap* map = ts_hashmap_new();
  ts_hashmap_set(map, ts_string_new("rows"), rowsVal);
  ts_hashmap_set(map, ts_string_new("total"), totalVal);

  /* Convenience fields when pageSize/limit known */
  double total = (totalVal.tag == TAG_NUMBER) ? totalVal.as.number : 0;
  int pageSize = 0;
  if (opt.has_limit && opt.limit > 0) pageSize = opt.limit;
  if (pageSize > 0) {
    int page = 1;
    if (opt.has_offset && opt.offset >= 0)
      page = (opt.offset / pageSize) + 1;
    int totalPages = (int)((total + pageSize - 1) / pageSize);
    if (totalPages < 1) totalPages = (total > 0) ? 1 : 0;
    ts_hashmap_set(map, ts_string_new("page"), ts_value_number((double)page));
    ts_hashmap_set(map, ts_string_new("pageSize"), ts_value_number((double)pageSize));
    ts_hashmap_set(map, ts_string_new("totalPages"), ts_value_number((double)totalPages));
  }

  return ts_value_object(map);
}
