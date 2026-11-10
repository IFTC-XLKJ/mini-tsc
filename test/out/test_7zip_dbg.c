#include "test_7zip_dbg.h"

#include "node_7zip.h"
#include "node_fs.h"
#include "node_process.h"

static int __init_done_test_7zip_dbg = 0;

void test_7zip_dbg_entry();

void test_7zip_dbg_entry() {
  puts("STEP 0: starting");
  node_fs_mkdirSync(ts_value_string(ts_string_new("out_7zdbg_src")), ts_value_boolean(1));
  puts("STEP 1: dir made");
  node_fs_writeFileSync(ts_value_string(ts_string_new("out_7zdbg_src/hello.txt")), ts_value_string(ts_string_new("hello world\n")), ts_value_null());
  puts("STEP 2: file written");
  node_7zip_compress(ts_value_string(ts_string_new("out_7zdbg_src/hello.txt")), ts_value_string(ts_string_new("out_7zdbg_out/one.7z")), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("STEP 3: single-file compress done |")), node_7zip_isArchive(ts_value_string(ts_string_new("out_7zdbg_out/one.7z"))), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("STEP 4: read:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zdbg_out/one.7z")), ts_value_string(ts_string_new("hello.txt")), ts_value_null())}, 2);
  puts("STEP 5: dir compress default opts");
  node_7zip_compress(ts_value_string(ts_string_new("out_7zdbg_src")), ts_value_string(ts_string_new("out_7zdbg_out/dir.7z")), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("STEP 6: dir.7z read:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zdbg_out/dir.7z")), ts_value_string(ts_string_new("hello.txt")), ts_value_null()), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  puts("STEP 7: dir compress with opts threads=2");
  node_7zip_compress(ts_value_string(ts_string_new("out_7zdbg_src")), ts_value_string(ts_string_new("out_7zdbg_out/dir2.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("level"), ts_value_number(6));
  ts_hashmap_set(map, ts_string_new("threads"), ts_value_number(2)); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("STEP 8: dir2.7z read:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zdbg_out/dir2.7z")), ts_value_string(ts_string_new("hello.txt")), ts_value_null()), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  puts("7zip debug done");
}

void __init_test_7zip_dbg(void) {
  if (__init_done_test_7zip_dbg) return;
  __init_done_test_7zip_dbg = 1;
}
