#ifndef TS_RUNTIME_H
#define TS_RUNTIME_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <setjmp.h>

/* CommonJS module globals (set by generated main.c for the entry file) */
extern const char* __ts_dirname;
extern const char* __ts_filename;

/* Value type — tagged union */
typedef enum {
  TAG_NUMBER, TAG_STRING, TAG_BOOLEAN, TAG_NULL,
  TAG_OBJECT, TAG_ARRAY, TAG_FUNCTION, TAG_SYMBOL
} ValueTag;

typedef struct Value {
  ValueTag tag;
  union {
    double number;
    int boolean;
    struct TSString* string;
    void* object;
    struct TSArray* array;
    void* function;
    int symbol;
  } as;
} Value;

/* TSString */
typedef struct TSString {
  int32_t refcount;
  int32_t length;
  char* data;
} TSString;

TSString* ts_string_new(const char* cstr);
TSString* ts_string_new_len(const char* data, int32_t len);
TSString* ts_string_concat(TSString* a, TSString* b);
int ts_string_equals(TSString* a, TSString* b);
TSString* ts_number_to_string(double n);
char ts_string_char_at(TSString* s, int32_t index);
void ts_string_free(TSString* s);
int ts_string_index_of(TSString* haystack, TSString* needle);
TSString* ts_string_substring(TSString* s, int32_t start, int32_t end);
TSString* ts_string_to_lower(TSString* s);
TSString* ts_string_to_upper(TSString* s);
TSString* ts_string_trim(TSString* s);
int ts_string_starts_with(TSString* s, TSString* prefix);
int ts_string_ends_with(TSString* s, TSString* suffix);
int ts_string_includes(TSString* haystack, TSString* needle);
TSString* ts_string_replace(TSString* s, TSString* search, TSString* replacement);
TSString* ts_string_repeat(TSString* s, int32_t count);
TSArray* ts_string_split(TSString* s, TSString* separator);

/* TSArray */
typedef struct TSArray {
  int32_t refcount;
  int32_t length;
  int32_t capacity;
  Value* items;
} TSArray;

TSArray* ts_array_new(void);
TSArray* ts_array_from_values(Value* values, int32_t count);
void ts_array_push(TSArray* arr, Value val);
Value ts_array_get(TSArray* arr, int32_t index);
void ts_array_set(TSArray* arr, int32_t index, Value val);
int32_t ts_array_index_of(TSArray* arr, Value val);
void ts_array_free(TSArray* arr);
TSArray* ts_array_filter(TSArray* arr, int (*predicate)(Value));
TSArray* ts_array_map(TSArray* arr, Value (*transform)(Value));
TSString* ts_array_join(TSArray* arr, TSString* separator);
int ts_array_some(TSArray* arr, int (*predicate)(Value));
int ts_array_every(TSArray* arr, int (*predicate)(Value));
Value ts_array_find(TSArray* arr, int (*predicate)(Value));
Value ts_array_reduce(TSArray* arr, Value (*reducer)(Value, Value), Value initialValue);
void ts_array_foreach(TSArray* arr, void (*callback)(Value));
void ts_array_splice(TSArray* arr, int32_t start, int32_t deleteCount, Value* items, int32_t itemCount);
Value ts_array_pop(TSArray* arr);

/* TSHashMap */
typedef struct HashEntry {
  TSString* key;
  Value value;
  int occupied;
} HashEntry;

struct TSHashMap {
  int32_t refcount;
  int32_t size;
  int32_t capacity;
  HashEntry* entries;
};

typedef struct TSHashMap TSHashMap;
TSHashMap* ts_hashmap_new(void);
void ts_hashmap_set(TSHashMap* map, TSString* key, Value val);
Value ts_hashmap_get(TSHashMap* map, TSString* key);
int ts_hashmap_has(TSHashMap* map, TSString* key);
TSString* ts_hashmap_to_string(TSHashMap* map);
void ts_hashmap_for_each(TSHashMap* map, void (*callback)(TSString* key, Value value, void* ctx), void* ctx);
int32_t ts_hashmap_count(TSHashMap* map);
void ts_hashmap_free(TSHashMap* map);

/* Closure */
typedef struct Closure {
  void* function_ptr;
  Value* captured_vars;
  int32_t captured_count;
} Closure;

Closure* ts_closure_new(void* fn, Value* captures, int32_t count);
Value ts_closure_call(Closure* closure, Value* args, int32_t arg_count);
void ts_closure_free(Closure* closure);

/* Garbage collector */
void ts_gc_init(void);
void* ts_gc_alloc(size_t size);
void ts_gc_collect(void);

/* Value constructors */
Value ts_value_number(double n);
Value ts_value_string(TSString* s);
Value ts_value_boolean(int b);
Value ts_value_null(void);
Value ts_value_undefined(void);
Value ts_value_array(TSArray* arr);
Value ts_value_object(void* obj);
Value ts_value_function(void* fn);

/* Type coercion */
double ts_to_number(Value val);
TSString* ts_to_string(Value val);
int ts_to_boolean(Value val);
TSString* ts_inspect(Value val);

/* Builtin functions */
void ts_console_log(Value val);
void ts_console_log_multi(Value* args, int argc);
void ts_console_info(Value val);
void ts_console_info_multi(Value* args, int argc);
void ts_console_warn(Value val);
void ts_console_warn_multi(Value* args, int argc);
void ts_console_error(Value val);
void ts_console_error_multi(Value* args, int argc);
void ts_console_debug(Value val);
void ts_console_debug_multi(Value* args, int argc);
void ts_console_assert(Value condition, Value val);
void ts_console_clear(void);
void ts_console_count(TSString* label);
void ts_console_count_reset(TSString* label);
void ts_console_dir(Value val);
void ts_console_group(void);
void ts_console_group_end(void);
void ts_console_table(Value val);
void ts_console_trace(Value val);
void ts_console_time(TSString* label);
void ts_console_time_end(TSString* label);

/* Timers: setTimeout / setInterval (event loop drained at process exit) */
double ts_set_timeout(Value callback, Value delayMs, Value* args, int argc);
double ts_set_interval(Value callback, Value delayMs, Value* args, int argc);
void ts_clear_timeout(Value id);
void ts_clear_interval(Value id);
void ts_timers_run(void);
int ts_timers_pending(void);

/* Browser-like dialogs (stdin/stdout) */
void ts_alert(Value message);
int ts_confirm(Value message);
Value ts_prompt(Value message);

Value ts_typeof(Value val);
void ts_throw(Value val);

/* Error */
Value ts_error_new(TSString* message);

/* Math builtins */
Value ts_math_random(void);
double ts_math_floor(double x);
double ts_math_ceil(double x);
double ts_math_round(double x);
double ts_math_abs(double x);
double ts_math_sqrt(double x);
double ts_math_pow(double base, double exp);
double ts_math_max(double a, double b);
double ts_math_min(double a, double b);
double ts_math_log(double x);
double ts_math_log2(double x);
double ts_math_log10(double x);
double ts_math_sin(double x);
double ts_math_cos(double x);
double ts_math_tan(double x);
double ts_math_asin(double x);
double ts_math_acos(double x);
double ts_math_atan(double x);
double ts_math_atan2(double y, double x);

/* Date */
typedef struct {
  double timestamp; /* milliseconds since epoch */
} Date;

double date_now_ts(void);
double date_parse_ts(TSString* str);
int32_t date_getFullYear_ts(double ts);
int32_t date_getMonth_ts(double ts);
int32_t date_getDate_ts(double ts);
int32_t date_getDay_ts(double ts);
int32_t date_getHours_ts(double ts);
int32_t date_getMinutes_ts(double ts);
int32_t date_getSeconds_ts(double ts);
int32_t date_getMilliseconds_ts(double ts);
double date_getTime_ts(double ts);
TSString* date_toISOString_ts(double ts);
TSString* date_toDateString_ts(double ts);
TSString* date_toTimeString_ts(double ts);
TSString* date_toLocaleString_ts(double ts);

/* Number parsing */
double ts_parse_int(TSString* str, int radix);
double ts_parse_float(TSString* str);

/* Utility */
int ts_is_nan(double x);
int ts_is_finite(double x);

/* Type extraction helpers */
#define TS_EXTRACT_STRING(val) ((val).tag == TAG_STRING ? (val).as.string : ts_to_string(val))
#define TS_EXTRACT_NUMBER(val) ((val).tag == TAG_NUMBER ? (val).as.number : ts_to_number(val))
#define TS_EXTRACT_BOOLEAN(val) ((val).tag == TAG_BOOLEAN ? (val).as.boolean : ts_to_boolean(val))

/* JSON */
Value ts_json_parse(TSString* json);
TSString* ts_json_stringify(Value val);
TSString* ts_json_stringify_indent(Value val, int indent);
int ts_json_is_raw_json(Value val);
Value ts_json_raw_json(TSString* raw);

/* Fetch Request options */
typedef struct {
  TSString* method;
  TSHashMap* headers;
  TSString* body;
} FetchRequest;

/* Fetch Response object */
typedef struct {
  int32_t type_tag;  /* 0x46455443 = 'FETCH' */
  int32_t status;
  TSString* statusText;
  TSString* body;
  TSHashMap* headers;
  TSString* url;
  void* stream;       /* live connection for streaming body, or NULL */
  int body_complete;  /* 1 if body fully buffered in `body` */
} FetchResponse;

#define FETCH_RESPONSE_TAG 0x46455443
#define FETCH_STREAM_TAG   0x5354524D  /* 'STRM' */
#define FETCH_READER_TAG   0x52445252  /* 'RDRR' */

/* Fetch functions */
Value ts_fetch(TSString* url, Value options);
Value ts_fetch_response(Value response);
Value ts_fetch_clone(Value response);
TSString* ts_fetch_text(Value response);
Value ts_fetch_json(Value response);
double ts_fetch_response_status(Value response);
TSString* ts_fetch_response_statusText(Value response);
TSString* ts_fetch_response_url(Value response);
Value ts_fetch_response_headers(Value response);
Value ts_fetch_response_body(Value response);
Value ts_fetch_body_get_reader(Value body);
Value ts_fetch_reader_read(Value reader);

/* Headers constructor */
Value ts_headers(void);
Value ts_headers_from_object(TSHashMap* obj);
void ts_headers_set(Value headers, TSString* key, TSString* value);

/* Blob */
typedef struct {
  int32_t type_tag;  /* 0x424C4F42 = 'BLOB' */
  TSString* data;
  TSString* type;
} Blob;

#define BLOB_TAG 0x424C4F42

Value ts_blob_new(void);
Value ts_blob_from_string(TSString* data, TSString* type);
TSString* ts_blob_text(Value blob);
double ts_blob_size(Value blob);
TSString* ts_blob_type(Value blob);

/* URL */
typedef struct {
  int32_t type_tag;  /* 0x55524C20 = 'URL ' */
  TSString* href;
  TSString* protocol;
  TSString* host;
  TSString* hostname;
  TSString* port;
  TSString* pathname;
  TSString* search;
  TSString* hash;
  TSString* origin;
} Url;

#define URL_TAG 0x55524C20

Value ts_url_new(TSString* urlStr);
TSString* ts_url_href(Value url);
TSString* ts_url_protocol(Value url);
TSString* ts_url_host(Value url);
TSString* ts_url_hostname(Value url);
TSString* ts_url_port(Value url);
TSString* ts_url_pathname(Value url);
TSString* ts_url_search(Value url);
TSString* ts_url_hash(Value url);
TSString* ts_url_toString(Value url);

/* Buffer */
typedef struct {
  int32_t type_tag;  /* 0x42554646 = 'BUFF' */
  uint8_t* data;
  int32_t length;
  int32_t capacity;
} Buffer;

#define BUFFER_TAG 0x42554646

Value ts_buffer_new(int32_t size);
Value ts_buffer_from_string(TSString* str);
Value ts_buffer_from_array(TSArray* arr);
Value ts_buffer_alloc(int32_t size);
Value ts_buffer_allocUnsafe(int32_t size);
Value ts_buffer_concat(Value* buffers, int32_t count);
int32_t ts_buffer_length(Value buf);
uint8_t ts_buffer_readUInt8(Value buf, int32_t offset);
void ts_buffer_writeUInt8(Value buf, int32_t offset, uint8_t value);
Value ts_buffer_slice(Value buf, int32_t start, int32_t end);
TSString* ts_buffer_toString_utf8(Value buf);
TSString* ts_buffer_toString_hex(Value buf);
TSString* ts_buffer_toString_base64(Value buf);
int ts_buffer_isBuffer(Value val);

/* Error handling (setjmp/longjmp) */
typedef struct {
  jmp_buf jump_buffer;
  Value error_value;
} TsErrorContext;

extern TsErrorContext _ts_current_error;

#define TS_TRY if (setjmp(_ts_current_error.jump_buffer) == 0)
#define TS_CATCH else
#define TS_THROW(val) do { _ts_current_error.error_value = val; longjmp(_ts_current_error.jump_buffer, 1); } while(0)

/* ==================== Promise ==================== */
#define PROMISE_TAG 0x50524F4D  /* 'PROM' */

typedef enum {
  PROMISE_PENDING = 0,
  PROMISE_FULFILLED = 1,
  PROMISE_REJECTED = 2
} PromiseState;

typedef struct TSPromise {
  int32_t type_tag;
  int32_t refcount;
  PromiseState state;
  Value result;
  Value onFulfilled;
  Value onRejected;
  Value onFinally;
  struct TSPromise* then_promise; /* promise returned by .then (optional) */
} TSPromise;

Value ts_promise_new(void);
Value ts_promise_resolve(Value p, Value v);
Value ts_promise_reject(Value p, Value err);
Value ts_promise_then(Value p, Value onFulfilled, Value onRejected);
Value ts_promise_catch(Value p, Value onRejected);
Value ts_promise_finally(Value p, Value onFinally);
Value ts_await(Value p);
int ts_value_is_promise(Value v);
Value Promise_constructor(Value executor);

/* ==================== Thread pool / async I/O ==================== */
typedef void (*TsJobFn)(void* userdata);

typedef struct TsJob {
  TsJobFn work;       /* runs on worker thread */
  TsJobFn complete;   /* runs on main thread after work */
  void* userdata;
  struct TsJob* next;
} TsJob;

void ts_thread_pool_init(void);
void ts_thread_pool_submit(TsJob* job);
void ts_thread_pool_shutdown(void);
int  ts_jobs_pending(void);
int  ts_completion_poll(void);   /* process finished jobs on main; returns count */
void ts_completion_wait(int timeout_ms);

/* Unified event loop: timers + async jobs */
void ts_async_run(void);
int  ts_async_pending(void);

#endif /* TS_RUNTIME_H */
