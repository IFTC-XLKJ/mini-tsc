#include "node_ffmpeg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

/* ── helpers (mirrors child_process exec style) ── */

typedef struct {
  char*  data;
  size_t len;
  size_t cap;
} DynBuf;

static void buf_init(DynBuf* b) { b->cap = 4096; b->len = 0; b->data = (char*)malloc(b->cap); b->data[0] = 0; }
static void buf_append(DynBuf* b, const char* s, size_t n) {
  if (n == 0) return;
  if (b->len + n + 1 > b->cap) {
    while (b->len + n + 1 > b->cap) b->cap *= 2;
    b->data = (char*)realloc(b->data, b->cap);
  }
  memcpy(b->data + b->len, s, n);
  b->len += n;
  b->data[b->len] = 0;
}

static TSString* ts_str_from_c(const char* s) { return ts_string_new(s ? s : ""); }

static char* value_to_cstr(Value v) {
  if (v.tag == TAG_STRING && v.as.string) {
    char* out = (char*)malloc((size_t)v.as.string->length + 1);
    memcpy(out, v.as.string->data, (size_t)v.as.string->length);
    out[v.as.string->length] = 0;
    return out;
  }
  TSString* s = ts_value_to_string(v);
  if (!s) {
    char* out = (char*)malloc(1); out[0] = 0; return out;
  }
  char* out = (char*)malloc((size_t)s->length + 1);
  memcpy(out, s->data, (size_t)s->length);
  out[s->length] = 0;
  return out;
}

static Value make_result(const char* stdout_s, const char* stderr_s, int code) {
  TSHashMap* map = ts_hashmap_new();
  ts_hashmap_set(map, ts_string_new("stdout"), ts_value_string(ts_str_from_c(stdout_s)));
  ts_hashmap_set(map, ts_string_new("stderr"), ts_value_string(ts_str_from_c(stderr_s)));
  ts_hashmap_set(map, ts_string_new("status"), ts_value_number((double)code));
  ts_hashmap_set(map, ts_string_new("exitCode"), ts_value_number((double)code));
  ts_hashmap_set(map, ts_string_new("ok"), ts_value_boolean(code == 0 ? 1 : 0));
  return ts_value_object(map);
}

#ifdef _WIN32
static void read_pipe_all(HANDLE h, DynBuf* b) {
  char tmp[4096]; DWORD n = 0;
  for (;;) {
    BOOL ok = ReadFile(h, tmp, sizeof(tmp), &n, NULL);
    if (!ok || n == 0) break;
    buf_append(b, tmp, (size_t)n);
  }
}

static char* quote_win_arg(const char* arg) {
  if (!arg) arg = "";
  int need = 0;
  for (const char* p = arg; *p; p++) {
    if (*p == ' ' || *p == '\t' || *p == '"' || *p == '\n' || *p == '\v') { need = 1; break; }
  }
  if (!need && *arg != 0) return _strdup(arg);
  size_t len = strlen(arg);
  size_t cap = len * 2 + 4;
  char* out = (char*)malloc(cap);
  size_t o = 0;
  out[o++] = '"';
  for (size_t i = 0; i < len; i++) {
    unsigned bs = 0;
    while (i < len && arg[i] == '\\') { bs++; i++; }
    if (i == len) {
      for (unsigned k = 0; k < bs * 2; k++) out[o++] = '\\';
      break;
    } else if (arg[i] == '"') {
      for (unsigned k = 0; k < bs * 2 + 1; k++) out[o++] = '\\';
      out[o++] = '"';
    } else {
      for (unsigned k = 0; k < bs; k++) out[o++] = '\\';
      out[o++] = arg[i];
    }
  }
  out[o++] = '"';
  out[o] = 0;
  return out;
}

static int run_command_capture(const char* cmdline, const char* cwd, DynBuf* outb, DynBuf* errb) {
  SECURITY_ATTRIBUTES sa; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = NULL;
  HANDLE out_rd = NULL, out_wr = NULL, err_rd = NULL, err_wr = NULL;
  if (!CreatePipe(&out_rd, &out_wr, &sa, 0)) return 127;
  if (!CreatePipe(&err_rd, &err_wr, &sa, 0)) { CloseHandle(out_rd); CloseHandle(out_wr); return 127; }
  SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_rd, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si; PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof(si)); memset(&pi, 0, sizeof(pi));
  si.cb = sizeof(si);
  si.dwFlags |= STARTF_USESTDHANDLES;
  si.hStdOutput = out_wr;
  si.hStdError = err_wr;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  char* cmd_mut = _strdup(cmdline);
  BOOL ok = CreateProcessA(NULL, cmd_mut, NULL, NULL, TRUE, 0, NULL, cwd, &si, &pi);
  free(cmd_mut);
  CloseHandle(out_wr); CloseHandle(err_wr);
  if (!ok) {
    CloseHandle(out_rd); CloseHandle(err_rd);
    return 127;
  }
  read_pipe_all(out_rd, outb);
  read_pipe_all(err_rd, errb);
  CloseHandle(out_rd); CloseHandle(err_rd);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  return (int)code;
}
#else
static void read_pipe_all(int fd, DynBuf* b) {
  char tmp[4096]; ssize_t n;
  while ((n = read(fd, tmp, sizeof(tmp))) > 0) buf_append(b, tmp, (size_t)n);
}

static char* quote_sh_arg(const char* arg) {
  if (!arg) arg = "";
  size_t len = strlen(arg);
  size_t cap = len * 4 + 3;
  char* out = (char*)malloc(cap);
  size_t o = 0;
  out[o++] = '\'';
  for (size_t i = 0; i < len; i++) {
    if (arg[i] == '\'') {
      memcpy(out + o, "'\\''", 4); o += 4;
    } else {
      out[o++] = arg[i];
    }
  }
  out[o++] = '\''; out[o] = 0;
  return out;
}

static int run_command_capture(const char* cmdline, const char* cwd, DynBuf* outb, DynBuf* errb) {
  int outp[2], errp[2];
  if (pipe(outp) != 0) return 127;
  if (pipe(errp) != 0) { close(outp[0]); close(outp[1]); return 127; }
  pid_t pid = fork();
  if (pid < 0) {
    close(outp[0]); close(outp[1]); close(errp[0]); close(errp[1]);
    return 127;
  }
  if (pid == 0) {
    dup2(outp[1], STDOUT_FILENO);
    dup2(errp[1], STDERR_FILENO);
    close(outp[0]); close(outp[1]); close(errp[0]); close(errp[1]);
    if (cwd && cwd[0]) chdir(cwd);
    execl("/bin/sh", "sh", "-c", cmdline, (char*)NULL);
    _exit(127);
  }
  close(outp[1]); close(errp[1]);
  read_pipe_all(outp[0], outb);
  read_pipe_all(errp[0], errb);
  close(outp[0]); close(errp[0]);
  int status = 0; waitpid(pid, &status, 0);
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return 127;
}
#endif

static char* get_opt_cwd(Value options) {
  if (options.tag != TAG_OBJECT || !options.as.object) return NULL;
  Value cwd = ts_hashmap_get((TSHashMap*)options.as.object, ts_string_new("cwd"));
  if (cwd.tag != TAG_STRING || !cwd.as.string) return NULL;
  return value_to_cstr(cwd);
}

static char* get_opt_bin(Value options, const char* key, const char* fallback) {
  if (options.tag == TAG_OBJECT && options.as.object) {
    Value v = ts_hashmap_get((TSHashMap*)options.as.object, ts_string_new(key));
    if (v.tag == TAG_STRING && v.as.string) return value_to_cstr(v);
  }
  return _strdup(fallback);
}

#ifndef _WIN32
#define _strdup strdup
#endif

static char* build_command_from_args(const char* bin, Value args) {
  DynBuf b; buf_init(&b);
#ifdef _WIN32
  char* qbin = quote_win_arg(bin);
  buf_append(&b, qbin, strlen(qbin)); free(qbin);
#else
  char* qbin = quote_sh_arg(bin);
  buf_append(&b, qbin, strlen(qbin)); free(qbin);
#endif
  if (args.tag == TAG_ARRAY && args.as.array) {
    TSArray* arr = (TSArray*)args.as.array;
    for (int i = 0; i < arr->length; i++) {
      char* a = value_to_cstr(arr->items[i]);
      buf_append(&b, " ", 1);
#ifdef _WIN32
      char* qa = quote_win_arg(a);
#else
      char* qa = quote_sh_arg(a);
#endif
      buf_append(&b, qa, strlen(qa));
      free(qa); free(a);
    }
  }
  return b.data; /* caller frees */
}

static Value run_binary_with_args(const char* default_bin, Value args, Value options) {
  char* bin = get_opt_bin(options, "bin", default_bin);
  char* cwd = get_opt_cwd(options);
  char* cmd = build_command_from_args(bin, args);
  DynBuf outb, errb; buf_init(&outb); buf_init(&errb);
  int code = run_command_capture(cmd, cwd, &outb, &errb);
  Value r = make_result(outb.data, errb.data, code);
  free(bin); if (cwd) free(cwd); free(cmd); free(outb.data); free(errb.data);
  return r;
}

/* ── public API ── */

Value node_ffmpeg_run(Value args, Value options) {
  /* allow (argsArray) or (binOrCmd, argsArrayOrOptions) flexibility is not needed; keep simple */
  return run_binary_with_args("ffmpeg", args, options);
}

Value node_ffmpeg_exec(Value command, Value options) {
  char* cmd = value_to_cstr(command);
  char* cwd = get_opt_cwd(options);
  DynBuf outb, errb; buf_init(&outb); buf_init(&errb);
  int code = run_command_capture(cmd, cwd, &outb, &errb);
  Value r = make_result(outb.data, errb.data, code);
  free(cmd); if (cwd) free(cwd); free(outb.data); free(errb.data);
  return r;
}

Value node_ffmpeg_ffprobe(Value args, Value options) {
  return run_binary_with_args("ffprobe", args, options);
}

Value node_ffmpeg_probe(Value file, Value options) {
  /* build args: -v error -print_format json -show_format -show_streams <file> plus passthrough? */
  char* path = value_to_cstr(file);
  TSArray* arr = ts_array_new();
  ts_array_push(arr, ts_value_string(ts_str_from_c("-v"))); ts_array_push(arr, ts_value_string(ts_str_from_c("error")));
  ts_array_push(arr, ts_value_string(ts_str_from_c("-print_format"))); ts_array_push(arr, ts_value_string(ts_str_from_c("json")));
  ts_array_push(arr, ts_value_string(ts_str_from_c("-show_format"))); ts_array_push(arr, ts_value_string(ts_str_from_c("-show_streams")));
  /* optional extra args in options.args */
  if (options.tag == TAG_OBJECT && options.as.object) {
    Value extra = ts_hashmap_get((TSHashMap*)options.as.object, ts_string_new("args"));
    if (extra.tag == TAG_ARRAY && extra.as.array) {
      TSArray* ea = (TSArray*)extra.as.array;
      for (int i = 0; i < ea->length; i++) ts_array_push(arr, ea->items[i]);
    }
  }
  ts_array_push(arr, ts_value_string(ts_str_from_c(path)));
  Value args; args.tag = TAG_ARRAY; args.as.array = arr;
  Value r = run_binary_with_args("ffprobe", args, options);
  /* attach parsed? leave stdout as JSON string for caller (JSON.parse in TS). add 'file' */
  if (r.tag == TAG_OBJECT && r.as.object) {
    ts_hashmap_set((TSHashMap*)r.as.object, ts_string_new("file"), ts_value_string(ts_str_from_c(path)));
  }
  free(path);
  return r;
}

Value node_ffmpeg_version(void) {
  TSArray* arr = ts_array_new();
  ts_array_push(arr, ts_value_string(ts_str_from_c("-version")));
  Value args; args.tag = TAG_ARRAY; args.as.array = arr;
  Value r = node_ffmpeg_run(args, ts_value_undefined());
  if (r.tag == TAG_OBJECT && r.as.object) {
    Value out = ts_hashmap_get((TSHashMap*)r.as.object, ts_string_new("stdout"));
    return out;
  }
  return ts_value_string(ts_str_from_c(""));
}

Value node_ffmpeg_isAvailable(void) {
  TSArray* arr = ts_array_new();
  ts_array_push(arr, ts_value_string(ts_str_from_c("-version")));
  Value args; args.tag = TAG_ARRAY; args.as.array = arr;
  Value r = node_ffmpeg_run(args, ts_value_undefined());
  if (r.tag == TAG_OBJECT && r.as.object) {
    Value ok = ts_hashmap_get((TSHashMap*)r.as.object, ts_string_new("ok"));
    return ok;
  }
  return ts_value_boolean(0);
}
