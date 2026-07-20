#include "runtime.h"
/* Optional feature flags (generated per-program as out/ts_features.h). */
#include "ts_features.h"

/* Forward declarations */
TSString* ts_object_to_string(TSHashMap* map);
TSString* ts_buffer_toString_utf8(Value buf);

/* Stub: ts_to_string may reference this even when TS_NEED_BUFFER isn't set */
#if !defined(TS_NEED_BUFFER)
TSString* ts_buffer_toString_utf8(Value buf) {
  return ts_string_new("");
}
#endif

/* Debug: verify runtime is compiled correctly */
#include <stdio.h>
static int __runtime_debug = 0;
#if defined(TS_NEED_JSON)
TSString* ts_json_stringify(Value val);
#endif

/* Date implementation */
#if defined(TS_NEED_DATE)
double date_now_ts(void) {
  return ts_date_now();
}

/* Helper: days from epoch */
/* Helper: parse date components from ISO string */
static int is_leap_year(int y) {
  return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0;
}

static int days_from_epoch(int year, int month, int day) {
  int days = 0;
  for (int y = 1970; y < year; y++) {
    days += 365 + is_leap_year(y);
  }
  int daysInMonths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (is_leap_year(year)) daysInMonths[1] = 29;
  for (int m = 1; m < month; m++) {
    days += daysInMonths[m - 1];
  }
  days += day - 1;
  return days;
}

/* Helper: parse date components from ISO string */
static void parse_iso_date(const char* s, int* year, int* month, int* day, int* hour, int* min, int* sec, int* ms) {
  *year = *month = *day = *hour = *min = *sec = *ms = 0;
  if (strlen(s) >= 10) {
    *year = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    *month = (s[5] - '0') * 10 + (s[6] - '0');
    *day = (s[8] - '0') * 10 + (s[9] - '0');
  }
  if (strlen(s) >= 13 && s[10] == 'T') {
    *hour = (s[11] - '0') * 10 + (s[12] - '0');
    *min = (s[14] - '0') * 10 + (s[15] - '0');
    *sec = (s[17] - '0') * 10 + (s[18] - '0');
  }
  if (strlen(s) >= 20 && s[19] == '.') {
    *ms = (s[20] - '0') * 100 + (s[21] - '0') * 10 + (s[22] - '0');
  }
}

double date_parse_ts(TSString* str) {
  if (!str || !str->data) return 0.0;
  int year, month, day, hour, min, sec, ms;
  parse_iso_date(str->data, &year, &month, &day, &hour, &min, &sec, &ms);
  int days = days_from_epoch(year, month, day);
  return (double)days * 86400000.0 +
         (double)hour * 3600000.0 +
         (double)min * 60000.0 +
         (double)sec * 1000.0 +
         (double)ms;
}

static void timestamp_to_components(double timestamp, int* year, int* month, int* day, int* hour, int* min, int* sec, int* ms) {
  int64_t ts = (int64_t)(timestamp / 1000.0);
  *ms = (int)((int64_t)timestamp % 1000);
  *sec = (int)(ts % 60); ts /= 60;
  *min = (int)(ts % 60); ts /= 60;
  *hour = (int)(ts % 24); ts /= 24;

  // ts is now days since epoch (1970-01-01)
  // Iterative approach to find year/month/day
  int64_t days = ts;
  int64_t y = 1970;
  
  while (days >= 0) {
    int leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0;
    int daysInYear = 365 + leap;
    if (days < daysInYear) break;
    days -= daysInYear;
    y++;
  }
  
  while (days < 0) {
    y--;
    int leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0;
    int daysInYear = 365 + leap;
    days += daysInYear;
  }
  
  int daysInMonths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0;
  daysInMonths[1] = 28 + leap;
  
  int m = 0;
  while (m < 12 && days >= daysInMonths[m]) {
    days -= daysInMonths[m];
    m++;
  }
  
  *year = (int)y;
  *month = m + 1; // 1-based
  *day = (int)days + 1; // 1-based
}

int32_t date_getFullYear_ts(double ts) { int y,mo,dy,h,mi,s,ms; timestamp_to_components(ts,&y,&mo,&dy,&h,&mi,&s,&ms); return y; }
int32_t date_getMonth_ts(double ts) { int y,mo,dy,h,mi,s,ms; timestamp_to_components(ts,&y,&mo,&dy,&h,&mi,&s,&ms); return mo - 1; }
int32_t date_getDate_ts(double ts) { int y,mo,dy,h,mi,s,ms; timestamp_to_components(ts,&y,&mo,&dy,&h,&mi,&s,&ms); return dy; }
int32_t date_getDay_ts(double ts) { long td = (long)(ts / 86400000.0); if (ts < 0 && ((long)ts % 86400000) != 0) td--; return (int)((td + 4) % 7); }
int32_t date_getHours_ts(double ts) { int y,mo,dy,h,mi,s,ms; timestamp_to_components(ts,&y,&mo,&dy,&h,&mi,&s,&ms); return h; }
int32_t date_getMinutes_ts(double ts) { int y,mo,dy,h,mi,s,ms; timestamp_to_components(ts,&y,&mo,&dy,&h,&mi,&s,&ms); return mi; }
int32_t date_getSeconds_ts(double ts) { int y,mo,dy,h,mi,s,ms; timestamp_to_components(ts,&y,&mo,&dy,&h,&mi,&s,&ms); return s; }
int32_t date_getMilliseconds_ts(double ts) { int y,mo,dy,h,mi,s,ms; timestamp_to_components(ts,&y,&mo,&dy,&h,&mi,&s,&ms); return ms; }
double date_getTime_ts(double ts) { return ts; }

static const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

TSString* date_toISOString_ts(double ts) {
  int y,mo,dy,h,mi,s,ms; timestamp_to_components(ts,&y,&mo,&dy,&h,&mi,&s,&ms);
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", y, mo, dy, h, mi, s, ms);
  return ts_string_new(buf);
}

TSString* date_toDateString_ts(double ts) {
  int y,mo,dy,h,mi,s,ms; timestamp_to_components(ts,&y,&mo,&dy,&h,&mi,&s,&ms);
  char buf[64];
  snprintf(buf, sizeof(buf), "%s %s %d %d", dayNames[date_getDay_ts(ts)], monthNames[mo-1], dy, y);
  return ts_string_new(buf);
}

TSString* date_toTimeString_ts(double ts) {
  int y,mo,dy,h,mi,s,ms; timestamp_to_components(ts,&y,&mo,&dy,&h,&mi,&s,&ms);
  char buf[32];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d GMT", h, mi, s);
  return ts_string_new(buf);
}

TSString* date_toLocaleString_ts(double ts) {
  int y,mo,dy,h,mi,s,ms; timestamp_to_components(ts,&y,&mo,&dy,&h,&mi,&s,&ms);
  char buf[64];
  snprintf(buf, sizeof(buf), "%s %s %d %d %02d:%02d:%02d GMT", dayNames[date_getDay_ts(ts)], monthNames[mo-1], dy, y, h, mi, s);
  return ts_string_new(buf);
}
#endif /* TS_NEED_DATE */

/* Global error context */
TsErrorContext _ts_current_error;

/* Value constructors */
Value ts_value_number(double n) {
  Value v = { .tag = TAG_NUMBER, .as.number = n };
  return v;
}

Value ts_value_string(TSString* s) {
  Value v = { .tag = TAG_STRING, .as.string = s };
  return v;
}

Value ts_value_boolean(int b) {
  Value v = { .tag = TAG_BOOLEAN, .as.boolean = b };
  return v;
}

Value ts_value_null(void) {
  Value v = { .tag = TAG_NULL };
  return v;
}

Value ts_value_undefined(void) {
  Value v = { .tag = TAG_NULL };
  return v;
}

Value ts_value_array(TSArray* arr) {
  Value v = { .tag = TAG_ARRAY, .as.array = arr };
  return v;
}

Value ts_value_object(void* obj) {
  Value v = { .tag = TAG_OBJECT, .as.object = obj };
  return v;
}

Value ts_value_function(void* fn) {
  Value v = { .tag = TAG_FUNCTION, .as.function = fn };
  return v;
}

/* Type coercion */
double ts_to_number(Value val) {
  switch (val.tag) {
    case TAG_NUMBER: return val.as.number;
    case TAG_BOOLEAN: return val.as.boolean ? 1.0 : 0.0;
    case TAG_STRING: return atof(val.as.string->data);
    case TAG_NULL: return 0.0;
    default: return NAN;
  }
}

TSString* ts_to_string(Value val) {
  char buf[256];
  switch (val.tag) {
    case TAG_NUMBER:
      snprintf(buf, sizeof(buf), "%g", val.as.number);
      return ts_string_new(buf);
    case TAG_BOOLEAN:
      return ts_string_new(val.as.boolean ? "true" : "false");
    case TAG_STRING:
      return val.as.string;
    case TAG_NULL:
      return ts_string_new("null");
    case TAG_ARRAY: {
#if defined(TS_NEED_ARRAY)
      // Convert array to string like [1, 2, 3]
      TSArray* arr = val.as.array;
      if (!arr || arr->length == 0) return ts_string_new("[]");
      // Build string: "[" + elements joined by ", " + "]"
      TSString* result = ts_string_new("[");
      TSString* tmp;
      for (int32_t i = 0; i < arr->length; i++) {
        TSString* elemStr = ts_to_string(arr->items[i]);
        tmp = ts_string_concat(result, elemStr);
        ts_string_free(result);
        result = tmp;
        ts_string_free(elemStr);
        if (i < arr->length - 1) {
          TSString* comma = ts_string_new(", ");
          tmp = ts_string_concat(result, comma);
          ts_string_free(result);
          result = tmp;
          ts_string_free(comma);
        }
      }
      TSString* closing = ts_string_new("]");
      tmp = ts_string_concat(result, closing);
      ts_string_free(result);
      ts_string_free(closing);
      return tmp;
#else
      return ts_string_new("[object Array]");
#endif
    }
    case TAG_FUNCTION:
      return ts_string_new("[Function]");
    case TAG_OBJECT: {
      void* obj = val.as.object;
#if defined(TS_NEED_FETCH)
      /* Check if this is a FetchResponse */
      if (obj && *((int32_t*)obj) == FETCH_RESPONSE_TAG) {
        FetchResponse* resp = (FetchResponse*)obj;
        /* Return Response representation like JavaScript */
        char buf[64];
        snprintf(buf, sizeof(buf), "Response { status: %d, statusText: \"%s\", url: \"stub-url\" }",
                 resp->status, resp->statusText->data);
        return ts_string_new(buf);
      }
#endif
#if defined(TS_NEED_BLOB)
      /* Check if this is a Blob */
      if (obj && *((int32_t*)obj) == BLOB_TAG) {
        Blob* blob = (Blob*)obj;
        char buf[64];
        snprintf(buf, sizeof(buf), "Blob { size: %d, type: \"%s\" }",
                 blob->data->length, blob->type->data);
        return ts_string_new(buf);
      }
#endif
#if defined(TS_NEED_URL)
      /* Check if this is a URL */
      if (obj && *((int32_t*)obj) == URL_TAG) {
        Url* u = (Url*)obj;
        return u->href;
      }
#endif
#if defined(TS_NEED_BUFFER)
      if (obj && *((int32_t*)obj) == BUFFER_TAG) {
        return ts_buffer_toString_utf8(val);
      }
#endif
      TSHashMap* map = (TSHashMap*)obj;
      return ts_object_to_string(map);
    }
    default:
      return ts_string_new("[object Object]");
  }
}
/* Object-to-string using iterator (needs hashmap runtime unit) */
#if defined(TS_NEED_HASHMAP)
typedef struct {
  TSString* result;
  int first;
} ObjectToStringCtx;

static void object_to_string_callback(TSString* key, Value value, void* ctx) {
  ObjectToStringCtx* c = (ObjectToStringCtx*)ctx;
  TSString* tmp;

  if (!c->first) {
    TSString* comma = ts_string_new(", ");
    tmp = ts_string_concat(c->result, comma);
    ts_string_free(c->result);
    c->result = tmp;
    ts_string_free(comma);
  }
  c->first = 0;

  // Add key
  TSString* quote1 = ts_string_new("\"");
  tmp = ts_string_concat(c->result, quote1);
  ts_string_free(c->result);
  c->result = tmp;
  tmp = ts_string_concat(c->result, key);
  ts_string_free(c->result);
  c->result = tmp;
  tmp = ts_string_concat(c->result, quote1);
  ts_string_free(c->result);
  c->result = tmp;
  ts_string_free(quote1);

  // Add ": "
  TSString* colon = ts_string_new(": ");
  tmp = ts_string_concat(c->result, colon);
  ts_string_free(c->result);
  c->result = tmp;
  ts_string_free(colon);

  // Add value (quote strings so objects look JSON-ish)
  if (value.tag == TAG_STRING) {
    TSString* q = ts_string_new("\"");
    tmp = ts_string_concat(c->result, q);
    ts_string_free(c->result);
    c->result = tmp;
    TSString* valStr = value.as.string ? value.as.string : ts_string_new("");
    tmp = ts_string_concat(c->result, valStr);
    ts_string_free(c->result);
    c->result = tmp;
    tmp = ts_string_concat(c->result, q);
    ts_string_free(c->result);
    c->result = tmp;
    ts_string_free(q);
  } else {
    TSString* valStr = ts_to_string(value);
    tmp = ts_string_concat(c->result, valStr);
    ts_string_free(c->result);
    c->result = tmp;
    ts_string_free(valStr);
  }
}

TSString* ts_object_to_string(TSHashMap* map) {
  if (!map) return ts_string_new("[object Object]");
  ObjectToStringCtx ctx = { .result = ts_string_new("{"), .first = 1 };
  ts_hashmap_for_each(map, object_to_string_callback, &ctx);
  TSString* closing = ts_string_new("}");
  TSString* tmp = ts_string_concat(ctx.result, closing);
  ts_string_free(ctx.result);
  ts_string_free(closing);
  return tmp;
}
#else
TSString* ts_object_to_string(TSHashMap* map) {
  (void)map;
  return ts_string_new("[object Object]");
}
#endif

int ts_to_boolean(Value val) {
  switch (val.tag) {
    case TAG_NUMBER: return val.as.number != 0.0 && !isnan(val.as.number);
    case TAG_BOOLEAN: return val.as.boolean;
    case TAG_STRING: return val.as.string->length > 0;
    case TAG_NULL: return 0;
    default: return 1;
  }
}

/* typeof */
Value ts_typeof(Value val) {
  const char* type;
  switch (val.tag) {
    case TAG_NUMBER:   type = "number"; break;
    case TAG_STRING:   type = "string"; break;
    case TAG_BOOLEAN:  type = "boolean"; break;
    case TAG_NULL:     type = "object"; break;
    case TAG_OBJECT:   type = "object"; break;
    case TAG_ARRAY:    type = "object"; break;
    case TAG_FUNCTION: type = "function"; break;
    case TAG_SYMBOL:   type = "symbol"; break;
    default:           type = "undefined"; break;
  }
  return ts_value_string(ts_string_new(type));
}

void ts_throw(Value val) {
  _ts_current_error.error_value = val;
  longjmp(_ts_current_error.jump_buffer, 1);
}

Value ts_error_new(TSString* message) {
  /* Lightweight Error: store message as TAG_STRING.
   * error.message is handled in codegen for string Values; object form
   * with hashmap is used when TS_NEED_HASHMAP is available via other paths. */
  return ts_value_string(message ? message : ts_string_new(""));
}

/* Promise constructor (stub - executor is called synchronously for now) */
Value Promise_constructor(Value executor) {
  /* In a full implementation, this would create a Promise object and
     handle async resolution. For now, we just return undefined. */
  return ts_value_undefined();
}

/* ================================================================
 * ts_inspect — pretty-print Value with indentation (like util.inspect)
 * ================================================================ */
static void ts_inspect_impl(Value val, int indent, int depth, char* out, int* pos, int cap) {
  if (depth > 10) { *pos += snprintf(out + *pos, cap - *pos, "[Object]"); return; }
  if (*pos >= cap - 64) return;

  switch (val.tag) {
    case TAG_NUMBER:
      *pos += snprintf(out + *pos, cap - *pos, "%g", val.as.number);
      break;
    case TAG_BOOLEAN:
      *pos += snprintf(out + *pos, cap - *pos, "%s", val.as.boolean ? "true" : "false");
      break;
    case TAG_STRING:
      *pos += snprintf(out + *pos, cap - *pos, "%s", val.as.string ? val.as.string->data : "");
      break;
    case TAG_NULL:
      *pos += snprintf(out + *pos, cap - *pos, "null");
      break;
    case TAG_FUNCTION:
      *pos += snprintf(out + *pos, cap - *pos, "[Function]");
      break;
    case TAG_ARRAY: {
#if defined(TS_NEED_ARRAY)
      TSArray* arr = val.as.array;
      if (!arr || arr->length == 0) { *pos += snprintf(out + *pos, cap - *pos, "[]"); break; }
      // Check if all elements are simple (numbers/strings/booleans)
      int allSimple = 1;
      for (int32_t i = 0; i < arr->length; i++) {
        int t = arr->items[i].tag;
        if (t != TAG_NUMBER && t != TAG_BOOLEAN && t != TAG_STRING && t != TAG_NULL) { allSimple = 0; break; }
      }
      if (allSimple && arr->length <= 8) {
        // Single-line for simple arrays
        *pos += snprintf(out + *pos, cap - *pos, "[ ");
        for (int32_t i = 0; i < arr->length; i++) {
          ts_inspect_impl(arr->items[i], indent, depth + 1, out, pos, cap);
          if (i < arr->length - 1) *pos += snprintf(out + *pos, cap - *pos, ", ");
        }
        *pos += snprintf(out + *pos, cap - *pos, " ]");
      } else {
        // Multi-line for complex/large arrays
        *pos += snprintf(out + *pos, cap - *pos, "[\n");
        for (int32_t i = 0; i < arr->length; i++) {
          for (int d = 0; d <= indent; d++) *pos += snprintf(out + *pos, cap - *pos, "  ");
          ts_inspect_impl(arr->items[i], indent + 1, depth + 1, out, pos, cap);
          if (i < arr->length - 1) *pos += snprintf(out + *pos, cap - *pos, ",");
          *pos += snprintf(out + *pos, cap - *pos, "\n");
        }
        for (int d = 0; d < indent; d++) *pos += snprintf(out + *pos, cap - *pos, "  ");
        *pos += snprintf(out + *pos, cap - *pos, "]");
      }
#else
      *pos += snprintf(out + *pos, cap - *pos, "[Array]");
#endif
      break;
    }
    case TAG_OBJECT: {
      void* obj = val.as.object;
#if defined(TS_NEED_FETCH)
      /* FetchResponse */
      if (obj && *((int32_t*)obj) == FETCH_RESPONSE_TAG) {
        FetchResponse* resp = (FetchResponse*)obj;
        *pos += snprintf(out + *pos, cap - *pos, "Response { status: %d, statusText: '%s' }",
                         resp->status, resp->statusText->data);
        break;
      }
#endif
#if defined(TS_NEED_BLOB)
      /* Blob */
      if (obj && *((int32_t*)obj) == BLOB_TAG) {
        Blob* blob = (Blob*)obj;
        *pos += snprintf(out + *pos, cap - *pos, "Blob { size: %d, type: '%s' }",
                         blob->data->length, blob->type->data);
        break;
      }
#endif
#if defined(TS_NEED_URL)
      /* URL */
      if (obj && *((int32_t*)obj) == URL_TAG) {
        Url* u = (Url*)obj;
        *pos += snprintf(out + *pos, cap - *pos, "URL { href: '%s' }", u->href->data);
        break;
      }
#endif
#if defined(TS_NEED_BUFFER)
      if (obj && *((int32_t*)obj) == BUFFER_TAG) {
        Buffer* buf = (Buffer*)obj;
        const int32_t maxShow = 50; /* Node default for Buffer */
        int32_t n = buf->length;
        int32_t show = n > maxShow ? maxShow : n;
        *pos += snprintf(out + *pos, cap - *pos, "<Buffer");
        for (int32_t i = 0; i < show; i++) {
          if (*pos >= cap - 8) break;
          *pos += snprintf(out + *pos, cap - *pos, " %02x", buf->data ? buf->data[i] : 0);
        }
        if (n > show) {
          *pos += snprintf(out + *pos, cap - *pos, " ... %d more bytes", n - show);
        }
        *pos += snprintf(out + *pos, cap - *pos, ">");
        break;
      }
#endif
#if defined(TS_NEED_HASHMAP)
      /* TSHashMap — pretty-print key-value pairs via direct iteration */
      TSHashMap* map = (TSHashMap*)obj;
      if (!map) { *pos += snprintf(out + *pos, cap - *pos, "{}"); break; }
      {
        int32_t count = 0;
        for (int32_t i = 0; i < map->capacity; i++) {
          if (map->entries[i].occupied) count++;
        }
        if (count == 0) { *pos += snprintf(out + *pos, cap - *pos, "{}"); break; }
        *pos += snprintf(out + *pos, cap - *pos, "{\n");
        int idx = 0;
        for (int32_t i = 0; i < map->capacity; i++) {
          if (!map->entries[i].occupied) continue;
          for (int d = 0; d <= indent; d++) *pos += snprintf(out + *pos, cap - *pos, "  ");
          *pos += snprintf(out + *pos, cap - *pos, "%s: ", map->entries[i].key->data);
          ts_inspect_impl(map->entries[i].value, indent + 1, depth + 1, out, pos, cap);
          idx++;
          if (idx < count) *pos += snprintf(out + *pos, cap - *pos, ",");
          *pos += snprintf(out + *pos, cap - *pos, "\n");
        }
        for (int d = 0; d < indent; d++) *pos += snprintf(out + *pos, cap - *pos, "  ");
        *pos += snprintf(out + *pos, cap - *pos, "}");
      }
#else
      (void)obj;
      *pos += snprintf(out + *pos, cap - *pos, "[object Object]");
#endif
      break;
    }
    default:
      *pos += snprintf(out + *pos, cap - *pos, "[object Object]");
      break;
  }
}

TSString* ts_inspect(Value val) {
  char* buf = (char*)malloc(4096);
  int pos = 0;
  ts_inspect_impl(val, 0, 0, buf, &pos, 4095);
  buf[pos] = '\0';
  TSString* s = ts_string_new(buf);
  free(buf);
  return s;
}

/* Prefer fputs over printf for common paths — avoids pulling full printf/snprintf CRT. */
static void ts_console_write_line(FILE* fp, Value val) {
  if (val.tag == TAG_STRING && val.as.string && val.as.string->data) {
    fputs(val.as.string->data, fp);
    fputc('\n', fp);
    return;
  }
  if (val.tag == TAG_NUMBER) {
    /* Minimal number print without snprintf when possible */
    char buf[64];
    /* snprintf still used for numbers; only linked if non-string console args exist */
    snprintf(buf, sizeof(buf), "%g", val.as.number);
    fputs(buf, fp);
    fputc('\n', fp);
    return;
  }
  if (val.tag == TAG_BOOLEAN) {
    fputs(val.as.boolean ? "true\n" : "false\n", fp);
    return;
  }
  if (val.tag == TAG_NULL) {
    fputs("null\n", fp);
    return;
  }
  TSString* s = ts_inspect(val);
  if (s && s->data) fputs(s->data, fp);
  fputc('\n', fp);
  ts_string_free(s);
}

/* console.log */
void ts_console_log(Value val) {
  ts_console_write_line(stdout, val);
}

/* Helper: concat multiple Values with space separator */
static TSString* concat_multi_args(Value* args, int argc) {
  if (argc == 0) return ts_string_new("");
  TSString* result = ts_inspect(args[0]);
  for (int i = 1; i < argc; i++) {
    TSString* space = ts_string_new(" ");
    TSString* tmp = ts_string_concat(result, space);
    ts_string_free(result);
    ts_string_free(space);
    result = tmp;
    TSString* elem = ts_inspect(args[i]);
    tmp = ts_string_concat(result, elem);
    ts_string_free(result);
    ts_string_free(elem);
    result = tmp;
  }
  return result;
}

void ts_console_log_multi(Value* args, int argc) {
  /* Fast path: single string arg */
  if (argc == 1) {
    ts_console_write_line(stdout, args[0]);
    return;
  }
  TSString* s = concat_multi_args(args, argc);
  if (s && s->data) fputs(s->data, stdout);
  fputc('\n', stdout);
  ts_string_free(s);
}

/* console.info — blue */
void ts_console_info(Value val) {
  fputs("\033[34m", stdout);
  if (val.tag == TAG_STRING && val.as.string && val.as.string->data) {
    fputs(val.as.string->data, stdout);
  } else {
    TSString* s = ts_inspect(val);
    if (s && s->data) fputs(s->data, stdout);
    ts_string_free(s);
  }
  fputs("\033[0m\n", stdout);
}

void ts_console_info_multi(Value* args, int argc) {
  if (argc == 1) { ts_console_info(args[0]); return; }
  TSString* s = concat_multi_args(args, argc);
  fputs("\033[34m", stdout);
  if (s && s->data) fputs(s->data, stdout);
  fputs("\033[0m\n", stdout);
  ts_string_free(s);
}

/* console.warn — yellow to stderr */
void ts_console_warn(Value val) {
  fputs("\033[33m", stderr);
  if (val.tag == TAG_STRING && val.as.string && val.as.string->data) {
    fputs(val.as.string->data, stderr);
  } else {
    TSString* s = ts_inspect(val);
    if (s && s->data) fputs(s->data, stderr);
    ts_string_free(s);
  }
  fputs("\033[0m\n", stderr);
}

void ts_console_warn_multi(Value* args, int argc) {
  if (argc == 1) { ts_console_warn(args[0]); return; }
  TSString* s = concat_multi_args(args, argc);
  fputs("\033[33m", stderr);
  if (s && s->data) fputs(s->data, stderr);
  fputs("\033[0m\n", stderr);
  ts_string_free(s);
}

/* console.error — red to stderr */
void ts_console_error(Value val) {
  fputs("\033[31m", stderr);
  if (val.tag == TAG_STRING && val.as.string && val.as.string->data) {
    fputs(val.as.string->data, stderr);
  } else {
    TSString* s = ts_inspect(val);
    if (s && s->data) fputs(s->data, stderr);
    ts_string_free(s);
  }
  fputs("\033[0m\n", stderr);
}

void ts_console_error_multi(Value* args, int argc) {
  if (argc == 1) { ts_console_error(args[0]); return; }
  TSString* s = concat_multi_args(args, argc);
  fputs("\033[31m", stderr);
  if (s && s->data) fputs(s->data, stderr);
  fputs("\033[0m\n", stderr);
  ts_string_free(s);
}

/* console.debug — alias for log (gray) */
void ts_console_debug(Value val) {
  fputs("\033[90m", stdout);
  if (val.tag == TAG_STRING && val.as.string && val.as.string->data) {
    fputs(val.as.string->data, stdout);
  } else {
    TSString* s = ts_inspect(val);
    if (s && s->data) fputs(s->data, stdout);
    ts_string_free(s);
  }
  fputs("\033[0m\n", stdout);
}

void ts_console_debug_multi(Value* args, int argc) {
  if (argc == 1) { ts_console_debug(args[0]); return; }
  TSString* s = concat_multi_args(args, argc);
  fputs("\033[90m", stdout);
  if (s && s->data) fputs(s->data, stdout);
  fputs("\033[0m\n", stdout);
  ts_string_free(s);
}

/* console.assert — print to stderr if condition is falsy */
void ts_console_assert(Value condition, Value val) {
  if (!ts_to_boolean(condition)) {
    TSString* s = ts_inspect(val);
    fprintf(stderr, "\033[31mAssertion failed: %s\033[0m\n", s->data);
    ts_string_free(s);
  }
}

/* console.clear — ANSI escape to clear terminal */
void ts_console_clear(void) {
  printf("\033[2J\033[H");
  fflush(stdout);
}

/* console.count / console.countReset */
typedef struct CountEntry {
  char label[64];
  int count;
  struct CountEntry* next;
} CountEntry;

static CountEntry* g_count_list = NULL;

static CountEntry* get_count_entry(const char* label) {
  for (CountEntry* e = g_count_list; e; e = e->next) {
    if (strcmp(e->label, label) == 0) return e;
  }
  CountEntry* e = (CountEntry*)malloc(sizeof(CountEntry));
  snprintf(e->label, sizeof(e->label), "%s", label);
  e->count = 0;
  e->next = g_count_list;
  g_count_list = e;
  return e;
}

void ts_console_count(TSString* label) {
  const char* l = (label && label->data) ? label->data : "default";
  CountEntry* e = get_count_entry(l);
  e->count++;
  printf("%s: %d\n", l, e->count);
}

void ts_console_count_reset(TSString* label) {
  const char* l = (label && label->data) ? label->data : "default";
  CountEntry* e = get_count_entry(l);
  e->count = 0;
}

/* console.dir — display object (simplified: print as JSON-like) */
void ts_console_dir(Value val) {
  TSString* s = ts_to_string(val);
  printf("%s\n", s->data);
  ts_string_free(s);
}

/* console.group / console.groupEnd — indentation tracking */
static int g_group_depth = 0;

void ts_console_group(void) {
  g_group_depth++;
}

void ts_console_group_end(void) {
  if (g_group_depth > 0) g_group_depth--;
}

/* console.table — simplified: print key-value pairs */
void ts_console_table(Value val) {
  TSString* s = ts_to_string(val);
  printf("%s\n", s->data);
  ts_string_free(s);
}

/* console.trace — print stack trace to stderr */
void ts_console_trace(Value val) {
  TSString* s = ts_to_string(val);
  fprintf(stderr, "Trace: %s\n", s->data);
  ts_string_free(s);
}

/* JSON implementation */
#if defined(TS_NEED_JSON)
Value ts_json_parse(TSString* json) {
  if (!json || !json->data) return ts_value_null();

  const char* p = json->data;

  // Skip whitespace
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

  // null
  if (strncmp(p, "null", 4) == 0) return ts_value_null();

  // true
  if (strncmp(p, "true", 4) == 0) return ts_value_boolean(1);

  // false
  if (strncmp(p, "false", 5) == 0) return ts_value_boolean(0);

  // number
  if (*p == '-' || (*p >= '0' && *p <= '9')) {
    char* end;
    double num = strtod(p, &end);
    if (end != p) return ts_value_number(num);
  }

  // string
  if (*p == '"') {
    p++; // skip opening quote
    char buf[4096];
    int len = 0;
    while (*p && *p != '"' && len < 4095) {
      if (*p == '\\') {
        p++;
        switch (*p) {
          case '"': buf[len++] = '"'; break;
          case '\\': buf[len++] = '\\'; break;
          case 'n': buf[len++] = '\n'; break;
          case 't': buf[len++] = '\t'; break;
          case 'r': buf[len++] = '\r'; break;
          default: buf[len++] = *p; break;
        }
      } else {
        buf[len++] = *p;
      }
      p++;
    }
    buf[len] = '\0';
    return ts_value_string(ts_string_new(buf));
  }

  // array
  if (*p == '[') {
    p++; // skip [
    TSArray* arr = ts_array_new();
    while (*p && *p != ']') {
      while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
      if (*p == ']') break;
      // Parse value
      int depth = 0;
      const char* start = p;
      while (*p) {
        if (*p == '[' || *p == '{') depth++;
        if (*p == ']' || *p == '}') {
          if (depth == 0) break;
          depth--;
        }
        if (*p == ',' && depth == 0) break;
        p++;
      }
      // Create substring for recursive parse
      int len = p - start;
      char* valStr = (char*)malloc(len + 1);
      memcpy(valStr, start, len);
      valStr[len] = '\0';
      TSString* valTsStr = ts_string_new(valStr);
      Value val = ts_json_parse(valTsStr);
      ts_string_free(valTsStr);
      free(valStr);
      ts_array_push(arr, val);
      while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
      if (*p == ',') p++;
    }
    return ts_value_array(arr);
  }

  // object
  if (*p == '{') {
    p++; // skip {
    TSHashMap* map = ts_hashmap_new();
    while (*p && *p != '}') {
      while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
      if (*p == '}') break;
      // Parse key (must be string)
      if (*p != '"') break;
      p++; // skip opening quote
      char keyBuf[1024];
      int keyLen = 0;
      while (*p && *p != '"' && keyLen < 1023) {
        if (*p == '\\') {
          p++;
          switch (*p) {
            case '"': keyBuf[keyLen++] = '"'; break;
            case '\\': keyBuf[keyLen++] = '\\'; break;
            case 'n': keyBuf[keyLen++] = '\n'; break;
            case 't': keyBuf[keyLen++] = '\t'; break;
            case 'r': keyBuf[keyLen++] = '\r'; break;
            default: keyBuf[keyLen++] = *p; break;
          }
        } else {
          keyBuf[keyLen++] = *p;
        }
        p++;
      }
      keyBuf[keyLen] = '\0';
      p++; // skip closing quote

      // Skip colon
      while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
      if (*p == ':') p++;
      while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

      // Parse value
      int depth = 0;
      const char* start = p;
      while (*p) {
        if (*p == '[' || *p == '{') depth++;
        if (*p == ']' || *p == '}') {
          if (depth == 0) break;
          depth--;
        }
        if (*p == ',' && depth == 0) break;
        p++;
      }
      int len = p - start;
      char* valStr = (char*)malloc(len + 1);
      memcpy(valStr, start, len);
      valStr[len] = '\0';
      TSString* valTsStr = ts_string_new(valStr);
      Value val = ts_json_parse(valTsStr);
      ts_string_free(valTsStr);
      free(valStr);
      ts_hashmap_set(map, ts_string_new(keyBuf), val);

      while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
      if (*p == ',') p++;
    }
    return ts_value_object(map);
  }

  return ts_value_null();
}

/* Helper for JSON stringify of objects using iterator */
typedef struct {
  TSString* result;
  int first;
} JsonStringifyCtx;

static void json_stringify_callback(TSString* key, Value value, void* ctx) {
  JsonStringifyCtx* c = (JsonStringifyCtx*)ctx;
  TSString* tmp;

  if (!c->first) {
    tmp = ts_string_concat(c->result, ts_string_new(", "));
    ts_string_free(c->result);
    c->result = tmp;
  }
  c->first = 0;

  // Key as JSON string
  TSString* keyStr = ts_json_stringify(ts_value_string(key));
  tmp = ts_string_concat(c->result, keyStr);
  ts_string_free(c->result);
  c->result = tmp;
  ts_string_free(keyStr);

  // Colon
  tmp = ts_string_concat(c->result, ts_string_new(": "));
  ts_string_free(c->result);
  c->result = tmp;

  // Value
  TSString* valStr = ts_json_stringify(value);
  tmp = ts_string_concat(c->result, valStr);
  ts_string_free(c->result);
  c->result = tmp;
  ts_string_free(valStr);
}

TSString* ts_json_stringify(Value val) {
  char buf[64];
  switch (val.tag) {
    case TAG_NUMBER: {
      snprintf(buf, sizeof(buf), "%g", val.as.number);
      return ts_string_new(buf);
    }
    case TAG_BOOLEAN:
      return ts_string_new(val.as.boolean ? "true" : "false");
    case TAG_STRING: {
      // Escape string for JSON
      TSString* s = val.as.string;
      char* out = (char*)malloc(s->length * 2 + 3);
      int j = 0;
      out[j++] = '"';
      for (int i = 0; i < s->length; i++) {
        switch (s->data[i]) {
          case '"': out[j++] = '\\'; out[j++] = '"'; break;
          case '\\': out[j++] = '\\'; out[j++] = '\\'; break;
          case '\n': out[j++] = '\\'; out[j++] = 'n'; break;
          case '\t': out[j++] = '\\'; out[j++] = 't'; break;
          case '\r': out[j++] = '\\'; out[j++] = 'r'; break;
          default: out[j++] = s->data[i]; break;
        }
      }
      out[j++] = '"';
      out[j] = '\0';
      TSString* result = ts_string_new(out);
      free(out);
      return result;
    }
    case TAG_NULL:
      return ts_string_new("null");
    case TAG_ARRAY: {
      TSArray* arr = val.as.array;
      TSString* result = ts_string_new("[");
      TSString* tmp;
      for (int32_t i = 0; i < arr->length; i++) {
        if (i > 0) {
          tmp = ts_string_concat(result, ts_string_new(", "));
          ts_string_free(result);
          result = tmp;
        }
        TSString* elemStr = ts_json_stringify(arr->items[i]);
        tmp = ts_string_concat(result, elemStr);
        ts_string_free(result);
        result = tmp;
        ts_string_free(elemStr);
      }
      tmp = ts_string_concat(result, ts_string_new("]"));
      ts_string_free(result);
      return tmp;
    }
    case TAG_OBJECT: {
      TSHashMap* map = (TSHashMap*)val.as.object;
      JsonStringifyCtx ctx = { .result = ts_string_new("{"), .first = 1 };
      ts_hashmap_for_each(map, json_stringify_callback, &ctx);
      TSString* tmp = ts_string_concat(ctx.result, ts_string_new("}"));
      ts_string_free(ctx.result);
      return tmp;
    }
    default:
      return ts_string_new("null");
  }
}

/* Helper: build indentation string */
static void build_indent(char* buf, int depth, int indent) {
  buf[0] = '\0';
  if (indent <= 0) return;
  int total = depth * indent;
  if (total > 256) total = 256;
  for (int i = 0; i < total; i++) buf[i] = ' ';
  buf[total] = '\0';
}

/* Forward declaration */
static TSString* json_stringify_indent(Value val, int indent, int depth);

/* Context for indented JSON stringify callback */
typedef struct {
  TSString* result;
  int first;
  int indent;
  int depth;
  char indBuf1[256];
} JsonIndentCtx;

/* Forward declaration */
static TSString* json_stringify_indent(Value val, int indent, int depth);

/* Callback for indented JSON object stringify */
static void json_indent_callback(TSString* key, Value value, void* _ctx) {
  JsonIndentCtx* c = (JsonIndentCtx*)_ctx;
  TSString* tmp;
  if (!c->first) {
    tmp = ts_string_concat(c->result, ts_string_new(",\n"));
    ts_string_free(c->result);
    c->result = tmp;
  }
  c->first = 0;
  tmp = ts_string_concat(c->result, ts_string_new(c->indBuf1));
  ts_string_free(c->result);
  c->result = tmp;
  TSString* keyStr = ts_json_stringify(ts_value_string(key));
  tmp = ts_string_concat(c->result, keyStr);
  ts_string_free(c->result);
  c->result = tmp;
  tmp = ts_string_concat(c->result, ts_string_new(": "));
  ts_string_free(c->result);
  c->result = tmp;
  TSString* valStr = json_stringify_indent(value, c->indent, c->depth + 1);
  tmp = ts_string_concat(c->result, valStr);
  ts_string_free(c->result);
  c->result = tmp;
  ts_string_free(keyStr);
  ts_string_free(valStr);
}

/* JSON.stringify with indent support */
TSString* ts_json_stringify_indent(Value val, int indent) {
  return json_stringify_indent(val, indent, 0);
}

static TSString* json_stringify_indent(Value val, int indent, int depth) {
  if (indent <= 0) return ts_json_stringify(val);

  char buf[64];
  char indBuf[256];
  char indBuf1[256];
  build_indent(indBuf, depth, indent);
  build_indent(indBuf1, depth + 1, indent);

  switch (val.tag) {
    case TAG_NUMBER: {
      snprintf(buf, sizeof(buf), "%g", val.as.number);
      return ts_string_new(buf);
    }
    case TAG_BOOLEAN:
      return ts_string_new(val.as.boolean ? "true" : "false");
    case TAG_STRING: {
      TSString* s = val.as.string;
      char* out = (char*)malloc(s->length * 2 + 3);
      int j = 0;
      out[j++] = '"';
      for (int i = 0; i < s->length; i++) {
        switch (s->data[i]) {
          case '"': out[j++] = '\\'; out[j++] = '"'; break;
          case '\\': out[j++] = '\\'; out[j++] = '\\'; break;
          case '\n': out[j++] = '\\'; out[j++] = 'n'; break;
          case '\t': out[j++] = '\\'; out[j++] = 't'; break;
          case '\r': out[j++] = '\\'; out[j++] = 'r'; break;
          default: out[j++] = s->data[i]; break;
        }
      }
      out[j++] = '"';
      out[j] = '\0';
      TSString* result = ts_string_new(out);
      free(out);
      return result;
    }
    case TAG_NULL:
      return ts_string_new("null");
    case TAG_ARRAY: {
      TSArray* arr = val.as.array;
      if (arr->length == 0) return ts_string_new("[]");

      TSString* result = ts_string_new("[\n");
      TSString* tmp;
      for (int32_t i = 0; i < arr->length; i++) {
        TSString* elemStr = json_stringify_indent(arr->items[i], indent, depth + 1);
        tmp = ts_string_concat(result, ts_string_new(indBuf1));
        ts_string_free(result);
        result = tmp;
        tmp = ts_string_concat(result, elemStr);
        ts_string_free(result);
        result = tmp;
        ts_string_free(elemStr);
        if (i < arr->length - 1) {
          tmp = ts_string_concat(result, ts_string_new(",\n"));
          ts_string_free(result);
          result = tmp;
        }
      }
      tmp = ts_string_concat(result, ts_string_new("\n"));
      ts_string_free(result);
      result = tmp;
      tmp = ts_string_concat(result, ts_string_new(indBuf));
      ts_string_free(result);
      result = tmp;
      tmp = ts_string_concat(result, ts_string_new("]"));
      ts_string_free(result);
      return tmp;
    }
    case TAG_OBJECT: {
      TSHashMap* map = (TSHashMap*)val.as.object;
      JsonIndentCtx jctx;
      jctx.result = ts_string_new("{\n");
      jctx.first = 1;
      jctx.indent = indent;
      jctx.depth = depth;
      build_indent(jctx.indBuf1, depth + 1, indent);

      ts_hashmap_for_each(map, json_indent_callback, &jctx);

      TSString* tmp;
      tmp = ts_string_concat(jctx.result, ts_string_new("\n"));
      ts_string_free(jctx.result);
      jctx.result = tmp;
      build_indent(indBuf, depth, indent);
      tmp = ts_string_concat(jctx.result, ts_string_new(indBuf));
      ts_string_free(jctx.result);
      jctx.result = tmp;
      tmp = ts_string_concat(jctx.result, ts_string_new("}"));
      ts_string_free(jctx.result);
      return tmp;
    }
    default:
      return ts_string_new("null");
  }
}

int ts_json_is_raw_json(Value val) {
  // Check if value is a string that starts with { or [
  if (val.tag != TAG_STRING) return 0;
  TSString* s = val.as.string;
  if (s->length == 0) return 0;
  return s->data[0] == '{' || s->data[0] == '[';
}

Value ts_json_raw_json(TSString* raw) {
  // rawJSON returns a special tagged value that won't be re-escaped
  // For simplicity, we return the parsed value
  return ts_json_parse(raw);
}
#endif /* TS_NEED_JSON */

/* Fetch Response object */
#if defined(TS_NEED_FETCH)
static FetchResponse* create_response(int status, const char* statusText, const char* body, const char* url) {
  FetchResponse* resp = (FetchResponse*)malloc(sizeof(FetchResponse));
  resp->type_tag = FETCH_RESPONSE_TAG;
  resp->status = status;
  resp->statusText = ts_string_new(statusText);
  resp->body = ts_string_new(body ? body : "");
  resp->headers = ts_hashmap_new();
  resp->url = ts_string_new(url);
  resp->stream = NULL;
  resp->body_complete = 1;
  return resp;
}

/* Queued body chunk for live curl streaming */
typedef struct FetchChunk {
  char* data;
  size_t len;
  struct FetchChunk* next;
} FetchChunk;

/* Live HTTP body stream / reader — handles filled after platform includes */
typedef struct FetchStreamCtx {
  int32_t type_tag; /* FETCH_STREAM_TAG */
  FetchResponse* response;
  void* hRequest;   /* WinHTTP HINTERNET request, or CURL* easy */
  void* hConnect;   /* WinHTTP connect, or CURLM* multi */
  void* hSession;   /* WinHTTP session, or curl_slist* request headers */
  int closed;
  /* curl multi streaming queue (also usable as generic buffer) */
  FetchChunk* q_head;
  FetchChunk* q_tail;
  int transfer_done; /* 1 when underlying transfer finished */
  int still_running; /* curl multi still_running flag cache */
} FetchStreamCtx;

typedef struct FetchReaderCtx {
  int32_t type_tag; /* FETCH_READER_TAG */
  FetchStreamCtx* stream;
  /* buffered fallback when no live stream */
  TSString* data;
  int offset;
  int done;
} FetchReaderCtx;

static void fetch_chunk_enqueue(FetchStreamCtx* s, const char* data, size_t len) {
  if (!s || !data || len == 0) return;
  FetchChunk* c = (FetchChunk*)malloc(sizeof(FetchChunk));
  if (!c) return;
  c->data = (char*)malloc(len + 1);
  if (!c->data) { free(c); return; }
  memcpy(c->data, data, len);
  c->data[len] = '\0';
  c->len = len;
  c->next = NULL;
  if (s->q_tail) s->q_tail->next = c;
  else s->q_head = c;
  s->q_tail = c;
}

static FetchChunk* fetch_chunk_dequeue(FetchStreamCtx* s) {
  if (!s || !s->q_head) return NULL;
  FetchChunk* c = s->q_head;
  s->q_head = c->next;
  if (!s->q_head) s->q_tail = NULL;
  c->next = NULL;
  return c;
}

static void fetch_chunk_free_all(FetchStreamCtx* s) {
  if (!s) return;
  FetchChunk* c = s->q_head;
  while (c) {
    FetchChunk* n = c->next;
    free(c->data);
    free(c);
    c = n;
  }
  s->q_head = s->q_tail = NULL;
}

/* Extract method from options object */
static TSString* get_method_from_options(Value options) {
  if (options.tag == TAG_OBJECT) {
    TSHashMap* map = (TSHashMap*)options.as.object;
    if (map) {
      Value methodVal = ts_hashmap_get(map, ts_string_new("method"));
      if (methodVal.tag == TAG_STRING) {
        return methodVal.as.string;
      }
    }
  }
  return ts_string_new("GET");
}

/* Extract headers from options object */
static TSHashMap* get_headers_from_options(Value options) {
  if (options.tag == TAG_OBJECT) {
    TSHashMap* map = (TSHashMap*)options.as.object;
    if (map) {
      Value headersVal = ts_hashmap_get(map, ts_string_new("headers"));
      if (headersVal.tag == TAG_OBJECT) {
        return (TSHashMap*)headersVal.as.object;
      }
    }
  }
  return NULL;
}

/* Extract body from options object */
static TSString* get_body_from_options(Value options) {
  if (options.tag == TAG_OBJECT) {
    TSHashMap* map = (TSHashMap*)options.as.object;
    if (map) {
      Value bodyVal = ts_hashmap_get(map, ts_string_new("body"));
      if (bodyVal.tag == TAG_STRING) {
        return bodyVal.as.string;
      }
      /* If body is object, stringify it */
      if (bodyVal.tag == TAG_OBJECT) {
        return ts_json_stringify(bodyVal);
      }
    }
  }
  return NULL;
}

/* Lookup a string header (case-insensitive-ish: exact key first, then lower) */
static TSString* get_header_string(TSHashMap* headers, const char* name) {
  if (!headers || !name) return NULL;
  Value v = ts_hashmap_get(headers, ts_string_new(name));
  if (v.tag == TAG_STRING && v.as.string) return v.as.string;
  /* try lowercase */
  char lower[128];
  size_t n = strlen(name);
  if (n >= sizeof(lower)) return NULL;
  for (size_t i = 0; i < n; i++) {
    char c = name[i];
    lower[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
  }
  lower[n] = '\0';
  v = ts_hashmap_get(headers, ts_string_new(lower));
  if (v.tag == TAG_STRING && v.as.string) return v.as.string;
  return NULL;
}

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")

#ifndef WINHTTP_OPTION_DECOMPRESSION
#define WINHTTP_OPTION_DECOMPRESSION 118
#endif
#ifndef WINHTTP_DECOMPRESSION_FLAG_GZIP
#define WINHTTP_DECOMPRESSION_FLAG_GZIP 0x00000001
#endif
#ifndef WINHTTP_DECOMPRESSION_FLAG_DEFLATE
#define WINHTTP_DECOMPRESSION_FLAG_DEFLATE 0x00000002
#endif

typedef struct {
  wchar_t* buf;
  size_t len;
  size_t cap;
} WinHeaderBuildCtx;

static void win_header_append(WinHeaderBuildCtx* ctx, const wchar_t* s) {
  size_t n = wcslen(s);
  if (ctx->len + n + 1 > ctx->cap) {
    size_t ncap = (ctx->cap ? ctx->cap * 2 : 1024);
    while (ncap < ctx->len + n + 1) ncap *= 2;
    ctx->buf = (wchar_t*)realloc(ctx->buf, ncap * sizeof(wchar_t));
    ctx->cap = ncap;
  }
  memcpy(ctx->buf + ctx->len, s, n * sizeof(wchar_t));
  ctx->len += n;
  ctx->buf[ctx->len] = L'\0';
}

static void win_header_cb(TSString* key, Value value, void* user) {
  WinHeaderBuildCtx* ctx = (WinHeaderBuildCtx*)user;
  if (!key || !key->data) return;
  /* Host is set by WinHTTP; skip empty values */
  if (_stricmp(key->data, "host") == 0) return;
  TSString* valStr = ts_to_string(value);
  if (!valStr || !valStr->data) return;

  wchar_t wkey[256];
  wchar_t wval[4096];
  MultiByteToWideChar(CP_UTF8, 0, key->data, -1, wkey, 256);
  MultiByteToWideChar(CP_UTF8, 0, valStr->data, -1, wval, 4096);
  win_header_append(ctx, wkey);
  win_header_append(ctx, L": ");
  win_header_append(ctx, wval);
  win_header_append(ctx, L"\r\n");
}

Value ts_fetch(TSString* url, Value options) {
  /* Get request options */
  TSString* method = get_method_from_options(options);
  TSHashMap* reqHeaders = get_headers_from_options(options);
  TSString* body = get_body_from_options(options);

  /* Parse URL */
  wchar_t urlW[2048];
  MultiByteToWideChar(CP_UTF8, 0, url->data, -1, urlW, 2048);

  URL_COMPONENTS urlComp = {0};
  urlComp.dwStructSize = sizeof(urlComp);
  urlComp.lpszHostName = (LPWSTR)malloc(1024 * sizeof(wchar_t));
  urlComp.dwHostNameLength = 1024;
  urlComp.lpszUrlPath = (LPWSTR)malloc(2048 * sizeof(wchar_t));
  urlComp.dwUrlPathLength = 2048;
  urlComp.lpszExtraInfo = (LPWSTR)malloc(1024 * sizeof(wchar_t));
  urlComp.dwExtraInfoLength = 1024;

  if (!WinHttpCrackUrl(urlW, 0, 0, &urlComp)) {
    free(urlComp.lpszHostName);
    free(urlComp.lpszUrlPath);
    free(urlComp.lpszExtraInfo);
    return ts_value_object(create_response(0, "Error", "Failed to parse URL", url->data));
  }

  /* Convert host to string */
  wchar_t host[1024] = {0};
  wcsncpy(host, urlComp.lpszHostName, urlComp.dwHostNameLength);

  /* Convert path to string */
  wchar_t path[2048] = {0};
  wcsncpy(path, urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
  if (urlComp.dwExtraInfoLength > 0) {
    wcscat(path, urlComp.lpszExtraInfo);
  }

  free(urlComp.lpszHostName);
  free(urlComp.lpszUrlPath);
  free(urlComp.lpszExtraInfo);

  /* Convert method to wide string */
  wchar_t methodW[32];
  MultiByteToWideChar(CP_UTF8, 0, method->data, -1, methodW, 32);

  /* User-Agent: prefer options.headers, else a browser-like default */
  const char* uaUtf8 = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
  TSString* uaHdr = get_header_string(reqHeaders, "user-agent");
  if (!uaHdr) uaHdr = get_header_string(reqHeaders, "User-Agent");
  if (uaHdr && uaHdr->data) uaUtf8 = uaHdr->data;
  wchar_t uaW[512];
  MultiByteToWideChar(CP_UTF8, 0, uaUtf8, -1, uaW, 512);

  /* Open session */
  HINTERNET hSession = WinHttpOpen(uaW,
    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
    WINHTTP_NO_PROXY_NAME,
    WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) {
    return ts_value_object(create_response(0, "Error", "Failed to open session", url->data));
  }

  /* Connect */
  HINTERNET hConnect = WinHttpConnect(hSession, host,
    urlComp.nPort, 0);
  if (!hConnect) {
    WinHttpCloseHandle(hSession);
    return ts_value_object(create_response(0, "Error", "Failed to connect", url->data));
  }

  /* Create request */
  HINTERNET hRequest = WinHttpOpenRequest(hConnect, methodW, path,
    NULL, WINHTTP_NO_REFERER,
    WINHTTP_DEFAULT_ACCEPT_TYPES,
    urlComp.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
  if (!hRequest) {
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ts_value_object(create_response(0, "Error", "Failed to create request", url->data));
  }

  /* Enable gzip/deflate decompression when available */
  {
    DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof(decomp));
  }
  /* Idle receive timeout: SSE may keep connection open after last event.
     Without this, QueryDataAvailable can block indefinitely waiting for more data. */
  {
    DWORD recvTimeout = 5000; /* 5s */
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &recvTimeout, sizeof(recvTimeout));
  }

  /* Build additional headers from options.headers */
  WinHeaderBuildCtx hdrCtx = {0};
  hdrCtx.buf = (wchar_t*)malloc(1024 * sizeof(wchar_t));
  hdrCtx.cap = 1024;
  hdrCtx.len = 0;
  hdrCtx.buf[0] = L'\0';
  if (reqHeaders) {
    ts_hashmap_for_each(reqHeaders, win_header_cb, &hdrCtx);
  }
  /* Default Content-Type for JSON body if not set */
  if (body && body->length > 0) {
    TSString* ct = get_header_string(reqHeaders, "content-type");
    if (!ct) ct = get_header_string(reqHeaders, "Content-Type");
    if (!ct) {
      win_header_append(&hdrCtx, L"Content-Type: application/json\r\n");
    }
  }

  /* Send request */
  LPVOID requestBody = (body && body->length > 0) ? (LPVOID)body->data : WINHTTP_NO_REQUEST_DATA;
  DWORD requestBodyLen = (body && body->length > 0) ? (DWORD)body->length : 0;
  LPCWSTR extraHeaders = (hdrCtx.len > 0) ? hdrCtx.buf : WINHTTP_NO_ADDITIONAL_HEADERS;
  DWORD extraHeadersLen = (hdrCtx.len > 0) ? (DWORD)-1 : 0;

  BOOL sendResult = WinHttpSendRequest(hRequest, extraHeaders, extraHeadersLen,
    requestBody, requestBodyLen, requestBodyLen, 0);

  free(hdrCtx.buf);

  if (!sendResult) {
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ts_value_object(create_response(0, "Error", "Failed to send request", url->data));
  }

  /* Receive response */
  if (!WinHttpReceiveResponse(hRequest, NULL)) {
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ts_value_object(create_response(0, "Error", "Failed to receive response", url->data));
  }

  /* Get status code */
  DWORD statusCode = 0;
  DWORD statusCodeSize = sizeof(statusCode);
  WinHttpQueryHeaders(hRequest,
    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
    WINHTTP_HEADER_NAME_BY_INDEX,
    &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

  /* Read response headers */
  TSHashMap* respHeaders = ts_hashmap_new();
  {
    DWORD headerBufLen = 0;
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_RAW_HEADERS_CRLF,
        WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER, &headerBufLen, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && headerBufLen > 0) {
      wchar_t* headerBuf = (wchar_t*)malloc(headerBufLen);
      if (headerBuf && WinHttpQueryHeaders(hRequest,
          WINHTTP_QUERY_RAW_HEADERS_CRLF,
          WINHTTP_HEADER_NAME_BY_INDEX,
          headerBuf, &headerBufLen, WINHTTP_NO_HEADER_INDEX)) {
        int utf8Cap = (int)(headerBufLen * 2 + 4);
        char* headerBufUtf8 = (char*)malloc((size_t)utf8Cap);
        if (headerBufUtf8) {
          WideCharToMultiByte(CP_UTF8, 0, headerBuf, -1, headerBufUtf8, utf8Cap, NULL, NULL);
          char* line = strtok(headerBufUtf8, "\r\n");
          while (line) {
            char* colon = strchr(line, ':');
            if (colon) {
              *colon = '\0';
              char* key = line;
              char* value = colon + 1;
              while (*value == ' ') value++;
              ts_hashmap_set(respHeaders, ts_string_new(key), ts_value_string(ts_string_new(value)));
            }
            line = strtok(NULL, "\r\n");
          }
          free(headerBufUtf8);
        }
      }
      free(headerBuf);
    }
  }

  /* Keep connection open for streaming via response.body.getReader().
     Do NOT drain the body here — reader.read() pulls chunks as they arrive. */
  FetchResponse* resp = create_response((int)statusCode, "OK", "", url->data);
  ts_hashmap_free(resp->headers);
  resp->headers = respHeaders;
  resp->body_complete = 0;

  FetchStreamCtx* stream = (FetchStreamCtx*)malloc(sizeof(FetchStreamCtx));
  memset(stream, 0, sizeof(FetchStreamCtx));
  stream->type_tag = FETCH_STREAM_TAG;
  stream->response = resp;
  stream->hRequest = (void*)hRequest;
  stream->hConnect = (void*)hConnect;
  stream->hSession = (void*)hSession;
  stream->closed = 0;
  resp->stream = stream;

  return ts_value_object(resp);
}

#elif defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#include <curl/curl.h>

/* Callback: enqueue live body bytes for reader.read() */
static size_t stream_write_callback(char* ptr, size_t size, size_t nmemb, void* userp) {
  FetchStreamCtx* stream = (FetchStreamCtx*)userp;
  size_t realsize = size * nmemb;
  if (stream && realsize > 0) {
    fetch_chunk_enqueue(stream, ptr, realsize);
  }
  return realsize;
}

/* Callback for curl to write response headers into a TSHashMap* */
static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userp) {
  size_t realsize = size * nitems;
  TSHashMap* headers = (TSHashMap*)userp;

  char line[4096];
  if (realsize < sizeof(line)) {
    memcpy(line, buffer, realsize);
    line[realsize] = '\0';

    if (strncmp(line, "HTTP/", 5) == 0) {
      return realsize;
    }

    char* colon = strchr(line, ':');
    if (colon) {
      *colon = '\0';
      char* key = line;
      char* value = colon + 1;
      while (*value == ' ') value++;
      char* end = value + strlen(value) - 1;
      while (end > value && (*end == '\r' || *end == '\n')) *end-- = '\0';
      ts_hashmap_set(headers, ts_string_new(key), ts_value_string(ts_string_new(value)));
    }
  }
  return realsize;
}

static void curl_header_cb(TSString* key, Value value, void* user) {
  struct curl_slist** list = (struct curl_slist**)user;
  if (!key || !key->data) return;
  TSString* valStr = ts_to_string(value);
  if (!valStr || !valStr->data) return;
  char line[8192];
  snprintf(line, sizeof(line), "%s: %s", key->data, valStr->data);
  *list = curl_slist_append(*list, line);
}

/* Pump multi until a body chunk is queued, headers deliver a status, or transfer ends.
   max_wait_ms: how long to block waiting for socket activity per call (-1 = default). */
static void curl_stream_pump(FetchStreamCtx* stream, int max_wait_ms) {
  if (!stream || stream->closed || !stream->hConnect || !stream->hRequest) return;
  CURLM* multi = (CURLM*)stream->hConnect;
  int still = stream->still_running;
  CURLMcode mc = curl_multi_perform(multi, &still);
  stream->still_running = still;
  if (mc != CURLM_OK) {
    stream->transfer_done = 1;
    stream->still_running = 0;
    return;
  }
  if (still == 0) {
    stream->transfer_done = 1;
    return;
  }
  /* Wait for activity if no chunk yet */
  if (!stream->q_head && still > 0) {
    int numfds = 0;
    curl_multi_wait(multi, NULL, 0, max_wait_ms > 0 ? max_wait_ms : 1000, &numfds);
    mc = curl_multi_perform(multi, &still);
    stream->still_running = still;
    if (mc != CURLM_OK || still == 0) {
      stream->transfer_done = 1;
      stream->still_running = 0;
    }
  }
  /* Drain multi messages for completion */
  int msgs = 0;
  CURLMsg* msg;
  while ((msg = curl_multi_info_read(multi, &msgs)) != NULL) {
    if (msg->msg == CURLMSG_DONE) {
      stream->transfer_done = 1;
      stream->still_running = 0;
    }
  }
}

Value ts_fetch(TSString* url, Value options) {
  TSString* method = get_method_from_options(options);
  TSHashMap* reqHeaders = get_headers_from_options(options);
  TSString* body = get_body_from_options(options);

  CURL* easy = curl_easy_init();
  CURLM* multi = curl_multi_init();
  if (!easy || !multi) {
    if (easy) curl_easy_cleanup(easy);
    if (multi) curl_multi_cleanup(multi);
    return ts_value_object(create_response(0, "Error", "Failed to init curl", url->data));
  }

  FetchStreamCtx* stream = (FetchStreamCtx*)malloc(sizeof(FetchStreamCtx));
  memset(stream, 0, sizeof(FetchStreamCtx));
  stream->type_tag = FETCH_STREAM_TAG;
  stream->closed = 0;
  stream->still_running = 1;

  curl_easy_setopt(easy, CURLOPT_URL, url->data);

  if (strcmp(method->data, "POST") == 0) {
    curl_easy_setopt(easy, CURLOPT_POST, 1L);
  } else if (strcmp(method->data, "PUT") == 0) {
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "PUT");
  } else if (strcmp(method->data, "DELETE") == 0) {
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "DELETE");
  } else if (strcmp(method->data, "HEAD") == 0) {
    curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
  } else if (strcmp(method->data, "OPTIONS") == 0) {
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "OPTIONS");
  } else if (strcmp(method->data, "TRACE") == 0) {
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "TRACE");
  } else if (strcmp(method->data, "PATCH") == 0) {
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "PATCH");
  }

  if (body && body->length > 0) {
    curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body->data);
    curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long)body->length);
  }

  struct curl_slist* headers = NULL;
  if (reqHeaders) {
    ts_hashmap_for_each(reqHeaders, curl_header_cb, &headers);
  }
  if (body && body->length > 0) {
    TSString* ct = get_header_string(reqHeaders, "content-type");
    if (!ct) ct = get_header_string(reqHeaders, "Content-Type");
    if (!ct) headers = curl_slist_append(headers, "Content-Type: application/json");
  }
  TSString* ua = get_header_string(reqHeaders, "user-agent");
  if (!ua) ua = get_header_string(reqHeaders, "User-Agent");
  if (!ua) {
    headers = curl_slist_append(headers,
      "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
  }
  if (headers) {
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
  }

  TSHashMap* respHeaders = ts_hashmap_new();
  curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, header_callback);
  curl_easy_setopt(easy, CURLOPT_HEADERDATA, respHeaders);

  /* Live body streaming into stream queue */
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, stream_write_callback);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, stream);
  curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
  /* Lower latency for SSE / streaming */
  curl_easy_setopt(easy, CURLOPT_BUFFERSIZE, 1024L);
  curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  curl_easy_setopt(easy, CURLOPT_TCP_NODELAY, 1L);
  curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
  /* End transfer if idle after last byte (SSE that never closes) */
  curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 8L);

  curl_multi_add_handle(multi, easy);
  stream->hRequest = easy;
  stream->hConnect = multi;
  stream->hSession = headers;

  /* Pump until we have a status code (headers received) or transfer ends */
  long statusCode = 0;
  for (int i = 0; i < 600; i++) { /* ~up to 60s connect/header wait */
    curl_stream_pump(stream, 100);
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &statusCode);
    if (statusCode > 0 || stream->transfer_done) break;
  }

  FetchResponse* resp = create_response((int)statusCode,
    statusCode > 0 ? "OK" : "Error",
    "", url->data);
  ts_hashmap_free(resp->headers);
  resp->headers = respHeaders;
  resp->body_complete = 0;
  resp->stream = stream;
  stream->response = resp;

  return ts_value_object(resp);
}

#else
/* Fallback: stub implementation */
Value ts_fetch(TSString* url, Value options) {
  return ts_value_object(create_response(200, "OK",
    "{\"status\":200,\"url\":\"stub\",\"message\":\"fetch is a stub\"}",
    url->data));
}
#endif

Value ts_fetch_response(Value response) {
  return response;
}

static void fetch_stream_close(FetchStreamCtx* stream) {
  if (!stream || stream->closed) return;
  stream->closed = 1;
#ifdef _WIN32
  if (stream->hRequest) WinHttpCloseHandle((HINTERNET)stream->hRequest);
  if (stream->hConnect) WinHttpCloseHandle((HINTERNET)stream->hConnect);
  if (stream->hSession) WinHttpCloseHandle((HINTERNET)stream->hSession);
#elif defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
  /* curl multi path: hRequest=CURL*, hConnect=CURLM*, hSession=curl_slist* */
  CURL* easy = (CURL*)stream->hRequest;
  CURLM* multi = (CURLM*)stream->hConnect;
  struct curl_slist* hdrs = (struct curl_slist*)stream->hSession;
  if (multi && easy) {
    curl_multi_remove_handle(multi, easy);
  }
  if (easy) curl_easy_cleanup(easy);
  if (multi) curl_multi_cleanup(multi);
  if (hdrs) curl_slist_free_all(hdrs);
  fetch_chunk_free_all(stream);
#else
  fetch_chunk_free_all(stream);
#endif
  stream->hRequest = stream->hConnect = stream->hSession = NULL;
  stream->transfer_done = 1;
  stream->still_running = 0;
  if (stream->response) {
    stream->response->stream = NULL;
    stream->response->body_complete = 1;
  }
}

/* Drain remaining live stream into resp->body (for .text() / .json()). */
static void fetch_stream_drain(FetchResponse* resp) {
  if (!resp || resp->body_complete) return;
  FetchStreamCtx* stream = (FetchStreamCtx*)resp->stream;
  if (!stream || stream->closed) {
    resp->body_complete = 1;
    return;
  }

  /* Collect already-queued chunks + remaining transfer */
  size_t bodyLen = 0;
  char* bodyBuf = (char*)malloc(1);
  bodyBuf[0] = '\0';
  if (resp->body && resp->body->data && resp->body->length > 0) {
    bodyLen = (size_t)resp->body->length;
    bodyBuf = (char*)realloc(bodyBuf, bodyLen + 1);
    memcpy(bodyBuf, resp->body->data, bodyLen);
    bodyBuf[bodyLen] = '\0';
  }

#ifdef _WIN32
  if (stream->hRequest) {
    for (;;) {
      DWORD bytesAvailable = 0;
      if (!WinHttpQueryDataAvailable((HINTERNET)stream->hRequest, &bytesAvailable)) break;
      if (bytesAvailable == 0) break;
      char* nb = (char*)realloc(bodyBuf, bodyLen + bytesAvailable + 1);
      if (!nb) break;
      bodyBuf = nb;
      DWORD bytesRead = 0;
      if (!WinHttpReadData((HINTERNET)stream->hRequest, bodyBuf + bodyLen, bytesAvailable, &bytesRead) || bytesRead == 0) {
        break;
      }
      bodyLen += bytesRead;
      bodyBuf[bodyLen] = '\0';
    }
  }
#elif defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
  /* Drain curl multi until complete, then flatten queue */
  while (!stream->transfer_done && stream->hConnect) {
    curl_stream_pump(stream, 1000);
  }
  for (;;) {
    FetchChunk* c = fetch_chunk_dequeue(stream);
    if (!c) break;
    char* nb = (char*)realloc(bodyBuf, bodyLen + c->len + 1);
    if (!nb) { free(c->data); free(c); break; }
    bodyBuf = nb;
    memcpy(bodyBuf + bodyLen, c->data, c->len);
    bodyLen += c->len;
    bodyBuf[bodyLen] = '\0';
    free(c->data);
    free(c);
  }
#endif

  resp->body = ts_string_new(bodyBuf ? bodyBuf : "");
  free(bodyBuf);
  fetch_stream_close(stream);
  free(stream);
  resp->stream = NULL;
  resp->body_complete = 1;
}

Value ts_fetch_clone(Value response) {
  if (response.tag != TAG_OBJECT) return response;
  FetchResponse* orig = (FetchResponse*)response.as.object;
  if (orig && orig->type_tag == FETCH_RESPONSE_TAG && !orig->body_complete) {
    fetch_stream_drain(orig);
  }
  FetchResponse* clone = create_response(orig->status, orig->statusText->data,
    orig->body && orig->body->data ? orig->body->data : "", "cloned");
  return ts_value_object(clone);
}

TSString* ts_fetch_text(Value response) {
  if (response.tag == TAG_OBJECT) {
    FetchResponse* resp = (FetchResponse*)response.as.object;
    if (resp && resp->type_tag == FETCH_RESPONSE_TAG) {
      if (!resp->body_complete) fetch_stream_drain(resp);
      return resp->body ? resp->body : ts_string_new("");
    }
    /* stream / reader objects */
    return ts_to_string(response);
  }
  if (response.tag == TAG_STRING) return response.as.string;
  return ts_to_string(response);
}

Value ts_fetch_json(Value response) {
  TSString* text = ts_fetch_text(response);
  return ts_json_parse(text);
}

double ts_fetch_response_status(Value response) {
  if (response.tag == TAG_OBJECT) {
    FetchResponse* resp = (FetchResponse*)response.as.object;
    if (resp && resp->type_tag == FETCH_RESPONSE_TAG) {
      return (double)resp->status;
    }
  }
  return 0;
}

TSString* ts_fetch_response_statusText(Value response) {
  if (response.tag == TAG_OBJECT) {
    FetchResponse* resp = (FetchResponse*)response.as.object;
    if (resp && resp->type_tag == FETCH_RESPONSE_TAG) {
      return resp->statusText;
    }
  }
  return ts_string_new("");
}

TSString* ts_fetch_response_url(Value response) {
  if (response.tag == TAG_OBJECT) {
    FetchResponse* resp = (FetchResponse*)response.as.object;
    if (resp && resp->type_tag == FETCH_RESPONSE_TAG) {
      return resp->url;
    }
  }
  return ts_string_new("stub-url");
}

Value ts_fetch_response_headers(Value response) {
  if (response.tag == TAG_OBJECT) {
    FetchResponse* resp = (FetchResponse*)response.as.object;
    if (resp && resp->type_tag == FETCH_RESPONSE_TAG) {
      return ts_value_object(resp->headers);
    }
  }
  return ts_value_object(ts_hashmap_new());
}

/* response.body — ReadableStream-like wrapper around live FetchStreamCtx or buffered body */
Value ts_fetch_response_body(Value response) {
  if (response.tag == TAG_OBJECT) {
    FetchResponse* resp = (FetchResponse*)response.as.object;
    if (resp && resp->type_tag == FETCH_RESPONSE_TAG) {
      /* Return the response itself; getReader peeks at stream / body */
      return response;
    }
  }
  /* Fallback empty stream */
  TSHashMap* body = ts_hashmap_new();
  ts_hashmap_set(body, ts_string_new("_data"), ts_value_string(ts_string_new("")));
  ts_hashmap_set(body, ts_string_new("_type"), ts_value_string(ts_string_new("ReadableStream")));
  return ts_value_object(body);
}

Value ts_fetch_body_get_reader(Value body) {
  FetchReaderCtx* reader = (FetchReaderCtx*)malloc(sizeof(FetchReaderCtx));
  reader->type_tag = FETCH_READER_TAG;
  reader->stream = NULL;
  reader->data = ts_string_new("");
  reader->offset = 0;
  reader->done = 0;

  if (body.tag == TAG_OBJECT && body.as.object) {
    FetchResponse* resp = (FetchResponse*)body.as.object;
    if (resp->type_tag == FETCH_RESPONSE_TAG) {
      if (resp->stream && !resp->body_complete) {
        reader->stream = (FetchStreamCtx*)resp->stream;
      } else {
        if (!resp->body_complete) fetch_stream_drain(resp);
        reader->data = resp->body ? resp->body : ts_string_new("");
      }
      return ts_value_object(reader);
    }
    /* HashMap body with _data (legacy / fallback) */
    Value dataVal = ts_hashmap_get((TSHashMap*)body.as.object, ts_string_new("_data"));
    if (dataVal.tag == TAG_STRING && dataVal.as.string) {
      reader->data = dataVal.as.string;
    }
  } else if (body.tag == TAG_STRING && body.as.string) {
    reader->data = body.as.string;
  }
  return ts_value_object(reader);
}

Value ts_fetch_reader_read(Value readerVal) {
  TSHashMap* result = ts_hashmap_new();
  if (readerVal.tag != TAG_OBJECT || !readerVal.as.object) {
    ts_hashmap_set(result, ts_string_new("done"), ts_value_boolean(1));
    ts_hashmap_set(result, ts_string_new("value"), ts_value_string(ts_string_new("")));
    return ts_value_object(result);
  }

  /* Live stream reader */
  FetchReaderCtx* reader = (FetchReaderCtx*)readerVal.as.object;
  if (reader->type_tag == FETCH_READER_TAG) {
    if (reader->done) {
      ts_hashmap_set(result, ts_string_new("done"), ts_value_boolean(1));
      ts_hashmap_set(result, ts_string_new("value"), ts_value_string(ts_string_new("")));
      return ts_value_object(result);
    }

    /* Live stream: block until next chunk arrives */
    if (reader->stream && !reader->stream->closed) {
#ifdef _WIN32
      if (reader->stream->hRequest) {
        HINTERNET hReq = (HINTERNET)reader->stream->hRequest;
        DWORD bytesAvailable = 0;
        if (!WinHttpQueryDataAvailable(hReq, &bytesAvailable) || bytesAvailable == 0) {
          reader->done = 1;
          fetch_stream_close(reader->stream);
          ts_hashmap_set(result, ts_string_new("done"), ts_value_boolean(1));
          ts_hashmap_set(result, ts_string_new("value"), ts_value_string(ts_string_new("")));
          return ts_value_object(result);
        }
        char* buf = (char*)malloc(bytesAvailable + 1);
        if (!buf) {
          reader->done = 1;
          ts_hashmap_set(result, ts_string_new("done"), ts_value_boolean(1));
          ts_hashmap_set(result, ts_string_new("value"), ts_value_string(ts_string_new("")));
          return ts_value_object(result);
        }
        DWORD bytesRead = 0;
        if (!WinHttpReadData(hReq, buf, bytesAvailable, &bytesRead) || bytesRead == 0) {
          free(buf);
          reader->done = 1;
          fetch_stream_close(reader->stream);
          ts_hashmap_set(result, ts_string_new("done"), ts_value_boolean(1));
          ts_hashmap_set(result, ts_string_new("value"), ts_value_string(ts_string_new("")));
          return ts_value_object(result);
        }
        buf[bytesRead] = '\0';
        if (reader->stream->response) {
          FetchResponse* resp = reader->stream->response;
          if (resp->body && resp->body->data && resp->body->length > 0) {
            char* combined = (char*)malloc((size_t)resp->body->length + bytesRead + 1);
            memcpy(combined, resp->body->data, (size_t)resp->body->length);
            memcpy(combined + resp->body->length, buf, bytesRead);
            combined[resp->body->length + bytesRead] = '\0';
            resp->body = ts_string_new(combined);
            free(combined);
          } else {
            resp->body = ts_string_new(buf);
          }
        }
        TSString* chunk = ts_string_new(buf);
        free(buf);
        ts_hashmap_set(result, ts_string_new("done"), ts_value_boolean(0));
        ts_hashmap_set(result, ts_string_new("value"), ts_value_string(chunk));
        return ts_value_object(result);
      }
#elif defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
      /* curl multi: pump until a chunk is available or transfer ends */
      FetchStreamCtx* stream = reader->stream;
      while (!stream->q_head && !stream->transfer_done && !stream->closed) {
        curl_stream_pump(stream, 1000);
      }
      FetchChunk* c = fetch_chunk_dequeue(stream);
      if (c) {
        /* Append to response body cache for later text() */
        if (stream->response) {
          FetchResponse* resp = stream->response;
          if (resp->body && resp->body->data && resp->body->length > 0) {
            char* combined = (char*)malloc((size_t)resp->body->length + c->len + 1);
            memcpy(combined, resp->body->data, (size_t)resp->body->length);
            memcpy(combined + resp->body->length, c->data, c->len);
            combined[resp->body->length + c->len] = '\0';
            resp->body = ts_string_new(combined);
            free(combined);
          } else {
            resp->body = ts_string_new(c->data);
          }
        }
        TSString* chunk = ts_string_new(c->data);
        free(c->data);
        free(c);
        ts_hashmap_set(result, ts_string_new("done"), ts_value_boolean(0));
        ts_hashmap_set(result, ts_string_new("value"), ts_value_string(chunk));
        return ts_value_object(result);
      }
      /* No more chunks and transfer done */
      reader->done = 1;
      fetch_stream_close(stream);
      ts_hashmap_set(result, ts_string_new("done"), ts_value_boolean(1));
      ts_hashmap_set(result, ts_string_new("value"), ts_value_string(ts_string_new("")));
      return ts_value_object(result);
#endif
    }

    /* Buffered fallback: emit remaining data as one chunk (or line-by-line if multi-line) */
    TSString* data = reader->data ? reader->data : ts_string_new("");
    int len = data && data->data ? (int)data->length : 0;
    if (reader->offset >= len) {
      reader->done = 1;
      ts_hashmap_set(result, ts_string_new("done"), ts_value_boolean(1));
      ts_hashmap_set(result, ts_string_new("value"), ts_value_string(ts_string_new("")));
      return ts_value_object(result);
    }
    /* Prefer line-based chunks so buffered SSE still looks progressive when possible */
    int start = reader->offset;
    int end = start;
    while (end < len && data->data[end] != '\n') end++;
    if (end < len && data->data[end] == '\n') end++; /* include newline */
    if (end == start) end = len; /* no newline — rest of buffer */
    int chunkLen = end - start;
    char* chunkBuf = (char*)malloc((size_t)chunkLen + 1);
    memcpy(chunkBuf, data->data + start, (size_t)chunkLen);
    chunkBuf[chunkLen] = '\0';
    reader->offset = end;
    if (reader->offset >= len) reader->done = 1;
    TSString* chunk = ts_string_new(chunkBuf);
    free(chunkBuf);
    ts_hashmap_set(result, ts_string_new("done"), ts_value_boolean(0));
    ts_hashmap_set(result, ts_string_new("value"), ts_value_string(chunk));
    return ts_value_object(result);
  }

  /* Legacy hashmap reader */
  TSHashMap* map = (TSHashMap*)readerVal.as.object;
  Value doneVal = ts_hashmap_get(map, ts_string_new("_done"));
  if (doneVal.tag == TAG_BOOLEAN && doneVal.as.boolean) {
    ts_hashmap_set(result, ts_string_new("done"), ts_value_boolean(1));
    ts_hashmap_set(result, ts_string_new("value"), ts_value_string(ts_string_new("")));
    return ts_value_object(result);
  }
  Value dataVal = ts_hashmap_get(map, ts_string_new("_data"));
  TSString* data = (dataVal.tag == TAG_STRING && dataVal.as.string)
    ? dataVal.as.string : ts_string_new("");
  Value offsetVal = ts_hashmap_get(map, ts_string_new("_offset"));
  int offset = (offsetVal.tag == TAG_NUMBER) ? (int)offsetVal.as.number : 0;
  int len = data && data->data ? (int)data->length : 0;
  if (offset >= len) {
    ts_hashmap_set(map, ts_string_new("_done"), ts_value_boolean(1));
    ts_hashmap_set(result, ts_string_new("done"), ts_value_boolean(1));
    ts_hashmap_set(result, ts_string_new("value"), ts_value_string(ts_string_new("")));
    return ts_value_object(result);
  }
  ts_hashmap_set(map, ts_string_new("_offset"), ts_value_number((double)len));
  ts_hashmap_set(map, ts_string_new("_done"), ts_value_boolean(1));
  ts_hashmap_set(result, ts_string_new("done"), ts_value_boolean(0));
  ts_hashmap_set(result, ts_string_new("value"), ts_value_string(data));
  return ts_value_object(result);
}

#endif /* TS_NEED_FETCH */

/* Headers constructor — only when fetch/headers are used (needs hashmap) */
#if defined(TS_NEED_FETCH)
Value ts_headers(void) {
  return ts_value_object(ts_hashmap_new());
}

Value ts_headers_from_object(TSHashMap* obj) {
  return ts_value_object(obj);
}

void ts_headers_set(Value headers, TSString* key, TSString* value) {
  if (headers.tag == TAG_OBJECT) {
    TSHashMap* map = (TSHashMap*)headers.as.object;
    if (map) {
      ts_hashmap_set(map, key, ts_value_string(value));
    }
  }
}
#endif /* TS_NEED_FETCH */

/* Blob */
#if defined(TS_NEED_BLOB)
Value ts_blob_new(void) {
  Blob* blob = (Blob*)malloc(sizeof(Blob));
  blob->type_tag = BLOB_TAG;
  blob->data = ts_string_new("");
  blob->type = ts_string_new("");
  return ts_value_object(blob);
}

Value ts_blob_from_string(TSString* data, TSString* type) {
  Blob* blob = (Blob*)malloc(sizeof(Blob));
  blob->type_tag = BLOB_TAG;
  blob->data = data;
  blob->type = type ? type : ts_string_new("");
  return ts_value_object(blob);
}

TSString* ts_blob_text(Value blob) {
  if (blob.tag == TAG_OBJECT) {
    Blob* b = (Blob*)blob.as.object;
    if (b && b->type_tag == BLOB_TAG) {
      return b->data;
    }
  }
  return ts_string_new("");
}

double ts_blob_size(Value blob) {
  if (blob.tag == TAG_OBJECT) {
    Blob* b = (Blob*)blob.as.object;
    if (b && b->type_tag == BLOB_TAG) {
      return (double)b->data->length;
    }
  }
  return 0;
}

TSString* ts_blob_type(Value blob) {
  if (blob.tag == TAG_OBJECT) {
    Blob* b = (Blob*)blob.as.object;
    if (b && b->type_tag == BLOB_TAG) {
      return b->type;
    }
  }
  return ts_string_new("");
}
#endif /* TS_NEED_BLOB */

/* URL */
#if defined(TS_NEED_URL)
Value ts_url_new(TSString* urlStr) {
  Url* url = (Url*)malloc(sizeof(Url));
  url->type_tag = URL_TAG;
  url->href = urlStr;

  /* Parse URL components */
  const char* s = urlStr->data;
  url->protocol = ts_string_new("");
  url->host = ts_string_new("");
  url->hostname = ts_string_new("");
  url->port = ts_string_new("");
  url->pathname = ts_string_new("/");
  url->search = ts_string_new("");
  url->hash = ts_string_new("");
  url->origin = ts_string_new("");

  /* Parse protocol */
  const char* protoEnd = strstr(s, "://");
  if (protoEnd) {
    url->protocol = ts_string_substring(urlStr, 0, (int32_t)(protoEnd - s + 3));
    s = protoEnd + 3;
  }

  /* Parse host (and optional port) */
  const char* pathStart = strchr(s, '/');
  const char* queryStart = strchr(s, '?');
  const char* hashStart = strchr(s, '#');

  const char* hostEnd = pathStart ? pathStart : (queryStart ? queryStart : (hashStart ? hashStart : s + strlen(s)));

  char hostBuf[1024];
  int hostLen = (int)(hostEnd - s);
  if (hostLen >= 1024) hostLen = 1023;
  memcpy(hostBuf, s, hostLen);
  hostBuf[hostLen] = '\0';

  url->host = ts_string_new(hostBuf);

  /* Split host and port */
  const char* colon = strchr(hostBuf, ':');
  if (colon) {
    int hostnameLen = (int)(colon - hostBuf);
    char* hostnameBuf = (char*)malloc(hostnameLen + 1);
    memcpy(hostnameBuf, hostBuf, hostnameLen);
    hostnameBuf[hostnameLen] = '\0';
    url->hostname = ts_string_new(hostnameBuf);
    free(hostnameBuf);
    url->port = ts_string_new(colon + 1);
  } else {
    url->hostname = ts_string_new(hostBuf);
    url->port = ts_string_new("");
  }

  /* Parse pathname */
  if (pathStart) {
    const char* pathEnd = queryStart ? queryStart : (hashStart ? hashStart : s + strlen(s));
    int pathLen = (int)(pathEnd - pathStart);
    char* pathBuf = (char*)malloc(pathLen + 1);
    memcpy(pathBuf, pathStart, pathLen);
    pathBuf[pathLen] = '\0';
    url->pathname = ts_string_new(pathBuf);
    free(pathBuf);
  }

  /* Parse search (query) */
  if (queryStart) {
    const char* searchEnd = hashStart ? hashStart : s + strlen(s);
    int searchLen = (int)(searchEnd - queryStart);
    char* searchBuf = (char*)malloc(searchLen + 1);
    memcpy(searchBuf, queryStart, searchLen);
    searchBuf[searchLen] = '\0';
    url->search = ts_string_new(searchBuf);
    free(searchBuf);
  }

  /* Parse hash */
  if (hashStart) {
    url->hash = ts_string_new(hashStart);
  }

  /* Origin = protocol + host */
  url->origin = ts_string_concat(url->protocol, url->host);

  return ts_value_object(url);
}

TSString* ts_url_href(Value url) {
  if (url.tag == TAG_OBJECT) {
    Url* u = (Url*)url.as.object;
    if (u && u->type_tag == URL_TAG) return u->href;
  }
  return ts_string_new("");
}

TSString* ts_url_protocol(Value url) {
  if (url.tag == TAG_OBJECT) {
    Url* u = (Url*)url.as.object;
    if (u && u->type_tag == URL_TAG) return u->protocol;
  }
  return ts_string_new("");
}

TSString* ts_url_host(Value url) {
  if (url.tag == TAG_OBJECT) {
    Url* u = (Url*)url.as.object;
    if (u && u->type_tag == URL_TAG) return u->host;
  }
  return ts_string_new("");
}

TSString* ts_url_hostname(Value url) {
  if (url.tag == TAG_OBJECT) {
    Url* u = (Url*)url.as.object;
    if (u && u->type_tag == URL_TAG) return u->hostname;
  }
  return ts_string_new("");
}

TSString* ts_url_port(Value url) {
  if (url.tag == TAG_OBJECT) {
    Url* u = (Url*)url.as.object;
    if (u && u->type_tag == URL_TAG) return u->port;
  }
  return ts_string_new("");
}

TSString* ts_url_pathname(Value url) {
  if (url.tag == TAG_OBJECT) {
    Url* u = (Url*)url.as.object;
    if (u && u->type_tag == URL_TAG) return u->pathname;
  }
  return ts_string_new("");
}

TSString* ts_url_search(Value url) {
  if (url.tag == TAG_OBJECT) {
    Url* u = (Url*)url.as.object;
    if (u && u->type_tag == URL_TAG) return u->search;
  }
  return ts_string_new("");
}

TSString* ts_url_hash(Value url) {
  if (url.tag == TAG_OBJECT) {
    Url* u = (Url*)url.as.object;
    if (u && u->type_tag == URL_TAG) return u->hash;
  }
  return ts_string_new("");
}

TSString* ts_url_toString(Value url) {
  return ts_url_href(url);
}

TSString* ts_url_origin(Value url) {
  if (url.tag == TAG_OBJECT) {
    Url* u = (Url*)url.as.object;
    if (u && u->type_tag == URL_TAG) return u->origin;
  }
  return ts_string_new("");
}
#endif /* TS_NEED_URL */

/* Buffer */
#if defined(TS_NEED_BUFFER)
Value ts_buffer_new(int32_t size) {
  Buffer* buf = (Buffer*)malloc(sizeof(Buffer));
  buf->type_tag = BUFFER_TAG;
  buf->length = size;
  buf->capacity = size > 0 ? size : 16;
  buf->data = (uint8_t*)calloc(buf->capacity, 1);
  return ts_value_object(buf);
}

Value ts_buffer_from_string(TSString* str) {
  if (!str || !str->data) return ts_buffer_new(0);
  Buffer* buf = (Buffer*)malloc(sizeof(Buffer));
  buf->type_tag = BUFFER_TAG;
  buf->length = str->length;
  buf->capacity = str->length > 0 ? str->length : 16;
  buf->data = (uint8_t*)malloc(buf->capacity);
  memcpy(buf->data, str->data, str->length);
  return ts_value_object(buf);
}

Value ts_buffer_from_array(TSArray* arr) {
  if (!arr || arr->length == 0) return ts_buffer_new(0);
  Buffer* buf = (Buffer*)malloc(sizeof(Buffer));
  buf->type_tag = BUFFER_TAG;
  buf->length = arr->length;
  buf->capacity = arr->length;
  buf->data = (uint8_t*)malloc(buf->capacity);
  for (int32_t i = 0; i < arr->length; i++) {
    buf->data[i] = (uint8_t)ts_to_number(arr->items[i]);
  }
  return ts_value_object(buf);
}

Value ts_buffer_alloc(int32_t size) {
  return ts_buffer_new(size);
}

Value ts_buffer_allocUnsafe(int32_t size) {
  return ts_buffer_new(size);
}

Value ts_buffer_concat(Value* buffers, int32_t count) {
  int32_t totalLen = 0;
  for (int32_t i = 0; i < count; i++) {
    if (buffers[i].tag == TAG_OBJECT) {
      Buffer* b = (Buffer*)buffers[i].as.object;
      if (b && b->type_tag == BUFFER_TAG) totalLen += b->length;
    }
  }
  Buffer* result = (Buffer*)malloc(sizeof(Buffer));
  result->type_tag = BUFFER_TAG;
  result->length = totalLen;
  result->capacity = totalLen > 0 ? totalLen : 16;
  result->data = (uint8_t*)malloc(result->capacity);
  int32_t offset = 0;
  for (int32_t i = 0; i < count; i++) {
    if (buffers[i].tag == TAG_OBJECT) {
      Buffer* b = (Buffer*)buffers[i].as.object;
      if (b && b->type_tag == BUFFER_TAG) {
        memcpy(result->data + offset, b->data, b->length);
        offset += b->length;
      }
    }
  }
  return ts_value_object(result);
}

int32_t ts_buffer_length(Value buf) {
  if (buf.tag == TAG_OBJECT) {
    Buffer* b = (Buffer*)buf.as.object;
    if (b && b->type_tag == BUFFER_TAG) return b->length;
  }
  return 0;
}

uint8_t ts_buffer_readUInt8(Value buf, int32_t offset) {
  if (buf.tag == TAG_OBJECT) {
    Buffer* b = (Buffer*)buf.as.object;
    if (b && b->type_tag == BUFFER_TAG && offset >= 0 && offset < b->length) {
      return b->data[offset];
    }
  }
  return 0;
}

void ts_buffer_writeUInt8(Value buf, int32_t offset, uint8_t value) {
  if (buf.tag == TAG_OBJECT) {
    Buffer* b = (Buffer*)buf.as.object;
    if (b && b->type_tag == BUFFER_TAG && offset >= 0 && offset < b->length) {
      b->data[offset] = value;
    }
  }
}

Value ts_buffer_slice(Value buf, int32_t start, int32_t end) {
  if (buf.tag != TAG_OBJECT) return ts_buffer_new(0);
  Buffer* b = (Buffer*)buf.as.object;
  if (!b || b->type_tag != BUFFER_TAG) return ts_buffer_new(0);
  if (start < 0) start = 0;
  if (end < 0 || end > b->length) end = b->length;
  if (start >= end) return ts_buffer_new(0);
  int32_t len = end - start;
  Buffer* result = (Buffer*)malloc(sizeof(Buffer));
  result->type_tag = BUFFER_TAG;
  result->length = len;
  result->capacity = len;
  result->data = (uint8_t*)malloc(len);
  memcpy(result->data, b->data + start, len);
  return ts_value_object(result);
}

TSString* ts_buffer_toString_utf8(Value buf) {
  if (buf.tag != TAG_OBJECT) return ts_string_new("");
  Buffer* b = (Buffer*)buf.as.object;
  if (!b || b->type_tag != BUFFER_TAG) return ts_string_new("");
  return ts_string_new_len((const char*)b->data, b->length);
}

TSString* ts_buffer_toString_hex(Value buf) {
  if (buf.tag != TAG_OBJECT) return ts_string_new("");
  Buffer* b = (Buffer*)buf.as.object;
  if (!b || b->type_tag != BUFFER_TAG) return ts_string_new("");
  char* hex = (char*)malloc(b->length * 2 + 1);
  for (int32_t i = 0; i < b->length; i++) {
    snprintf(hex + i * 2, 3, "%02x", b->data[i]);
  }
  hex[b->length * 2] = '\0';
  TSString* result = ts_string_new(hex);
  free(hex);
  return result;
}

TSString* ts_buffer_toString_base64(Value buf) {
  if (buf.tag != TAG_OBJECT) return ts_string_new("");
  Buffer* b = (Buffer*)buf.as.object;
  if (!b || b->type_tag != BUFFER_TAG) return ts_string_new("");
  static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int32_t len = b->length;
  int32_t outLen = (len + 2) / 3 * 4;
  char* out = (char*)malloc(outLen + 1);
  int32_t j = 0;
  for (int32_t i = 0; i < len; i += 3) {
    uint32_t a = b->data[i];
    uint32_t b1 = (i + 1 < len) ? b->data[i + 1] : 0;
    uint32_t c = (i + 2 < len) ? b->data[i + 2] : 0;
    uint32_t triple = (a << 16) | (b1 << 8) | c;
    out[j++] = table[(triple >> 18) & 0x3F];
    out[j++] = table[(triple >> 12) & 0x3F];
    out[j++] = (i + 1 < len) ? table[(triple >> 6) & 0x3F] : '=';
    out[j++] = (i + 2 < len) ? table[triple & 0x3F] : '=';
  }
  out[j] = '\0';
  TSString* result = ts_string_new(out);
  free(out);
  return result;
}

int ts_buffer_isBuffer(Value val) {
  if (val.tag == TAG_OBJECT) {
    Buffer* b = (Buffer*)val.as.object;
    if (b && b->type_tag == BUFFER_TAG) return 1;
  }
  return 0;
}
#endif /* TS_NEED_BUFFER */

/* GC init placeholder */
#if defined(TS_NEED_GC)
static int gc_initialized = 0;

void ts_gc_init(void) {
  if (gc_initialized) return;
  gc_initialized = 1;
  /* Initialize mark-sweep GC */
}

void* ts_gc_alloc(size_t size) {
  return malloc(size);
}

void ts_gc_collect(void) {
  /* Mark-sweep pass — placeholder */
}
#else
void ts_gc_init(void) {}
void* ts_gc_alloc(size_t size) { return malloc(size); }
void ts_gc_collect(void) {}
#endif /* TS_NEED_GC */
