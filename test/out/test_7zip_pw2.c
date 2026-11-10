#include "test_7zip_pw2.h"

#include "node_7zip.h"
#include "node_fs.h"
#include "node_process.h"

static int __init_done_test_7zip_pw2 = 0;

void test_7zip_pw2_entry();

void test_7zip_pw2_entry() {
  node_fs_mkdirSync(ts_value_string(ts_string_new("out_7zpw_src")), ts_value_boolean(1));
  node_fs_writeFileSync(ts_value_string(ts_string_new("out_7zpw_src/hello.txt")), ts_value_string(ts_string_new("hello world\n")), ts_value_null());
  puts("STEP 1: compress with password");
  node_7zip_compress(ts_value_string(ts_string_new("out_7zpw_src")), ts_value_string(ts_string_new("out_7zpw_out/pw.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("password"), ts_value_string(ts_string_new("sekrit")));
  ts_hashmap_set(map, ts_string_new("level"), ts_value_number(6)); map; })));
  puts("STEP 2: compress done");
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("STEP 3: isArchive:")), node_7zip_isArchive(ts_value_string(ts_string_new("out_7zpw_out/pw.7z"))), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  puts("pw2 done");
}

void __init_test_7zip_pw2(void) {
  if (__init_done_test_7zip_pw2) return;
  __init_done_test_7zip_pw2 = 1;
}
