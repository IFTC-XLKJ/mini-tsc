#include "node_child_process.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#endif

typedef Value (*ExecCallback)(Value error, Value stdoutVal, Value stderrVal);
typedef Value (*DataCallback)(Value data);
typedef Value (*CloseCallback)(Value code, Value signal);
typedef Value (*MessageCallback)(Value msg);

/* ---------- dynamic string buffer ---------- */

typedef struct {
  char* data;
  size_t len;
  size_t cap;
} DynBuf;

static void buf_init(DynBuf* b) {
  b->data = (char*)malloc(256);
  if (b->data) {
    b->data[0] = '\0';
    b->cap = 256;
  } else {
    b->cap = 0;
  }
  b->len = 0;
}

static void buf_free(DynBuf* b) {
  free(b->data);
  b->data = NULL;
  b->len = b->cap = 0;
}

static int buf_grow(DynBuf* b, size_t need) {
  if (b->len + need + 1 <= b->cap) return 1;
  size_t ncap = b->cap ? b->cap : 256;
  while (ncap < b->len + need + 1) ncap *= 2;
  char* p = (char*)realloc(b->data, ncap);
  if (!p) return 0;
  b->data = p;
  b->cap = ncap;
  return 1;
}

static void buf_append(DynBuf* b, const char* s, size_t n) {
  if (!s || n == 0) return;
  if (!buf_grow(b, n)) return;
  memcpy(b->data + b->len, s, n);
  b->len += n;
  b->data[b->len] = '\0';
}

static void buf_append_cstr(DynBuf* b, const char* s) {
  if (!s) return;
  buf_append(b, s, strlen(s));
}

static TSString* buf_to_tsstring(DynBuf* b) {
  return ts_string_new(b->data ? b->data : "");
}

/* ---------- quoting / cmdline ---------- */

#ifdef _WIN32
/* Windows CreateProcess quoting rules (simplified Node-compatible). */
static void append_win_quoted(DynBuf* b, const char* arg) {
  if (!arg) arg = "";
  int need_quote = 0;
  for (const char* p = arg; *p; p++) {
    if (*p == ' ' || *p == '\t' || *p == '"' || *p == '\n' || *p == '\v') {
      need_quote = 1;
      break;
    }
  }
  if (!need_quote) {
    buf_append_cstr(b, arg);
    return;
  }
  buf_append_cstr(b, "\"");
  for (const char* p = arg; *p; ) {
    int bs = 0;
    while (*p == '\\') { bs++; p++; }
    if (*p == '\0') {
      for (int i = 0; i < bs * 2; i++) buf_append_cstr(b, "\\");
      break;
    }
    if (*p == '"') {
      for (int i = 0; i < bs * 2 + 1; i++) buf_append_cstr(b, "\\");
      buf_append_cstr(b, "\"");
      p++;
    } else {
      for (int i = 0; i < bs; i++) buf_append_cstr(b, "\\");
      buf_append(b, p, 1);
      p++;
    }
  }
  buf_append_cstr(b, "\"");
}
#else
static void append_posix_quoted(DynBuf* b, const char* arg) {
  if (!arg) arg = "";
  int need_quote = 0;
  for (const char* p = arg; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c <= 0x20 || strchr("\"'`$\\!#&*|<>()[]{};?", c)) {
      need_quote = 1;
      break;
    }
  }
  if (!need_quote) {
    buf_append_cstr(b, arg);
    return;
  }
  buf_append_cstr(b, "'");
  for (const char* p = arg; *p; p++) {
    if (*p == '\'') buf_append_cstr(b, "'\\''");
    else buf_append(b, p, 1);
  }
  buf_append_cstr(b, "'");
}
#endif

static char* build_cmdline(const char* file, Value args) {
  DynBuf b;
  buf_init(&b);
  if (!b.data) return NULL;

#ifdef _WIN32
  append_win_quoted(&b, file ? file : "");
#else
  append_posix_quoted(&b, file ? file : "");
#endif

  if (args.tag == TAG_ARRAY && args.as.array) {
    TSArray* arr = args.as.array;
    for (int i = 0; i < arr->length; i++) {
      TSString* a = ts_to_string(ts_array_get(arr, i));
      const char* s = (a && a->data) ? a->data : "";
      buf_append_cstr(&b, " ");
#ifdef _WIN32
      append_win_quoted(&b, s);
#else
      append_posix_quoted(&b, s);
#endif
    }
  }
  return b.data; /* caller frees */
}

/* ---------- process run with separate stdout/stderr ---------- */

typedef struct {
  TSString* stdout_str;
  TSString* stderr_str;
  int exit_code;
  int signal; /* 0 if exited normally */
  double pid;
} RunResult;

static RunResult run_result_empty(int code) {
  RunResult r;
  r.stdout_str = ts_string_new("");
  r.stderr_str = ts_string_new("");
  r.exit_code = code;
  r.signal = 0;
  r.pid = 0;
  return r;
}

#ifdef _WIN32

static void read_pipe_all(HANDLE h, DynBuf* out) {
  char chunk[4096];
  DWORD n = 0;
  for (;;) {
    if (!ReadFile(h, chunk, sizeof(chunk), &n, NULL) || n == 0) break;
    buf_append(out, chunk, (size_t)n);
  }
}

static RunResult run_process_capture(const char* cmdline, int shell) {
  RunResult r = run_result_empty(1);
  if (!cmdline) return r;

  SECURITY_ATTRIBUTES sa;
  ZeroMemory(&sa, sizeof(sa));
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE out_rd = NULL, out_wr = NULL;
  HANDLE err_rd = NULL, err_wr = NULL;
  if (!CreatePipe(&out_rd, &out_wr, &sa, 0) ||
      !CreatePipe(&err_rd, &err_wr, &sa, 0)) {
    return r;
  }
  SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_rd, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  ZeroMemory(&pi, sizeof(pi));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = out_wr;
  si.hStdError = err_wr;

  char* cmd_mutable = _strdup(cmdline);
  if (!cmd_mutable) {
    CloseHandle(out_rd); CloseHandle(out_wr);
    CloseHandle(err_rd); CloseHandle(err_wr);
    return r;
  }

  BOOL ok;
  if (shell) {
    /* cmd.exe /c <cmdline> */
    DynBuf shell_cmd;
    buf_init(&shell_cmd);
    buf_append_cstr(&shell_cmd, "cmd.exe /d /s /c \"");
    /* escape embedded quotes lightly by doubling is not perfect; pass as-is inside quotes */
    buf_append_cstr(&shell_cmd, cmdline);
    buf_append_cstr(&shell_cmd, "\"");
    free(cmd_mutable);
    cmd_mutable = shell_cmd.data;
    ok = CreateProcessA(NULL, cmd_mutable, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  } else {
    ok = CreateProcessA(NULL, cmd_mutable, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  }

  CloseHandle(out_wr);
  CloseHandle(err_wr);

  if (!ok) {
    free(cmd_mutable);
    CloseHandle(out_rd);
    CloseHandle(err_rd);
    return r;
  }

  r.pid = (double)(uintptr_t)pi.dwProcessId;

  DynBuf bout, berr;
  buf_init(&bout);
  buf_init(&berr);
  read_pipe_all(out_rd, &bout);
  read_pipe_all(err_rd, &berr);
  CloseHandle(out_rd);
  CloseHandle(err_rd);

  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  r.exit_code = (int)code;
  r.signal = 0;
  r.stdout_str = buf_to_tsstring(&bout);
  r.stderr_str = buf_to_tsstring(&berr);
  buf_free(&bout);
  buf_free(&berr);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  free(cmd_mutable);
  return r;
}

#else /* POSIX */

static void read_fd_all(int fd, DynBuf* out) {
  char chunk[4096];
  for (;;) {
    ssize_t n = read(fd, chunk, sizeof(chunk));
    if (n <= 0) break;
    buf_append(out, chunk, (size_t)n);
  }
}

static RunResult run_process_capture(const char* cmdline, int shell) {
  RunResult r = run_result_empty(1);
  if (!cmdline) return r;

  int out_pipe[2], err_pipe[2];
  if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) return r;

  pid_t pid = fork();
  if (pid < 0) {
    close(out_pipe[0]); close(out_pipe[1]);
    close(err_pipe[0]); close(err_pipe[1]);
    return r;
  }

  if (pid == 0) {
    /* child */
    close(out_pipe[0]);
    close(err_pipe[0]);
    dup2(out_pipe[1], STDOUT_FILENO);
    dup2(err_pipe[1], STDERR_FILENO);
    close(out_pipe[1]);
    close(err_pipe[1]);
    if (shell) {
      execl("/bin/sh", "sh", "-c", cmdline, (char*)NULL);
    } else {
      /* non-shell: still use sh -c for simplicity when we only have a cmdline string */
      execl("/bin/sh", "sh", "-c", cmdline, (char*)NULL);
    }
    _exit(127);
  }

  close(out_pipe[1]);
  close(err_pipe[1]);
  r.pid = (double)pid;

  DynBuf bout, berr;
  buf_init(&bout);
  buf_init(&berr);
  read_fd_all(out_pipe[0], &bout);
  read_fd_all(err_pipe[0], &berr);
  close(out_pipe[0]);
  close(err_pipe[0]);

  int st = 0;
  if (waitpid(pid, &st, 0) < 0) {
    r.exit_code = 1;
  } else if (WIFEXITED(st)) {
    r.exit_code = WEXITSTATUS(st);
    r.signal = 0;
  } else if (WIFSIGNALED(st)) {
    r.exit_code = 1;
    r.signal = WTERMSIG(st);
  } else {
    r.exit_code = 1;
  }

  r.stdout_str = buf_to_tsstring(&bout);
  r.stderr_str = buf_to_tsstring(&berr);
  buf_free(&bout);
  buf_free(&berr);
  return r;
}

#endif

/* ---------- Value helpers ---------- */

static Value make_error_value(const char* msg, int code) {
  TSHashMap* err = ts_hashmap_new();
  ts_hashmap_set(err, ts_string_new("message"), ts_value_string(ts_string_new(msg ? msg : "Command failed")));
  ts_hashmap_set(err, ts_string_new("code"), ts_value_number((double)code));
  ts_hashmap_set(err, ts_string_new("_type"), ts_value_string(ts_string_new("Error")));
  return ts_value_object(err);
}

static Value make_child_object(double pid, TSString* stdoutStr, TSString* stderrStr, double status, int signal) {
  TSHashMap* child = ts_hashmap_new();
  ts_hashmap_set(child, ts_string_new("pid"), ts_value_number(pid));
  ts_hashmap_set(child, ts_string_new("status"), ts_value_number(status));
  ts_hashmap_set(child, ts_string_new("signalCode"),
                 signal ? ts_value_number((double)signal) : ts_value_null());
  ts_hashmap_set(child, ts_string_new("_stdout"), ts_value_string(stdoutStr ? stdoutStr : ts_string_new("")));
  ts_hashmap_set(child, ts_string_new("_stderr"), ts_value_string(stderrStr ? stderrStr : ts_string_new("")));
  ts_hashmap_set(child, ts_string_new("_type"), ts_value_string(ts_string_new("ChildProcess")));

  TSHashMap* outStream = ts_hashmap_new();
  ts_hashmap_set(outStream, ts_string_new("_type"), ts_value_string(ts_string_new("stdout")));
  ts_hashmap_set(child, ts_string_new("stdout"), ts_value_object(outStream));

  TSHashMap* errStream = ts_hashmap_new();
  ts_hashmap_set(errStream, ts_string_new("_type"), ts_value_string(ts_string_new("stderr")));
  ts_hashmap_set(child, ts_string_new("stderr"), ts_value_object(errStream));

  return ts_value_object(child);
}

/* ---------- public API ---------- */

Value node_child_process_execSync(Value command, Value options) {
  (void)options;
  TSString* cmdStr = ts_to_string(command);
  const char* cmd = (cmdStr && cmdStr->data) ? cmdStr->data : "";
  RunResult r = run_process_capture(cmd, 1 /* shell */);
  if (r.exit_code != 0) {
    /* Node throws on non-zero; we still return stdout for simplicity */
  }
  return ts_value_string(r.stdout_str ? r.stdout_str : ts_string_new(""));
}

Value node_child_process_exec(Value command, Value options, Value callback) {
  (void)options;
  TSString* cmdStr = ts_to_string(command);
  const char* cmd = (cmdStr && cmdStr->data) ? cmdStr->data : "";
  RunResult r = run_process_capture(cmd, 1 /* shell */);

  Value err = (r.exit_code != 0)
    ? make_error_value("Command failed", r.exit_code)
    : ts_value_null();
  Value stdoutVal = ts_value_string(r.stdout_str ? r.stdout_str : ts_string_new(""));
  Value stderrVal = ts_value_string(r.stderr_str ? r.stderr_str : ts_string_new(""));

  if (callback.tag == TAG_FUNCTION && callback.as.function) {
    ExecCallback cb = (ExecCallback)callback.as.function;
    cb(err, stdoutVal, stderrVal);
  }
  return stdoutVal;
}

Value node_child_process_execFile(Value file, Value args, Value options, Value callback) {
  (void)options;
  TSString* fileStr = ts_to_string(file);
  char* cmdline = build_cmdline(fileStr && fileStr->data ? fileStr->data : "", args);
  RunResult r = run_process_capture(cmdline ? cmdline : "", 0 /* no shell */);
  free(cmdline);

  Value err = (r.exit_code != 0)
    ? make_error_value("Command failed", r.exit_code)
    : ts_value_null();
  Value stdoutVal = ts_value_string(r.stdout_str ? r.stdout_str : ts_string_new(""));
  Value stderrVal = ts_value_string(r.stderr_str ? r.stderr_str : ts_string_new(""));

  if (callback.tag == TAG_FUNCTION && callback.as.function) {
    ExecCallback cb = (ExecCallback)callback.as.function;
    cb(err, stdoutVal, stderrVal);
  }
  return stdoutVal;
}

Value node_child_process_spawn(Value command, Value args, Value options) {
  (void)options;
  TSString* cmdStr = ts_to_string(command);
  char* cmdline = NULL;
  int use_shell = 0;

  if (args.tag == TAG_ARRAY) {
    cmdline = build_cmdline(cmdStr && cmdStr->data ? cmdStr->data : "", args);
  } else {
    /* spawn("dir") style — treat as shell command on Windows (cmd builtins) */
    use_shell = 1;
    DynBuf b;
    buf_init(&b);
    buf_append_cstr(&b, cmdStr && cmdStr->data ? cmdStr->data : "");
    cmdline = b.data;
  }

  RunResult r = run_process_capture(cmdline ? cmdline : "", use_shell);
  free(cmdline);
  return make_child_object(r.pid, r.stdout_str, r.stderr_str, (double)r.exit_code, r.signal);
}

Value node_child_process_fork(Value modulePath, Value args, Value options) {
  (void)args;
  (void)options;
  TSString* mod = ts_to_string(modulePath);
  const char* path = (mod && mod->data) ? mod->data : "";

  /* node <modulePath> with proper quoting */
  Value nodeArgs = ts_value_null();
  {
    TSArray* arr = ts_array_new();
    ts_array_push(arr, ts_value_string(ts_string_new(path)));
    nodeArgs = ts_value_array(arr);
  }
  char* cmdline = build_cmdline("node", nodeArgs);
  RunResult r = run_process_capture(cmdline ? cmdline : "", 0);
  free(cmdline);

  Value child = make_child_object(r.pid, r.stdout_str, r.stderr_str, (double)r.exit_code, r.signal);
  if (child.tag == TAG_OBJECT && child.as.object) {
    ts_hashmap_set((TSHashMap*)child.as.object, ts_string_new("_fork"), ts_value_boolean(1));
  }
  return child;
}

Value node_child_process_on(Value child, Value event, Value callback) {
  if (child.tag != TAG_OBJECT || !child.as.object) return ts_value_undefined();
  TSHashMap* map = (TSHashMap*)child.as.object;
  TSString* ev = ts_to_string(event);
  if (!ev || !ev->data) return ts_value_undefined();

  if (strcmp(ev->data, "close") == 0 || strcmp(ev->data, "exit") == 0) {
    Value status = ts_hashmap_get(map, ts_string_new("status"));
    Value signal = ts_hashmap_get(map, ts_string_new("signalCode"));
    if (callback.tag == TAG_FUNCTION && callback.as.function) {
      CloseCallback cb = (CloseCallback)callback.as.function;
      cb(status, signal.tag == TAG_NULL ? ts_value_null() : signal);
    }
  } else if (strcmp(ev->data, "message") == 0) {
    ts_hashmap_set(map, ts_string_new("_message_cb"), callback);
    if (ts_hashmap_has(map, ts_string_new("_message")) &&
        callback.tag == TAG_FUNCTION && callback.as.function) {
      Value msg = ts_hashmap_get(map, ts_string_new("_message"));
      MessageCallback cb = (MessageCallback)callback.as.function;
      cb(msg);
    }
  } else if (strcmp(ev->data, "error") == 0) {
    /* fire only if error stored */
    if (ts_hashmap_has(map, ts_string_new("_error")) &&
        callback.tag == TAG_FUNCTION && callback.as.function) {
      Value err = ts_hashmap_get(map, ts_string_new("_error"));
      MessageCallback cb = (MessageCallback)callback.as.function;
      cb(err);
    }
  }
  return ts_value_undefined();
}

Value node_child_process_stream_on(Value child, Value streamName, Value event, Value callback) {
  if (child.tag != TAG_OBJECT || !child.as.object) return ts_value_undefined();
  TSHashMap* map = (TSHashMap*)child.as.object;
  TSString* stream = ts_to_string(streamName);
  TSString* ev = ts_to_string(event);
  if (!stream || !stream->data || !ev || !ev->data) return ts_value_undefined();
  if (strcmp(ev->data, "data") != 0) return ts_value_undefined();

  const char* key = (strcmp(stream->data, "stderr") == 0) ? "_stderr" : "_stdout";
  Value data = ts_hashmap_get(map, ts_string_new(key));
  if (callback.tag == TAG_FUNCTION && callback.as.function) {
    DataCallback cb = (DataCallback)callback.as.function;
    if (data.tag == TAG_STRING && data.as.string && data.as.string->length > 0) {
      cb(data);
    }
  }
  return ts_value_undefined();
}

Value node_child_process_send(Value child, Value message) {
  if (child.tag != TAG_OBJECT || !child.as.object) return ts_value_boolean(0);
  TSHashMap* map = (TSHashMap*)child.as.object;
  ts_hashmap_set(map, ts_string_new("_message"), message);

  Value cbVal = ts_hashmap_get(map, ts_string_new("_message_cb"));
  if (cbVal.tag == TAG_FUNCTION && cbVal.as.function) {
    MessageCallback cb = (MessageCallback)cbVal.as.function;
    cb(message);
  }
  return ts_value_boolean(1);
}
