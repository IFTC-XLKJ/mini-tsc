#include "test_7zip_pw3.h"

#include "node_7zip.h"
#include "node_fs.h"
#include "node_process.h"

static int __init_done_test_7zip_pw3 = 0;

void test_7zip_pw3_entry();

void test_7zip_pw3_entry() {
  node_fs_mkdirSync(ts_value_string(ts_string_new("out_7zpw_src")), ts_value_boolean(1));
  node_fs_writeFileSync(ts_value_string(ts_string_new("out_7zpw_src/hello.txt")), ts_value_string(ts_string_new("hello world\n")), ts_value_null());
  puts("STEP 1: compress with password");
  node_7zip_compress(ts_value_string(ts_string_new("out_7zpw_src")), ts_value_string(ts_string_new("out_7zpw_out/pw.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("password"), ts_value_string(ts_string_new("sekrit")));
  ts_hashmap_set(map, ts_string_new("level"), ts_value_number(6)); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("STEP 2: isArchive:")), node_7zip_isArchive(ts_value_string(ts_string_new("out_7zpw_out/pw.7z"))), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("STEP 3: list:")), ts_value_string(ts_json_stringify_indent(node_7zip_list(ts_value_string(ts_string_new("out_7zpw_out/pw.7z")), ts_value_null()), 0)), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("STEP 4: correct-password read:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zpw_out/pw.7z")), ts_value_string(ts_string_new("hello.txt")), ts_value_string(ts_string_new("sekrit"))), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  puts("pw3 done");
}

void __init_test_7zip_pw3(void) {
  if (__init_done_test_7zip_pw3) return;
  __init_done_test_7zip_pw3 = 1;
}
