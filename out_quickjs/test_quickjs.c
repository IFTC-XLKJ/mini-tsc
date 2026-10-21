#include "test_quickjs.h"

#include "node_quickjs.h"
#include "node_process.h"

static int __init_done_test_quickjs = 0;

void test_quickjs_entry();

void test_quickjs_entry() {
  Value ctx = node_quickjs_createContext();
  ts_console_log(node_quickjs_eval(ctx, ts_value_string(ts_string_new("1 + 2 * 3"))));
  ts_console_log(node_quickjs_eval(ctx, ts_value_string(ts_string_new("'hello ' + 'world'"))));
  Value arr = node_quickjs_eval(ctx, ts_value_string(ts_string_new("[1, 2, 3, 4]")));
  ts_console_log(ts_value_number((double)(((TSArray*)arr.as.object)->length)));
  ts_console_log(ts_value_string(ts_to_string(ts_array_get(((TSArray*)arr.as.object), (int32_t)(0)))));
  ts_console_log(ts_value_string(ts_to_string(ts_array_get(((TSArray*)arr.as.object), (int32_t)(3)))));
  Value obj = node_quickjs_eval(ctx, ts_value_string(ts_string_new("({ name: 'Alice', age: 30 })")));
  ts_console_log(ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)obj.as.object), ts_string_new("name")))));
  ts_console_log(ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)obj.as.object), ts_string_new("age")))));
  node_quickjs_setGlobal(ctx, ts_value_string(ts_string_new("answer")), ts_value_number(42));
  ts_console_log(node_quickjs_eval(ctx, ts_value_string(ts_string_new("answer * 2"))));
  node_quickjs_eval(ctx, ts_value_string(ts_string_new("function double(x) { return x * 2; }")));
  ts_console_log(node_quickjs_eval(ctx, ts_value_string(ts_string_new("double(21)"))));
  node_quickjs_eval(ctx, ts_value_string(ts_string_new("var greeting = 'hi from js';")));
  ts_console_log(node_quickjs_getGlobal(ctx, ts_value_string(ts_string_new("greeting"))));
  TS_TRY {
  node_quickjs_eval(ctx, ts_value_string(ts_string_new("throw new Error('boom');")));
} TS_CATCH {
  Value e = _ts_current_error.error_value;
{
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("caught:")), ts_value_string(ts_to_string(({ Value __em = e; (__em.tag == TAG_STRING) ? __em : ((__em.tag == TAG_OBJECT && __em.as.object) ? ts_hashmap_get((TSHashMap*)__em.as.object, ts_string_new("message")) : ts_value_string(ts_string_new(""))); })))}, 2);
}
}
  node_quickjs_dispose(ctx);
  puts("quickjs tests passed!");
}

void __init_test_quickjs(void) {
  if (__init_done_test_quickjs) return;
  __init_done_test_quickjs = 1;
}
