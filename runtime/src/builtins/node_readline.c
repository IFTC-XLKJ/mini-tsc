#include "node_readline.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Interface object fields:
 *   _type: "ReadlineInterface"
 *   _prompt: string
 *   _closed: boolean
 *   _line_cb: Function | null
 *   _close_cb: Function | null
 *   _history: array (unused v1)
 */

typedef Value (*LineCallback)(Value line);
typedef Value (*CloseCallback)(void);
typedef Value (*QuestionCallback)(Value answer);

static TSHashMap* as_map(Value v) {
  if (v.tag != TAG_OBJECT || !v.as.object) return NULL;
  return (TSHashMap*)v.as.object;
}

static int is_rl(Value rl) {
  TSHashMap* m = as_map(rl);
  if (!m) return 0;
  Value t = ts_hashmap_get(m, ts_string_new("_type"));
  return t.tag == TAG_STRING && t.as.string && t.as.string->data &&
         strcmp(t.as.string->data, "ReadlineInterface") == 0;
}

static void trim_crlf(char* s) {
  if (!s) return;
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
    s[n - 1] = '\0';
    n--;
  }
}

/* Read one line from stdin; returns newly allocated TSString* (never NULL).
 * On EOF returns empty string and sets *eof = 1. */
static TSString* read_line_stdin(int* eof) {
  if (eof) *eof = 0;
  char buf[4096];
  if (!fgets(buf, sizeof(buf), stdin)) {
    if (eof) *eof = 1;
    return ts_string_new("");
  }
  trim_crlf(buf);
  return ts_string_new(buf);
}

static void emit_line(TSHashMap* map, TSString* line) {
  Value cb = ts_hashmap_get(map, ts_string_new("_line_cb"));
  if (cb.tag == TAG_FUNCTION && cb.as.function) {
    LineCallback fn = (LineCallback)cb.as.function;
    fn(ts_value_string(line));
  }
}

static void emit_close(TSHashMap* map) {
  Value cb = ts_hashmap_get(map, ts_string_new("_close_cb"));
  if (cb.tag == TAG_FUNCTION && cb.as.function) {
    CloseCallback fn = (CloseCallback)cb.as.function;
    fn();
  }
}

Value node_readline_createInterface(Value options) {
  (void)options; /* input/output ignored — always stdin/stdout for v1 */
  TSHashMap* rl = ts_hashmap_new();
  ts_hashmap_set(rl, ts_string_new("_type"), ts_value_string(ts_string_new("ReadlineInterface")));
  ts_hashmap_set(rl, ts_string_new("_prompt"), ts_value_string(ts_string_new("> ")));
  ts_hashmap_set(rl, ts_string_new("_closed"), ts_value_boolean(0));
  ts_hashmap_set(rl, ts_string_new("_line_cb"), ts_value_null());
  ts_hashmap_set(rl, ts_string_new("_close_cb"), ts_value_null());
  return ts_value_object(rl);
}

Value node_readline_setPrompt(Value rl, Value prompt) {
  if (!is_rl(rl)) return rl;
  TSHashMap* map = as_map(rl);
  TSString* s = ts_to_string(prompt);
  ts_hashmap_set(map, ts_string_new("_prompt"),
                 ts_value_string(s ? s : ts_string_new("")));
  return rl;
}

Value node_readline_getPrompt(Value rl) {
  if (!is_rl(rl)) return ts_value_string(ts_string_new(""));
  TSHashMap* map = as_map(rl);
  Value p = ts_hashmap_get(map, ts_string_new("_prompt"));
  if (p.tag == TAG_STRING) return p;
  return ts_value_string(ts_string_new("> "));
}

Value node_readline_prompt(Value rl) {
  if (!is_rl(rl)) return ts_value_undefined();
  TSHashMap* map = as_map(rl);
  Value closed = ts_hashmap_get(map, ts_string_new("_closed"));
  if (ts_to_boolean(closed)) return ts_value_undefined();

  Value p = ts_hashmap_get(map, ts_string_new("_prompt"));
  TSString* s = ts_to_string(p);
  const char* prompt = (s && s->data) ? s->data : "> ";
  fputs(prompt, stdout);
  fflush(stdout);
  return ts_value_undefined();
}

Value node_readline_question(Value rl, Value query, Value callback) {
  if (!is_rl(rl)) return ts_value_undefined();
  TSHashMap* map = as_map(rl);
  Value closed = ts_hashmap_get(map, ts_string_new("_closed"));
  if (ts_to_boolean(closed)) return ts_value_undefined();

  TSString* q = ts_to_string(query);
  const char* qs = (q && q->data) ? q->data : "";
  fputs(qs, stdout);
  fflush(stdout);

  int eof = 0;
  TSString* line = read_line_stdin(&eof);
  Value answer = ts_value_string(line);

  /* Fire 'line' listener if any */
  emit_line(map, line);

  if (callback.tag == TAG_FUNCTION && callback.as.function) {
    QuestionCallback fn = (QuestionCallback)callback.as.function;
    fn(answer);
  }

  if (eof) {
    /* stdin closed → auto-close interface */
    ts_hashmap_set(map, ts_string_new("_closed"), ts_value_boolean(1));
    emit_close(map);
  }
  return ts_value_undefined();
}

Value node_readline_close(Value rl) {
  if (!is_rl(rl)) return ts_value_undefined();
  TSHashMap* map = as_map(rl);
  Value closed = ts_hashmap_get(map, ts_string_new("_closed"));
  if (ts_to_boolean(closed)) return ts_value_undefined();
  ts_hashmap_set(map, ts_string_new("_closed"), ts_value_boolean(1));
  emit_close(map);
  return ts_value_undefined();
}

Value node_readline_on(Value rl, Value event, Value callback) {
  if (!is_rl(rl)) return rl;
  TSHashMap* map = as_map(rl);
  TSString* ev = ts_to_string(event);
  if (!ev || !ev->data) return rl;

  if (strcmp(ev->data, "line") == 0) {
    ts_hashmap_set(map, ts_string_new("_line_cb"), callback);
  } else if (strcmp(ev->data, "close") == 0) {
    ts_hashmap_set(map, ts_string_new("_close_cb"), callback);
    /* If already closed, fire immediately */
    Value closed = ts_hashmap_get(map, ts_string_new("_closed"));
    if (ts_to_boolean(closed) && callback.tag == TAG_FUNCTION && callback.as.function) {
      CloseCallback fn = (CloseCallback)callback.as.function;
      fn();
    }
  }
  return rl;
}

Value node_readline_write(Value rl, Value data) {
  (void)rl;
  TSString* s = ts_to_string(data);
  if (s && s->data) {
    fputs(s->data, stdout);
    fflush(stdout);
  }
  return ts_value_undefined();
}

Value node_readline_pause(Value rl) {
  (void)rl;
  return ts_value_undefined();
}

Value node_readline_resume(Value rl) {
  (void)rl;
  return ts_value_undefined();
}

/* Static helpers — ANSI where possible */
void node_readline_clearLine(Value stream, Value dir) {
  (void)stream;
  int d = (int)ts_to_number(dir);
  /* -1 left, 0 entire, 1 right — approximate with full clear */
  if (d == 0) fputs("\r\x1b[2K", stdout);
  else if (d < 0) fputs("\x1b[1K", stdout);
  else fputs("\x1b[0K", stdout);
  fflush(stdout);
}

void node_readline_cursorTo(Value stream, Value x, Value y) {
  (void)stream;
  int col = (int)ts_to_number(x) + 1;
  int row = (y.tag == TAG_NULL || y.tag == TAG_UNDEFINED) ? -1 : (int)ts_to_number(y) + 1;
  if (row > 0) fprintf(stdout, "\x1b[%d;%dH", row, col);
  else fprintf(stdout, "\x1b[%dG", col);
  fflush(stdout);
}

void node_readline_moveCursor(Value stream, Value dx, Value dy) {
  (void)stream;
  int x = (int)ts_to_number(dx);
  int y = (int)ts_to_number(dy);
  if (y < 0) fprintf(stdout, "\x1b[%dA", -y);
  else if (y > 0) fprintf(stdout, "\x1b[%dB", y);
  if (x > 0) fprintf(stdout, "\x1b[%dC", x);
  else if (x < 0) fprintf(stdout, "\x1b[%dD", -x);
  fflush(stdout);
}
