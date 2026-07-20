#include "node_process.h"
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define getcwd_fn _getcwd
#define chdir_fn _chdir
#else
#include <unistd.h>
#include <sys/ioctl.h>
#define getcwd_fn getcwd
#define chdir_fn chdir
#endif
#include <stdio.h>
#include <string.h>

extern char** environ;

Value node_process_env(void) {
  TSHashMap* map = ts_hashmap_new();
  char** env = environ;
  while (*env) {
    char* eq = strchr(*env, '=');
    if (eq) {
      size_t nameLen = eq - *env;
      char* name = (char*)malloc(nameLen + 1);
      memcpy(name, *env, nameLen);
      name[nameLen] = '\0';
      ts_hashmap_set(map, ts_string_new(name), ts_value_string(ts_string_new(eq + 1)));
      free(name);
    }
    env++;
  }
  return ts_value_object(map);
}

/* Saved from main() — used by process.argv on Unix/Android */
static int g_ts_argc = 0;
static char** g_ts_argv = NULL;

void node_process_set_argv(int argc, char** argv) {
  g_ts_argc = argc;
  g_ts_argv = argv;
}

Value node_process_argv(void) {
  TSArray* arr = ts_array_new();
#ifdef _WIN32
  int argc;
  wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 0; i < argc; i++) {
    /* Simplified: assumes ASCII */
    char buf[1024];
    WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, buf, sizeof(buf), NULL, NULL);
    ts_array_push(arr, ts_value_string(ts_string_new(buf)));
  }
  LocalFree(wargv);
#else
  /* Prefer argv captured from main(); never use glibc-only __argc/__argv
     (missing on Android/Termux, musl, etc.). */
  if (g_ts_argc > 0 && g_ts_argv) {
    for (int i = 0; i < g_ts_argc; i++) {
      const char* a = g_ts_argv[i] ? g_ts_argv[i] : "";
      ts_array_push(arr, ts_value_string(ts_string_new(a)));
    }
  } else {
    /* Fallback: single empty placeholder so callers still get an array */
    ts_array_push(arr, ts_value_string(ts_string_new("")));
  }
#endif
  return ts_value_array(arr);
}

Value node_process_cwd(void) {
  char buf[4096];
  if (getcwd_fn(buf, sizeof(buf))) {
    return ts_value_string(ts_string_new(buf));
  }
  return ts_value_string(ts_string_new("."));
}

int node_process_chdir(Value dir) {
  TSString* dirStr = ts_to_string(dir);
  return chdir_fn(dirStr->data);
}

void node_process_exit(Value code) {
  int exitCode = (int)ts_to_number(code);
  exit(exitCode);
}

int node_process_pid(void) {
#ifdef _WIN32
  return (int)GetCurrentProcessId();
#else
  return (int)getpid();
#endif
}

/* Minimal stream-like objects for console.log(process.stdin/out/err) */
static void stream_get_size(int fd, int* rows, int* cols) {
  *rows = 24;
  *cols = 80;
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (fd == 0) h = GetStdHandle(STD_INPUT_HANDLE);
  if (fd == 2) h = GetStdHandle(STD_ERROR_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(h, &csbi)) {
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
  }
#else
  struct winsize ws;
  if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
    *rows = ws.ws_row;
    *cols = ws.ws_col;
  }
#endif
}

static Value make_stream(const char* fdName, int fd, int isTTY) {
  TSHashMap* stream = ts_hashmap_new();
  int rows = 24, cols = 80;
  stream_get_size(fd, &rows, &cols);
  ts_hashmap_set(stream, ts_string_new("fd"), ts_value_number((double)fd));
  ts_hashmap_set(stream, ts_string_new("isTTY"), ts_value_boolean(isTTY));
  ts_hashmap_set(stream, ts_string_new("rows"), ts_value_number((double)rows));
  ts_hashmap_set(stream, ts_string_new("columns"), ts_value_number((double)cols));
  ts_hashmap_set(stream, ts_string_new("_type"), ts_value_string(ts_string_new(fdName)));
  return ts_value_object(stream);
}

Value node_process_stdin(void) {
#ifdef _WIN32
  int isTTY = GetFileType(GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_CHAR;
#else
  int isTTY = isatty(0);
#endif
  return make_stream("stdin", 0, isTTY);
}

Value node_process_stdout(void) {
#ifdef _WIN32
  int isTTY = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_CHAR;
#else
  int isTTY = isatty(1);
#endif
  return make_stream("stdout", 1, isTTY);
}

Value node_process_stderr(void) {
#ifdef _WIN32
  int isTTY = GetFileType(GetStdHandle(STD_ERROR_HANDLE)) == FILE_TYPE_CHAR;
#else
  int isTTY = isatty(2);
#endif
  return make_stream("stderr", 2, isTTY);
}

typedef Value (*StreamDataCallback)(Value data);

static Value stream_on_read(int fd, Value event, Value callback) {
  TSString* eventStr = ts_to_string(event);
  if (!eventStr || !eventStr->data) return ts_value_undefined();

  /* Only "data" is implemented for now */
  if (strcmp(eventStr->data, "data") != 0) {
    return ts_value_undefined();
  }

  if (callback.tag != TAG_FUNCTION || !callback.as.function) {
    return ts_value_undefined();
  }

  StreamDataCallback cb = (StreamDataCallback)callback.as.function;
  char buf[4096];

#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
  if (fd == 1) h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (fd == 2) h = GetStdHandle(STD_ERROR_HANDLE);

  /* Read until EOF (Ctrl+Z / pipe close) */
  for (;;) {
    DWORD n = 0;
    if (!ReadFile(h, buf, sizeof(buf) - 1, &n, NULL) || n == 0) break;
    buf[n] = '\0';
    cb(ts_value_string(ts_string_new(buf)));
  }
#else
  for (;;) {
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) break;
    buf[n] = '\0';
    cb(ts_value_string(ts_string_new(buf)));
  }
#endif

  return ts_value_undefined();
}

Value node_process_stdin_on(Value event, Value callback) {
  return stream_on_read(0, event, callback);
}

Value node_process_stdout_on(Value event, Value callback) {
  return stream_on_read(1, event, callback);
}

Value node_process_stderr_on(Value event, Value callback) {
  return stream_on_read(2, event, callback);
}

/* Buffer/string-like toString for stream data chunks */
Value node_process_stream_toString(Value val) {
  return ts_value_string(ts_to_string(val));
}

#ifdef _WIN32
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
static void enable_vt(HANDLE h) {
  DWORD mode = 0;
  if (GetConsoleMode(h, &mode)) {
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
}
#endif

static Value stream_write(FILE* fp, Value data) {
  TSString* s = ts_to_string(data);
  if (s && s->data) {
    fputs(s->data, fp);
    fflush(fp);
  }
  return ts_value_boolean(1);
}

static Value stream_cursor_to(FILE* fp, Value x, Value y) {
  int col = (int)ts_to_number(x) + 1; /* ANSI is 1-based */
  int row = (int)ts_to_number(y) + 1;
  if (col < 1) col = 1;
  if (row < 1) row = 1;
#ifdef _WIN32
  enable_vt(fp == stderr ? GetStdHandle(STD_ERROR_HANDLE) : GetStdHandle(STD_OUTPUT_HANDLE));
#endif
  fprintf(fp, "\x1b[%d;%dH", row, col);
  fflush(fp);
  return ts_value_boolean(1);
}

static Value stream_move_cursor(FILE* fp, Value dx, Value dy) {
  int x = (int)ts_to_number(dx);
  int y = (int)ts_to_number(dy);
#ifdef _WIN32
  enable_vt(fp == stderr ? GetStdHandle(STD_ERROR_HANDLE) : GetStdHandle(STD_OUTPUT_HANDLE));
#endif
  if (y < 0) fprintf(fp, "\x1b[%dA", -y);
  else if (y > 0) fprintf(fp, "\x1b[%dB", y);
  if (x > 0) fprintf(fp, "\x1b[%dC", x);
  else if (x < 0) fprintf(fp, "\x1b[%dD", -x);
  fflush(fp);
  return ts_value_boolean(1);
}

static Value stream_clear_screen_down(FILE* fp) {
#ifdef _WIN32
  enable_vt(fp == stderr ? GetStdHandle(STD_ERROR_HANDLE) : GetStdHandle(STD_OUTPUT_HANDLE));
#endif
  fputs("\x1b[J", fp);
  fflush(fp);
  return ts_value_boolean(1);
}

/* Node clearLine(dir): -1 = to start, 0 = entire line, 1 = to end */
static Value stream_clear_line(FILE* fp, Value dir) {
  int d = (int)ts_to_number(dir);
#ifdef _WIN32
  enable_vt(fp == stderr ? GetStdHandle(STD_ERROR_HANDLE) : GetStdHandle(STD_OUTPUT_HANDLE));
#endif
  if (d < 0) fputs("\x1b[1K", fp);
  else if (d > 0) fputs("\x1b[0K", fp);
  else fputs("\x1b[2K", fp);
  fflush(fp);
  return ts_value_boolean(1);
}

Value node_process_stdout_write(Value data) {
  return stream_write(stdout, data);
}

Value node_process_stderr_write(Value data) {
  return stream_write(stderr, data);
}

Value node_process_stdout_cursorTo(Value x, Value y) {
  return stream_cursor_to(stdout, x, y);
}

Value node_process_stderr_cursorTo(Value x, Value y) {
  return stream_cursor_to(stderr, x, y);
}

Value node_process_stdout_moveCursor(Value dx, Value dy) {
  return stream_move_cursor(stdout, dx, dy);
}

Value node_process_stderr_moveCursor(Value dx, Value dy) {
  return stream_move_cursor(stderr, dx, dy);
}

Value node_process_stdout_clearScreenDown(void) {
  return stream_clear_screen_down(stdout);
}

Value node_process_stderr_clearScreenDown(void) {
  return stream_clear_screen_down(stderr);
}

Value node_process_stdout_clearLine(Value dir) {
  return stream_clear_line(stdout, dir);
}

Value node_process_stderr_clearLine(Value dir) {
  return stream_clear_line(stderr, dir);
}
