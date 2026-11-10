#include "test_7zip_vol.h"

#include "node_7zip.h"
#include "node_fs.h"
#include "node_process.h"

static int __init_done_test_7zip_vol = 0;

void test_7zip_vol_entry();

void test_7zip_vol_entry() {
  node_fs_mkdirSync(ts_value_string(ts_string_new("out_7zvol_src")), ts_value_boolean(1));
  node_fs_mkdirSync(ts_value_string(ts_string_new("out_7zvol_out")), ts_value_boolean(1));
  Value big = ts_buffer_alloc(1000);
  for (double i = 0; i < 1000; i++) {
  ts_buffer_writeUInt8(big, (int32_t)(i), (uint8_t)(i));
  ts_gc_maybe_collect();
}
  node_fs_writeFileSync(ts_value_string(ts_string_new("out_7zvol_src/big.bin")), big, ts_value_null());
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("wrote big.bin, size:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)node_fs_statSync(ts_value_string(ts_string_new("out_7zvol_src/big.bin"))).as.object), ts_string_new("size"))))}, 2);
  node_7zip_compress(ts_value_string(ts_string_new("out_7zvol_src/big.bin")), ts_value_string(ts_string_new("out_7zvol_out/vol.7z")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("volumeSize"), ts_value_number(64));
  ts_hashmap_set(map, ts_string_new("level"), ts_value_number(1)); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("vol.7z exists:")), ts_value_boolean(node_fs_existsSync(ts_value_string(ts_string_new("out_7zvol_out/vol.7z"))))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("vol.7z.001 exists:")), ts_value_boolean(node_fs_existsSync(ts_value_string(ts_string_new("out_7zvol_out/vol.7z.001"))))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("vol.7z.002 exists:")), ts_value_boolean(node_fs_existsSync(ts_value_string(ts_string_new("out_7zvol_out/vol.7z.002"))))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("vol.7z.003 exists:")), ts_value_boolean(node_fs_existsSync(ts_value_string(ts_string_new("out_7zvol_out/vol.7z.003"))))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("lastError:")), node_7zip_lastError()}, 2);
}

void __init_test_7zip_vol(void) {
  if (__init_done_test_7zip_vol) return;
  __init_done_test_7zip_vol = 1;
}
