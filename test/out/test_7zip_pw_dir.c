#include "test_7zip_pw_dir.h"

#include "node_7zip.h"
#include "node_fs.h"
#include "node_process.h"

static int __init_done_test_7zip_pw_dir = 0;


void __init_test_7zip_pw_dir(void) {
  if (__init_done_test_7zip_pw_dir) return;
  __init_done_test_7zip_pw_dir = 1;
  node_fs_mkdirSync(ts_value_string(ts_string_new("out_7zpwdir_src")), ts_value_boolean(1));
  node_fs_mkdirSync(ts_value_string(ts_string_new("out_7zpwdir_src/sub")), ts_value_boolean(1));
  node_fs_writeFileSync(ts_value_string(ts_string_new("out_7zpwdir_src/hello.txt")), ts_value_string(ts_string_new("hello world\n")), ts_value_null());
  node_fs_writeFileSync(ts_value_string(ts_string_new("out_7zpwdir_src/sub/nested.txt")), ts_value_string(ts_string_new("nested data\n")), ts_value_null());
  node_7zip_compress(ts_value_string(ts_string_new("out_7zpwdir_src")), ts_value_string(ts_string_new("out_7zpwdir_out/pw.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("password"), ts_value_string(ts_string_new("sekrit")));
  ts_hashmap_set(map, ts_string_new("level"), ts_value_number(6)); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("list:")), ts_value_string(ts_json_stringify_indent(node_7zip_list(ts_value_string(ts_string_new("out_7zpwdir_out/pw.7z")), ts_value_null()), 0))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("hello:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zpwdir_out/pw.7z")), ts_value_string(ts_string_new("hello.txt")), ts_value_string(ts_string_new("sekrit"))), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("nested:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zpwdir_out/pw.7z")), ts_value_string(ts_string_new("sub/nested.txt")), ts_value_string(ts_string_new("sekrit"))), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
}

/* no user main(): empty program entry for main.c */
void test_7zip_pw_dir_entry(void) {}
