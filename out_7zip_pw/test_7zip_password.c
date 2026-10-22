#include "test_7zip_password.h"

#include "node_7zip.h"
#include "node_process.h"

static int __init_done_test_7zip_password = 0;

void test_7zip_password_entry();

void test_7zip_password_entry() {
  TSString* archive = ts_string_new("test/fixtures/encrypted.7z");
  TSString* password = ts_string_new("s3cret-pw");
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("isArchive:")), node_7zip_isArchive(ts_value_string(archive))}, 2);
  Value entries = node_7zip_list(ts_value_string(archive), ts_value_string(password));
  if (!ts_to_boolean(entries)) {
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("list failed |")), node_7zip_lastError()}, 2);
} else {
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("list count:")), ts_value_number((double)(((TSArray*)entries.as.object)->length)), ts_value_string(ts_string_new("name:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)ts_array_get(((TSArray*)entries.as.object), (int32_t)(0)).as.object), ts_string_new("name"))))}, 4);
}
  Value noPw = node_7zip_readFile(ts_value_string(archive), ts_value_string(ts_string_new("secret.txt")), ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("no-password read is null:")), ts_value_boolean(!ts_to_boolean(noPw)), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  Value wrong = node_7zip_readFile(ts_value_string(archive), ts_value_string(ts_string_new("secret.txt")), ts_value_string(ts_string_new("wrong-pw")));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("wrong-password read is null:")), ts_value_boolean(!ts_to_boolean(wrong)), ts_value_string(ts_string_new("|")), node_7zip_lastError()}, 4);
  Value plain = node_7zip_readFile(ts_value_string(archive), ts_value_string(ts_string_new("secret.txt")), ts_value_string(password));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("decrypted:")), plain}, 2);
  Value h = node_7zip_open(ts_value_string(archive), ts_value_string(password));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("handle count:")), node_7zip_handleCount(h)}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("handle read:")), node_7zip_handleRead(h, ts_value_string(ts_string_new("secret.txt")))}, 2);
  node_7zip_close(h);
  Value n = node_7zip_extractToDir(ts_value_string(archive), ts_value_string(ts_string_new("out_7zip_enc")), ts_value_string(password));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("extracted count:")), n}, 2);
  puts("7zip password test done");
}

void __init_test_7zip_password(void) {
  if (__init_done_test_7zip_password) return;
  __init_done_test_7zip_password = 1;
}
