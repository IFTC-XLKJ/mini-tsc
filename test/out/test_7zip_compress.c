#include "test_7zip_compress.h"

#include "node_7zip.h"
#include "node_fs.h"
#include "node_process.h"

static int __init_done_test_7zip_compress = 0;

void test_7zip_compress_entry();

void test_7zip_compress_entry() {
  node_fs_mkdirSync(ts_value_string(ts_string_new("out_7zcomp_src")), ts_value_boolean(1));
  node_fs_mkdirSync(ts_value_string(ts_string_new("out_7zcomp_src/sub")), ts_value_boolean(1));
  node_fs_writeFileSync(ts_value_string(ts_string_new("out_7zcomp_src/hello.txt")), ts_value_string(ts_string_new("hello world\n")), ts_value_null());
  node_fs_writeFileSync(ts_value_string(ts_string_new("out_7zcomp_src/sub/nested.txt")), ts_value_string(ts_string_new("nested data\n")), ts_value_null());
  node_fs_writeFileSync(ts_value_string(ts_string_new("out_7zcomp_src/data.json")), ts_value_string(ts_string_new("{\"a\":1,\"b\":2,\"c\":[1,2,3]}")), ts_value_null());
  node_7zip_compress(ts_value_string(ts_string_new("out_7zcomp_src/hello.txt")), ts_value_string(ts_string_new("out_7zcomp_out/one.7z")), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("one.7z isArchive:")), node_7zip_isArchive(ts_value_string(ts_string_new("out_7zcomp_out/one.7z"))), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("one.7z hello.txt:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/one.7z")), ts_value_string(ts_string_new("hello.txt")), ts_value_null())}, 2);
  node_7zip_compress(ts_value_string(ts_string_new("out_7zcomp_src")), ts_value_string(ts_string_new("out_7zcomp_out/dir.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("format"), ts_value_string(ts_string_new("7z")));
  ts_hashmap_set(map, ts_string_new("method"), ts_value_string(ts_string_new("lzma2")));
  ts_hashmap_set(map, ts_string_new("level"), ts_value_number(9));
  ts_hashmap_set(map, ts_string_new("dictSize"), ts_value_number(1048576));
  ts_hashmap_set(map, ts_string_new("wordSize"), ts_value_number(3));
  ts_hashmap_set(map, ts_string_new("threads"), ts_value_number(2));
  ts_hashmap_set(map, ts_string_new("solidSize"), ts_value_number(0)); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("dir.7z isArchive:")), node_7zip_isArchive(ts_value_string(ts_string_new("out_7zcomp_out/dir.7z"))), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("dir.7z hello.txt:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/dir.7z")), ts_value_string(ts_string_new("hello.txt")), ts_value_null())}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("dir.7z sub/nested.txt:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/dir.7z")), ts_value_string(ts_string_new("sub/nested.txt")), ts_value_null())}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("dir.7z data.json:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/dir.7z")), ts_value_string(ts_string_new("data.json")), ts_value_null())}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("extracted count:")), node_7zip_extractToDir(ts_value_string(ts_string_new("out_7zcomp_out/dir.7z")), ts_value_string(ts_string_new("out_7zcomp_extracted")), ts_value_null()), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("extracted hello:")), node_fs_readFileSync(ts_value_string(ts_string_new("out_7zcomp_extracted/hello.txt")), ts_value_string(ts_string_new("utf8")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("extracted nested:")), node_fs_readFileSync(ts_value_string(ts_string_new("out_7zcomp_extracted/sub/nested.txt")), ts_value_string(ts_string_new("utf8")))}, 2);
  node_7zip_compress(ts_value_string(ts_string_new("out_7zcomp_src")), ts_value_string(ts_string_new("out_7zcomp_out/copy.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("method"), ts_value_string(ts_string_new("copy"))); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("copy.7z data.json:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/copy.7z")), ts_value_string(ts_string_new("data.json")), ts_value_null()), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  node_7zip_compress(ts_value_string(ts_string_new("out_7zcomp_src")), ts_value_string(ts_string_new("out_7zcomp_out/pw.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("password"), ts_value_string(ts_string_new("sekrit")));
  ts_hashmap_set(map, ts_string_new("level"), ts_value_number(6)); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("pw.7z isArchive:")), node_7zip_isArchive(ts_value_string(ts_string_new("out_7zcomp_out/pw.7z"))), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("pw.7z no-password read:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/pw.7z")), ts_value_string(ts_string_new("hello.txt")), ts_value_null())}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("pw.7z wrong-password read:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/pw.7z")), ts_value_string(ts_string_new("hello.txt")), ts_value_string(ts_string_new("wrong")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("pw.7z correct-password read:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/pw.7z")), ts_value_string(ts_string_new("hello.txt")), ts_value_string(ts_string_new("sekrit")))}, 2);
  node_7zip_compress(ts_value_string(ts_string_new("out_7zcomp_src")), ts_value_string(ts_string_new("out_7zcomp_out/vol.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("volumeSize"), ts_value_number(64));
  ts_hashmap_set(map, ts_string_new("level"), ts_value_number(1)); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("vol.7z base exists:")), ts_value_boolean(node_fs_existsSync(ts_value_string(ts_string_new("out_7zcomp_out/vol.7z"))))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("vol.7z.001 exists:")), ts_value_boolean(node_fs_existsSync(ts_value_string(ts_string_new("out_7zcomp_out/vol.7z.001"))))}, 2);
  node_7zip_compress(ts_value_string(ts_string_new("out_7zcomp_src")), ts_value_string(ts_string_new("out_7zcomp_out/l0.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("level"), ts_value_number(0));
  ts_hashmap_set(map, ts_string_new("dictSize"), ts_value_number(262144)); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("level 0 data.json:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/l0.7z")), ts_value_string(ts_string_new("data.json")), ts_value_null()), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  node_7zip_compress(ts_value_string(ts_string_new("out_7zcomp_src")), ts_value_string(ts_string_new("out_7zcomp_out/l3.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("level"), ts_value_number(3));
  ts_hashmap_set(map, ts_string_new("dictSize"), ts_value_number(262144)); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("level 3 data.json:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/l3.7z")), ts_value_string(ts_string_new("data.json")), ts_value_null()), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  node_7zip_compress(ts_value_string(ts_string_new("out_7zcomp_src")), ts_value_string(ts_string_new("out_7zcomp_out/l5.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("level"), ts_value_number(5));
  ts_hashmap_set(map, ts_string_new("dictSize"), ts_value_number(262144)); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("level 5 data.json:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/l5.7z")), ts_value_string(ts_string_new("data.json")), ts_value_null()), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  node_7zip_compress(ts_value_string(ts_string_new("out_7zcomp_src")), ts_value_string(ts_string_new("out_7zcomp_out/mt.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("level"), ts_value_number(6));
  ts_hashmap_set(map, ts_string_new("threads"), ts_value_number(4));
  ts_hashmap_set(map, ts_string_new("solidSize"), ts_value_number(65536)); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("mt data.json:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/mt.7z")), ts_value_string(ts_string_new("data.json")), ts_value_null()), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  node_7zip_compress(ts_value_string(ts_string_new("out_7zcomp_src")), ts_value_string(ts_string_new("out_7zcomp_out/ppmd.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("method"), ts_value_string(ts_string_new("ppmd")));
  ts_hashmap_set(map, ts_string_new("level"), ts_value_number(6)); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("ppmd.7z isArchive:")), node_7zip_isArchive(ts_value_string(ts_string_new("out_7zcomp_out/ppmd.7z"))), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("ppmd data.json:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/ppmd.7z")), ts_value_string(ts_string_new("data.json")), ts_value_null()), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("ppmd hello.txt:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/ppmd.7z")), ts_value_string(ts_string_new("hello.txt")), ts_value_null()), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  node_7zip_compress(ts_value_string(ts_string_new("out_7zcomp_src")), ts_value_string(ts_string_new("out_7zcomp_out/ppmd_pw.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("method"), ts_value_string(ts_string_new("ppmd")));
  ts_hashmap_set(map, ts_string_new("level"), ts_value_number(4));
  ts_hashmap_set(map, ts_string_new("password"), ts_value_string(ts_string_new("ppmd-secret"))); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("ppmd_pw correct:")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/ppmd_pw.7z")), ts_value_string(ts_string_new("data.json")), ts_value_string(ts_string_new("ppmd-secret"))), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("ppmd_pw wrong:  ")), node_7zip_readFile(ts_value_string(ts_string_new("out_7zcomp_out/ppmd_pw.7z")), ts_value_string(ts_string_new("data.json")), ts_value_string(ts_string_new("bad")))}, 2);
  puts("7zip compress test done");
}

void __init_test_7zip_compress(void) {
  if (__init_done_test_7zip_compress) return;
  __init_done_test_7zip_compress = 1;
}
