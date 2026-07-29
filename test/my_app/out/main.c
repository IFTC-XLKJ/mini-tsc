#include "runtime.h"

#include "test_my_app_main.h"

extern void __init_test_my_app_main(void);

/* process.argv capture (Unix/Android); no-op storage on Windows */
extern void node_process_set_argv(int argc, char** argv);

/* CommonJS module globals for entry: E:/project/mini-tsc/test/my_app/main.ts */
const char* __ts_dirname = "E:/project/mini-tsc/test/my_app";
const char* __ts_filename = "E:/project/mini-tsc/test/my_app/main.ts";

int main(int argc, char* argv[]) {
  node_process_set_argv(argc, argv);
  /* GC: stack bottom for conservative mark + init */
  ts_gc_init();
  ts_gc_set_stack_bottom((void*)&argc);
  /* Initialize modules */
  __init_test_my_app_main();

  /* Run entry point */
  test_my_app_main_entry();

  /* Final opportunistic GC before exit */
  ts_gc_maybe_collect_idle();

  return 0;
}