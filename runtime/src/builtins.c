#include "runtime.h"
#include "ts_features.h"
#include <time.h>
#include <math.h>
#include <ctype.h>

/* Math builtins */
#if defined(TS_NEED_MATH)
Value ts_math_random(void) {
  return ts_value_number((double)rand() / (double)RAND_MAX);
}

double ts_math_floor(double x) {
  return floor(x);
}

double ts_math_ceil(double x) {
  return ceil(x);
}

double ts_math_round(double x) {
  return round(x);
}

double ts_math_abs(double x) {
  return fabs(x);
}

double ts_math_sqrt(double x) {
  return sqrt(x);
}

double ts_math_pow(double base, double exp) {
  return pow(base, exp);
}

double ts_math_max(double a, double b) {
  return a > b ? a : b;
}

double ts_math_min(double a, double b) {
  return a < b ? a : b;
}

double ts_math_log(double x) {
  return log(x);
}

double ts_math_log2(double x) {
  return log2(x);
}

double ts_math_log10(double x) {
  return log10(x);
}

double ts_math_sin(double x) { return sin(x); }
double ts_math_cos(double x) { return cos(x); }
double ts_math_tan(double x) { return tan(x); }
double ts_math_asin(double x) { return asin(x); }
double ts_math_acos(double x) { return acos(x); }
double ts_math_atan(double x) { return atan(x); }
double ts_math_atan2(double y, double x) { return atan2(y, x); }
#endif /* TS_NEED_MATH */

/* Date.now — only when Date feature is used */
#if defined(TS_NEED_DATE)
#ifdef _WIN32
#include <windows.h>
double ts_date_now(void) {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER li;
  li.LowPart = ft.dwLowDateTime;
  li.HighPart = ft.dwHighDateTime;
  /* FILETIME is in 100-nanosecond intervals since 1601-01-01 */
  /* Convert to milliseconds since 1970-01-01 */
  return (double)((li.QuadPart - 116444736000000000ULL) / 10000ULL);
}
#else
double ts_date_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}
#endif
#endif /* TS_NEED_DATE */

/* parseInt / parseFloat */
#if defined(TS_NEED_PARSE)
double ts_parse_int(TSString* str, int radix) {
  return (double)strtol(str->data, NULL, radix);
}

double ts_parse_float(TSString* str) {
  return atof(str->data);
}
#endif

/* isNaN, isFinite — tiny; keep always (often used with numbers) */
int ts_is_nan(double x) {
  return isnan(x);
}

int ts_is_finite(double x) {
  return isfinite(x);
}

/* Console timer */
#if defined(TS_NEED_CONSOLE_TIME)
typedef struct TimerEntry {
  TSString* label;
  clock_t start;
  struct TimerEntry* next;
} TimerEntry;

static TimerEntry* timer_list = NULL;

void ts_console_time(TSString* label) {
  TimerEntry* entry = (TimerEntry*)malloc(sizeof(TimerEntry));
  entry->label = label;
  entry->start = clock();
  entry->next = timer_list;
  timer_list = entry;
}

void ts_console_time_end(TSString* label) {
  TimerEntry** prev = &timer_list;
  while (*prev) {
    if (ts_string_equals((*prev)->label, label)) {
      TimerEntry* entry = *prev;
      *prev = entry->next;
      clock_t elapsed = clock() - entry->start;
      double ms = (double)elapsed / CLOCKS_PER_SEC * 1000.0;
      printf("%s: %.3fms\n", label->data, ms);
      free(entry);
      return;
    }
    prev = &(*prev)->next;
  }
  printf("%s: 0.000ms\n", label->data);
}
#endif /* TS_NEED_CONSOLE_TIME */

/* String methods live in string_ops.c (always linked with core runtime). */

/* ---------- setTimeout / setInterval ---------- */
#if defined(TS_NEED_TIMERS)

#ifdef _WIN32
#include <windows.h>
static double ts_now_ms(void) {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER li;
  li.LowPart = ft.dwLowDateTime;
  li.HighPart = ft.dwHighDateTime;
  return (double)((li.QuadPart - 116444736000000000ULL) / 10000ULL);
}
static void ts_sleep_ms(int ms) {
  if (ms > 0) Sleep((DWORD)ms);
}
#else
#include <unistd.h>
#include <sys/time.h>
static double ts_now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}
static void ts_sleep_ms(int ms) {
  if (ms > 0) usleep((useconds_t)ms * 1000);
}
#endif

typedef Value (*TimerCallback)(Value a0, Value a1, Value a2, Value a3);

typedef struct TsTimer {
  int id;
  int active;
  int repeating; /* 1 = setInterval */
  double next_fire_ms;
  double interval_ms;
  Value callback;
  Value args[4];
  int argc;
  struct TsTimer* next;
} TsTimer;

static TsTimer* g_timers = NULL;
static int g_next_timer_id = 1;

static void ts_timer_call(TsTimer* t) {
  if (t->callback.tag != TAG_FUNCTION || !t->callback.as.function) return;
  TimerCallback fn = (TimerCallback)t->callback.as.function;
  Value a0 = (t->argc > 0) ? t->args[0] : ts_value_undefined();
  Value a1 = (t->argc > 1) ? t->args[1] : ts_value_undefined();
  Value a2 = (t->argc > 2) ? t->args[2] : ts_value_undefined();
  Value a3 = (t->argc > 3) ? t->args[3] : ts_value_undefined();
  fn(a0, a1, a2, a3);
}

static double ts_timer_add(Value callback, Value delayMs, Value* args, int argc, int repeating) {
  double delay = ts_to_number(delayMs);
  if (delay < 0 || delay != delay) delay = 0; /* NaN → 0 */
  TsTimer* t = (TsTimer*)malloc(sizeof(TsTimer));
  if (!t) return 0;
  t->id = g_next_timer_id++;
  t->active = 1;
  t->repeating = repeating;
  t->interval_ms = delay;
  t->next_fire_ms = ts_now_ms() + delay;
  t->callback = callback;
  t->argc = argc > 4 ? 4 : (argc < 0 ? 0 : argc);
  for (int i = 0; i < t->argc; i++) t->args[i] = args[i];
  for (int i = t->argc; i < 4; i++) t->args[i] = ts_value_undefined();
  t->next = g_timers;
  g_timers = t;
  return (double)t->id;
}

static void ts_timer_clear(Value idVal) {
  int id = (int)ts_to_number(idVal);
  for (TsTimer* t = g_timers; t; t = t->next) {
    if (t->id == id) {
      t->active = 0;
      return;
    }
  }
}

double ts_set_timeout(Value callback, Value delayMs, Value* args, int argc) {
  return ts_timer_add(callback, delayMs, args, argc, 0);
}

double ts_set_interval(Value callback, Value delayMs, Value* args, int argc) {
  return ts_timer_add(callback, delayMs, args, argc, 1);
}

void ts_clear_timeout(Value id) { ts_timer_clear(id); }
void ts_clear_interval(Value id) { ts_timer_clear(id); }

int ts_timers_pending(void) {
  for (TsTimer* t = g_timers; t; t = t->next) {
    if (t->active) return 1;
  }
  return 0;
}

void ts_timers_run(void) {
  /* Simple single-threaded event loop: fire due timers until none remain. */
  while (ts_timers_pending()) {
    double now = ts_now_ms();
    double next = -1;
    int fired = 0;

    for (TsTimer* t = g_timers; t; t = t->next) {
      if (!t->active) continue;
      if (t->next_fire_ms <= now + 0.5) {
        /* Fire */
        if (t->repeating) {
          ts_timer_call(t);
          if (t->active) {
            /* reschedule from now to avoid drift pile-up under load */
            t->next_fire_ms = ts_now_ms() + t->interval_ms;
          }
        } else {
          t->active = 0;
          ts_timer_call(t);
        }
        fired = 1;
      } else {
        if (next < 0 || t->next_fire_ms < next) next = t->next_fire_ms;
      }
    }

    if (!ts_timers_pending()) break;
    if (!fired && next > 0) {
      int sleep_ms = (int)(next - ts_now_ms());
      if (sleep_ms < 1) sleep_ms = 1;
      if (sleep_ms > 100) sleep_ms = 100; /* wake periodically */
      ts_sleep_ms(sleep_ms);
    } else if (!fired) {
      ts_sleep_ms(1);
    }
  }

  /* Free inactive timers */
  TsTimer** prev = &g_timers;
  while (*prev) {
    TsTimer* t = *prev;
    if (!t->active) {
      *prev = t->next;
      free(t);
    } else {
      prev = &t->next;
    }
  }
}

#else /* !TS_NEED_TIMERS */

double ts_set_timeout(Value callback, Value delayMs, Value* args, int argc) {
  (void)callback; (void)delayMs; (void)args; (void)argc;
  return 0;
}
double ts_set_interval(Value callback, Value delayMs, Value* args, int argc) {
  (void)callback; (void)delayMs; (void)args; (void)argc;
  return 0;
}
void ts_clear_timeout(Value id) { (void)id; }
void ts_clear_interval(Value id) { (void)id; }
void ts_timers_run(void) {}
int ts_timers_pending(void) { return 0; }

#endif /* TS_NEED_TIMERS */

/* ---------- alert / confirm / prompt ---------- */
#if defined(TS_NEED_DIALOGS)

static void ts_trim_crlf(char* s) {
  if (!s) return;
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
    s[n - 1] = '\0';
    n--;
  }
}

static int ts_read_line(char* buf, size_t cap) {
  if (!buf || cap == 0) return 0;
  if (!fgets(buf, (int)cap, stdin)) {
    buf[0] = '\0';
    return 0;
  }
  ts_trim_crlf(buf);
  return 1;
}

void ts_alert(Value message) {
  TSString* s = ts_to_string(message);
  const char* msg = (s && s->data) ? s->data : "";
  printf("%s\n", msg);
  fflush(stdout);
  printf("[Press Enter to continue] ");
  fflush(stdout);
  char line[1024];
  (void)ts_read_line(line, sizeof(line));
}

int ts_confirm(Value message) {
  TSString* s = ts_to_string(message);
  const char* msg = (s && s->data) ? s->data : "";
  for (;;) {
    printf("%s [y/N] ", msg);
    fflush(stdout);
    char line[64];
    if (!ts_read_line(line, sizeof(line))) {
      /* EOF — treat as cancel */
      return 0;
    }
    /* empty or whitespace-only → re-prompt */
    char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') continue;
    /* single y/Y or n/N (allow trailing spaces) */
    char c = (char)tolower((unsigned char)*p);
    char* rest = p + 1;
    while (*rest == ' ' || *rest == '\t') rest++;
    if (*rest != '\0') continue; /* extra junk → re-prompt */
    if (c == 'y') return 1;
    if (c == 'n') return 0;
    /* other single char → re-prompt */
  }
}

Value ts_prompt(Value message) {
  TSString* s = ts_to_string(message);
  const char* msg = (s && s->data) ? s->data : "";
  printf("%s", msg);
  /* add space after prompt unless message already ends with whitespace */
  size_t len = strlen(msg);
  if (len == 0 || (msg[len - 1] != ' ' && msg[len - 1] != '\t' && msg[len - 1] != '\n')) {
    printf(" ");
  }
  fflush(stdout);
  char line[4096];
  if (!ts_read_line(line, sizeof(line))) {
    return ts_value_null();
  }
  if (line[0] == '\0') {
    return ts_value_null();
  }
  return ts_value_string(ts_string_new(line));
}

#else /* !TS_NEED_DIALOGS */

void ts_alert(Value message) { (void)message; }
int ts_confirm(Value message) { (void)message; return 0; }
Value ts_prompt(Value message) { (void)message; return ts_value_null(); }

#endif /* TS_NEED_DIALOGS */
