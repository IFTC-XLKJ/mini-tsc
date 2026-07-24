#include "node_chalk.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ANSI escape code helpers */
static const char* get_fg_code(const char* name) {
  if (strcmp(name, "black") == 0) return "\033[30m";
  if (strcmp(name, "red") == 0) return "\033[31m";
  if (strcmp(name, "green") == 0) return "\033[32m";
  if (strcmp(name, "yellow") == 0) return "\033[33m";
  if (strcmp(name, "blue") == 0) return "\033[34m";
  if (strcmp(name, "magenta") == 0) return "\033[35m";
  if (strcmp(name, "cyan") == 0) return "\033[36m";
  if (strcmp(name, "white") == 0) return "\033[37m";
  if (strcmp(name, "gray") == 0 || strcmp(name, "grey") == 0) return "\033[90m";
  if (strcmp(name, "redBright") == 0) return "\033[91m";
  if (strcmp(name, "greenBright") == 0) return "\033[92m";
  if (strcmp(name, "yellowBright") == 0) return "\033[93m";
  if (strcmp(name, "blueBright") == 0) return "\033[94m";
  if (strcmp(name, "magentaBright") == 0) return "\033[95m";
  if (strcmp(name, "cyanBright") == 0) return "\033[96m";
  if (strcmp(name, "whiteBright") == 0) return "\033[97m";
  return "\033[37m";
}

static const char* get_bg_code(const char* name) {
  if (strcmp(name, "black") == 0) return "\033[40m";
  if (strcmp(name, "red") == 0) return "\033[41m";
  if (strcmp(name, "green") == 0) return "\033[42m";
  if (strcmp(name, "yellow") == 0) return "\033[43m";
  if (strcmp(name, "blue") == 0) return "\033[44m";
  if (strcmp(name, "magenta") == 0) return "\033[45m";
  if (strcmp(name, "cyan") == 0) return "\033[46m";
  if (strcmp(name, "white") == 0) return "\033[47m";
  return "\033[47m";
}

static const char* get_mod_code(const char* name) {
  if (strcmp(name, "bold") == 0) return "\033[1m";
  if (strcmp(name, "dim") == 0) return "\033[2m";
  if (strcmp(name, "italic") == 0) return "\033[3m";
  if (strcmp(name, "underline") == 0) return "\033[4m";
  if (strcmp(name, "strikethrough") == 0) return "\033[9m";
  if (strcmp(name, "visible") == 0) return "\033[28m";
  if (strcmp(name, "reset") == 0) return "\033[0m";
  return "";
}

static Value wrap_ansi(Value text, const char* open, const char* close) {
  TSString* s = ts_to_string(text);
  if (!s || !s->data) {
    return ts_value_string(ts_string_new(""));
  }
  TSString* openStr = ts_string_new(open);
  TSString* closeStr = ts_string_new(close);
  TSString* result = ts_string_concat(openStr, s);
  TSString* final = ts_string_concat(result, closeStr);
  ts_string_free(openStr);
  ts_string_free(closeStr);
  ts_string_free(result);
  return ts_value_string(final);
}

/* Generic style wrapper */
static Value chalk_style(Value text, const char* open, const char* close) {
  return wrap_ansi(text, open, close);
}

/* Basic colors */
Value node_chalk_red(Value text) { return chalk_style(text, "\033[31m", "\033[39m"); }
Value node_chalk_green(Value text) { return chalk_style(text, "\033[32m", "\033[39m"); }
Value node_chalk_blue(Value text) { return chalk_style(text, "\033[34m", "\033[39m"); }
Value node_chalk_yellow(Value text) { return chalk_style(text, "\033[33m", "\033[39m"); }
Value node_chalk_magenta(Value text) { return chalk_style(text, "\033[35m", "\033[39m"); }
Value node_chalk_cyan(Value text) { return chalk_style(text, "\033[36m", "\033[39m"); }
Value node_chalk_white(Value text) { return chalk_style(text, "\033[37m", "\033[39m"); }
Value node_chalk_gray(Value text) { return chalk_style(text, "\033[90m", "\033[39m"); }
Value node_chalk_grey(Value text) { return chalk_style(text, "\033[90m", "\033[39m"); }
Value node_chalk_black(Value text) { return chalk_style(text, "\033[30m", "\033[39m"); }

/* Bright colors */
Value node_chalk_redBright(Value text) { return chalk_style(text, "\033[91m", "\033[39m"); }
Value node_chalk_greenBright(Value text) { return chalk_style(text, "\033[92m", "\033[39m"); }
Value node_chalk_blueBright(Value text) { return chalk_style(text, "\033[94m", "\033[39m"); }
Value node_chalk_yellowBright(Value text) { return chalk_style(text, "\033[93m", "\033[39m"); }
Value node_chalk_magentaBright(Value text) { return chalk_style(text, "\033[95m", "\033[39m"); }
Value node_chalk_cyanBright(Value text) { return chalk_style(text, "\033[96m", "\033[39m"); }
Value node_chalk_whiteBright(Value text) { return chalk_style(text, "\033[97m", "\033[39m"); }

/* Background colors */
Value node_chalk_bgRed(Value text) { return chalk_style(text, "\033[41m", "\033[49m"); }
Value node_chalk_bgGreen(Value text) { return chalk_style(text, "\033[42m", "\033[49m"); }
Value node_chalk_bgBlue(Value text) { return chalk_style(text, "\033[44m", "\033[49m"); }
Value node_chalk_bgYellow(Value text) { return chalk_style(text, "\033[43m", "\033[49m"); }
Value node_chalk_bgMagenta(Value text) { return chalk_style(text, "\033[45m", "\033[49m"); }
Value node_chalk_bgCyan(Value text) { return chalk_style(text, "\033[46m", "\033[49m"); }
Value node_chalk_bgWhite(Value text) { return chalk_style(text, "\033[47m", "\033[49m"); }
Value node_chalk_bgBlack(Value text) { return chalk_style(text, "\033[40m", "\033[49m"); }

/* Modifiers */
Value node_chalk_bold(Value text) { return chalk_style(text, "\033[1m", "\033[22m"); }
Value node_chalk_dim(Value text) { return chalk_style(text, "\033[2m", "\033[22m"); }
Value node_chalk_italic(Value text) { return chalk_style(text, "\033[3m", "\033[23m"); }
Value node_chalk_underline(Value text) { return chalk_style(text, "\033[4m", "\033[24m"); }
Value node_chalk_strikethrough(Value text) { return chalk_style(text, "\033[9m", "\033[29m"); }
Value node_chalk_visible(Value text) { return text; }
Value node_chalk_reset(Value text) { return chalk_style(text, "\033[0m", ""); }

/* Parse hex color string like "#FF0000" or "FF0000" to 24-bit value */
static int parse_hex_color(const char* hex, int* r, int* g, int* b) {
  if (!hex) return 0;
  if (hex[0] == '#') hex++;
  if (strlen(hex) != 6) return 0;
  char* end;
  long val = strtol(hex, &end, 16);
  if (*end != '\0' || val < 0 || val > 0xFFFFFF) return 0;
  *r = (val >> 16) & 0xFF;
  *g = (val >> 8) & 0xFF;
  *b = val & 0xFF;
  return 1;
}

/* Extended color functions */
Value node_chalk_hex(Value color, Value text) {
  TSString* cs = ts_to_string(color);
  if (!cs || !cs->data) return text;
  int r, g, b;
  if (!parse_hex_color(cs->data, &r, &g, &b)) return text;
  char open[32], close[32];
  snprintf(open, sizeof(open), "\033[38;2;%d;%d;%dm", r, g, b);
  snprintf(close, sizeof(close), "\033[39m");
  return wrap_ansi(text, open, close);
}

Value node_chalk_rgb(Value r, Value g, Value b, Value text) {
  int ri = (int)ts_to_number(r);
  int gi = (int)ts_to_number(g);
  int bi = (int)ts_to_number(b);
  if (ri < 0) ri = 0; if (ri > 255) ri = 255;
  if (gi < 0) gi = 0; if (gi > 255) gi = 255;
  if (bi < 0) bi = 0; if (bi > 255) bi = 255;
  char open[32], close[32];
  snprintf(open, sizeof(open), "\033[38;2;%d;%d;%dm", ri, gi, bi);
  snprintf(close, sizeof(close), "\033[39m");
  return wrap_ansi(text, open, close);
}

Value node_chalk_ansi256(Value code, Value text) {
  int c = (int)ts_to_number(code);
  if (c < 0) c = 0; if (c > 255) c = 255;
  char open[32], close[32];
  snprintf(open, sizeof(open), "\033[38;5;%dm", c);
  snprintf(close, sizeof(close), "\033[39m");
  return wrap_ansi(text, open, close);
}

Value node_chalk_bgHex(Value color, Value text) {
  TSString* cs = ts_to_string(color);
  if (!cs || !cs->data) return text;
  int r, g, b;
  if (!parse_hex_color(cs->data, &r, &g, &b)) return text;
  char open[32], close[32];
  snprintf(open, sizeof(open), "\033[48;2;%d;%d;%dm", r, g, b);
  snprintf(close, sizeof(close), "\033[49m");
  return wrap_ansi(text, open, close);
}

Value node_chalk_bgRgb(Value r, Value g, Value b, Value text) {
  int ri = (int)ts_to_number(r);
  int gi = (int)ts_to_number(g);
  int bi = (int)ts_to_number(b);
  if (ri < 0) ri = 0; if (ri > 255) ri = 255;
  if (gi < 0) gi = 0; if (gi > 255) gi = 255;
  if (bi < 0) bi = 0; if (bi > 255) bi = 255;
  char open[32], close[32];
  snprintf(open, sizeof(open), "\033[48;2;%d;%d;%dm", ri, gi, bi);
  snprintf(close, sizeof(close), "\033[49m");
  return wrap_ansi(text, open, close);
}

/* Properties */
Value node_chalk_level(void) {
  /* Level 3 = truecolor (24-bit) support */
  return ts_value_number(3);
}

Value node_chalk_enabled(void) {
  return ts_value_boolean(1);
}
