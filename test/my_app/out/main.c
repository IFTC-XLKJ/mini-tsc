#include "runtime.h"

#include "test_my_app_main.h"

extern void __init_test_my_app_main(void);

/* process.argv capture (Unix/Android); no-op storage on Windows */
extern void node_process_set_argv(int argc, char** argv);

/* CommonJS module globals (runtime-resolved exe paths) */
char* __ts_dirname;
char* __ts_filename;
#ifdef _WIN32
#include <windows.h>
#endif
static void __ts_init_paths(void) {
#ifdef _WIN32
  wchar_t wbuf[MAX_PATH];
  DWORD n = GetModuleFileNameW(NULL, wbuf, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    static char buf[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, MAX_PATH, NULL, NULL);
    __ts_filename = strdup(buf);
    { char* p = buf; char* last = NULL; while (*p) { if (*p == '\\' || *p == '/') last = p; p++; }
      if (last) *last = '\0'; }
    __ts_dirname = strdup(buf);
  } else { __ts_dirname = strdup("."); __ts_filename = strdup(""); }
#else
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = '\0';
    __ts_filename = strdup(buf);
    { char* p = buf; char* last = NULL; while (*p) { if (*p == '/' || *p == '\\') last = p; p++; }
      if (last) *last = '\0'; }
    __ts_dirname = strdup(buf);
  } else { __ts_dirname = strdup("."); __ts_filename = strdup(""); }
#endif
}

int main(int argc, char* argv[]) {
  node_process_set_argv(argc, argv);
  __ts_init_paths();
  /* GC: stack bottom for conservative mark + init */
  ts_gc_init();
  ts_gc_set_stack_bottom((void*)&argc);
  /* Initialize modules */
  __init_test_my_app_main();

  /* Run entry point */
  test_my_app_main_entry();

  /* Event loop: drain async I/O + timers + websockets + workers + HTTP */
  ts_async_run();

  return 0;
}