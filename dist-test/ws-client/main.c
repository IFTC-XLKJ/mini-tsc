#include "runtime.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "test_websocket.h"

extern void __init_test_websocket(void);

/* process.argv capture (Unix/Android); no-op storage on Windows */
extern void node_process_set_argv(int argc, char** argv);

/* CommonJS module globals for entry: E:/project/mini-tsc/test/websocket.ts */
const char* __ts_dirname = "E:/project/mini-tsc/test";
const char* __ts_filename = "E:/project/mini-tsc/test/websocket.ts";

int main(int argc, char* argv[]) {
  node_process_set_argv(argc, argv);
  /* GC: stack bottom for conservative mark + init */
  ts_gc_init();
  ts_gc_set_stack_bottom((void*)&argc);
  /* Initialize modules */
  __init_test_websocket();

  /* Run entry point */
  test_websocket_entry();

  /* Event loop: drain async I/O completions + timers + websockets */
  while (ts_websocket_pending()
  ) {
    ts_websocket_poll();
#ifdef _WIN32
    Sleep(10);
#else
    usleep(10000);
#endif
  }
  ts_gc_maybe_collect_idle();

  return 0;
}