#include "test_7zip_iso.h"

#include "node_7zip.h"
#include "node_fs.h"
#include "node_process.h"

static int __init_done_test_7zip_iso = 0;

void test_7zip_iso_entry();

void test_7zip_iso_entry() {
  node_fs_mkdirSync(ts_value_string(ts_string_new("out_7ziso_src")), ts_value_boolean(1));
  node_fs_writeFileSync(ts_value_string(ts_string_new("out_7ziso_src/a.txt")), ts_value_string(ts_string_new("AAAA")), ts_value_null());
  node_fs_writeFileSync(ts_value_string(ts_string_new("out_7ziso_src/b.txt")), ts_value_string(ts_string_new("BBBB")), ts_value_null());
  node_7zip_compress(ts_value_string(ts_string_new("out_7ziso_src/a.txt")), ts_value_string(ts_string_new("out_7ziso_out/copy_a.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("method"), ts_value_string(ts_string_new("copy"))); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("copy a.7z isArchive:")), node_7zip_isArchive(ts_value_string(ts_string_new("out_7ziso_out/copy_a.7z"))), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("copy a read:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7ziso_out/copy_a.7z")), ts_value_string(ts_string_new("a.txt")), ts_value_null()), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  node_7zip_compress(ts_value_string(ts_string_new("out_7ziso_src")), ts_value_string(ts_string_new("out_7ziso_out/copy_dir.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("method"), ts_value_string(ts_string_new("copy"))); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("copy dir isArchive:")), node_7zip_isArchive(ts_value_string(ts_string_new("out_7ziso_out/copy_dir.7z"))), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("copy dir a:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7ziso_out/copy_dir.7z")), ts_value_string(ts_string_new("a.txt")), ts_value_null()), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("copy dir b:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7ziso_out/copy_dir.7z")), ts_value_string(ts_string_new("b.txt")), ts_value_null()), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  node_7zip_compress(ts_value_string(ts_string_new("out_7ziso_src/a.txt")), ts_value_string(ts_string_new("out_7ziso_out/lz_a.7z")), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("lz a read:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7ziso_out/lz_a.7z")), ts_value_string(ts_string_new("a.txt")), ts_value_null()), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  puts("iso done");
}

void __init_test_7zip_iso(void) {
  if (__init_done_test_7zip_iso) return;
  __init_done_test_7zip_iso = 1;
}
