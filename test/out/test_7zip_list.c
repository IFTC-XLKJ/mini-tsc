#include "test_7zip_list.h"

#include "node_7zip.h"
#include "node_process.h"

static int __init_done_test_7zip_list = 0;


void __init_test_7zip_list(void) {
  if (__init_done_test_7zip_list) return;
  __init_done_test_7zip_list = 1;
  ts_console_log(ts_value_string(ts_json_stringify_indent(node_7zip_list(ts_value_string(ts_string_new("out_7zcomp_out/dir.7z")), ts_value_null()), 0)));
}

/* no user main(): empty program entry for main.c */
void test_7zip_list_entry(void) {}
