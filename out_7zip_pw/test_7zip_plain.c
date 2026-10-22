#include "test_7zip_plain.h"

#include "node_7zip.h"
#include "node_process.h"

static int __init_done_test_7zip_plain = 0;

void test_7zip_plain_entry();

void test_7zip_plain_entry() {
  TSString* a = ts_string_new("test/fixtures/plain_copy.7z");
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("isArchive:")), node_7zip_isArchive(ts_value_string(a))}, 2);
  Value entries = node_7zip_list(ts_value_string(a), ts_value_null());
  if (!ts_to_boolean(entries)) {
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("list: null |")), node_7zip_lastError()}, 2);
} else {
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("list count:")), ts_value_number((double)(((TSArray*)entries.as.object)->length))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("name:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)ts_array_get(((TSArray*)entries.as.object), (int32_t)(0)).as.object), ts_string_new("name")))), ts_value_string(ts_string_new("size:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)ts_array_get(((TSArray*)entries.as.object), (int32_t)(0)).as.object), ts_string_new("size"))))}, 4);
}
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("read:")), node_7zip_readFile(ts_value_string(a), ts_value_string(ts_string_new("plain.txt")), ts_value_null()), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
}

void __init_test_7zip_plain(void) {
  if (__init_done_test_7zip_plain) return;
  __init_done_test_7zip_plain = 1;
}
