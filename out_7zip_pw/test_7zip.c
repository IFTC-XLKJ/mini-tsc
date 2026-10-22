#include "test_7zip.h"

#include "node_7zip.h"
#include "node_fs.h"
#include "node_process.h"

static int __init_done_test_7zip = 0;

void test_7zip_entry();

void test_7zip_entry() {
  TSString* archive = ts_string_new("test/fixtures/sample.7z");
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("isArchive(7z):")), node_7zip_isArchive(ts_value_string(archive))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("isArchive(ts):")), node_7zip_isArchive(ts_value_string(ts_string_new("test/7zip.ts")))}, 2);
  Value entries = node_7zip_list(ts_value_string(archive), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("count:")), ts_value_number((double)(((TSArray*)entries.as.object)->length))}, 2);
  for (double i = 0; i < ((TSArray*)entries.as.object)->length; i++) {
  Value e = ts_array_get(((TSArray*)entries.as.object), (int32_t)(i));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("  entry:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)e.as.object), ts_string_new("name")))), ts_value_string(ts_string_new("size:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)e.as.object), ts_string_new("size")))), ts_value_string(ts_string_new("isDir:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)e.as.object), ts_string_new("isDir"))))}, 6);
  ts_gc_maybe_collect();
}
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("hello:")), node_7zip_readFile(ts_value_string(archive), ts_value_string(ts_string_new("hello.txt")), ts_value_null())}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("data:")), node_7zip_readFile(ts_value_string(archive), ts_value_string(ts_string_new("data.json")), ts_value_null())}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("nested:")), node_7zip_readFile(ts_value_string(archive), ts_value_string(ts_string_new("sub/nested.txt")), ts_value_null())}, 2);
  Value missing = node_7zip_readFile(ts_value_string(archive), ts_value_string(ts_string_new("does-not-exist.txt")), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("missing is null:")), ts_value_boolean(!ts_to_boolean(missing))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("lastError:")), node_7zip_lastError()}, 2);
  Value handle = node_7zip_open(ts_value_string(archive), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("handle count:")), node_7zip_handleCount(handle)}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("has hello.txt:")), node_7zip_handleHas(handle, ts_value_string(ts_string_new("hello.txt")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("has missing.txt:")), node_7zip_handleHas(handle, ts_value_string(ts_string_new("missing.txt")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("handle read:")), node_7zip_handleRead(handle, ts_value_string(ts_string_new("hello.txt")))}, 2);
  node_7zip_close(handle);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("hasEntry(hello):")), node_7zip_hasEntry(ts_value_string(archive), ts_value_string(ts_string_new("hello.txt")), ts_value_null())}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("hasEntry(nope):")), node_7zip_hasEntry(ts_value_string(archive), ts_value_string(ts_string_new("nope.txt")), ts_value_null())}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("size(hello):")), node_7zip_size(ts_value_string(archive), ts_value_string(ts_string_new("hello.txt")), ts_value_null())}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("size(nope):")), node_7zip_size(ts_value_string(archive), ts_value_string(ts_string_new("nope.txt")), ts_value_null())}, 2);
  Value one = node_7zip_getEntry(ts_value_string(archive), ts_value_string(ts_string_new("data.json")), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("getEntry size:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)one.as.object), ts_string_new("size")))), ts_value_string(ts_string_new("isDir:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)one.as.object), ts_string_new("isDir")))), ts_value_string(ts_string_new("attrib:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)one.as.object), ts_string_new("attrib"))))}, 6);
  Value names = node_7zip_entryNames(ts_value_string(archive), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("names count:")), ts_value_number((double)(((TSArray*)names.as.object)->length)), ts_value_string(ts_string_new("first:")), ts_value_string(ts_to_string(ts_array_get(((TSArray*)names.as.object), (int32_t)(0))))}, 4);
  Value info = node_7zip_info(ts_value_string(archive), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("info count:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)info.as.object), ts_string_new("count")))), ts_value_string(ts_string_new("files:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)info.as.object), ts_string_new("fileCount")))), ts_value_string(ts_string_new("dirs:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)info.as.object), ts_string_new("dirCount")))), ts_value_string(ts_string_new("total:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)info.as.object), ts_string_new("totalSize"))))}, 8);
  Value buf = node_7zip_readFileBuffer(ts_value_string(archive), ts_value_string(ts_string_new("hello.txt")), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("buffer length:")), ts_value_number((double)(ts_buffer_length(buf)))}, 2);
  Value h2 = node_7zip_open(ts_value_string(archive), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("h2 isOpen:")), node_7zip_handleIsOpen(h2)}, 2);
  Value h2names = node_7zip_handleNames(h2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("h2 names:")), ts_value_number((double)(((TSArray*)h2names.as.object)->length))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("h2 size(hello):")), node_7zip_handleSize(h2, ts_value_string(ts_string_new("hello.txt")))}, 2);
  Value hi = node_7zip_handleInfo(h2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("h2 info files:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)hi.as.object), ts_string_new("fileCount"))))}, 2);
  Value h2buf = node_7zip_handleReadBuffer(h2, ts_value_string(ts_string_new("hello.txt")));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("h2 readBuffer len:")), ts_value_number((double)(ts_buffer_length(h2buf)))}, 2);
  node_7zip_close(h2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("h2 isOpen after close:")), node_7zip_handleIsOpen(h2)}, 2);
  node_7zip_close(h2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("h2 count after close:")), node_7zip_handleCount(h2)}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("h2 lastError:")), node_7zip_lastError()}, 2);
  Value extracted = node_7zip_extractToDir(ts_value_string(archive), ts_value_string(ts_string_new("out_7zip_test")), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("extracted file count:")), extracted}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("extracted hello:")), node_fs_readFileSync(ts_value_string(ts_string_new("out_7zip_test/hello.txt")), ts_value_string(ts_string_new("utf8")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("extracted nested:")), node_fs_readFileSync(ts_value_string(ts_string_new("out_7zip_test/sub/nested.txt")), ts_value_string(ts_string_new("utf8")))}, 2);
  Value subCount = node_7zip_extractMatching(ts_value_string(archive), ts_value_string(ts_string_new("sub")), ts_value_string(ts_string_new("out_7zip_test_sub")), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("subtree extracted count:")), subCount}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("subtree nested:")), node_fs_readFileSync(ts_value_string(ts_string_new("out_7zip_test_sub/sub/nested.txt")), ts_value_string(ts_string_new("utf8")))}, 2);
  node_7zip_extractFile(ts_value_string(archive), ts_value_string(ts_string_new("data.json")), ts_value_string(ts_string_new("out_7zip_test/one.json")), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("one.json:")), node_fs_readFileSync(ts_value_string(ts_string_new("out_7zip_test/one.json")), ts_value_string(ts_string_new("utf8")))}, 2);
  Value h3 = node_7zip_open(ts_value_string(archive), ts_value_null());
  Value h3count = node_7zip_handleExtractMatching(h3, ts_value_string(ts_string_new("sub")), ts_value_string(ts_string_new("out_7zip_test_sub2")));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("handle subtree extracted count:")), h3count}, 2);
  node_7zip_close(h3);
  Value none = node_7zip_extractMatching(ts_value_string(archive), ts_value_string(ts_string_new("nope")), ts_value_string(ts_string_new("out_7zip_test_none")), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("non-matching prefix count:")), none}, 2);
  Value deep = node_7zip_extractToDir(ts_value_string(archive), ts_value_string(ts_string_new("out_7zip_test_deep/a/b")), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("deep dir count:")), deep}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("deep hello:")), node_fs_readFileSync(ts_value_string(ts_string_new("out_7zip_test_deep/a/b/hello.txt")), ts_value_string(ts_string_new("utf8")))}, 2);
  Value bad = node_7zip_extractToDir(ts_value_string(ts_string_new("test/fixtures/does-not-exist.7z")), ts_value_string(ts_string_new("out_7zip_test_bad")), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("bad archive is null:")), ts_value_boolean(!ts_to_boolean(bad)), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  puts("7zip smoke test done");
}

void __init_test_7zip(void) {
  if (__init_done_test_7zip) return;
  __init_done_test_7zip = 1;
}
