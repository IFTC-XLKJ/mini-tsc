#include "node_assert.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char* msg_or(Value message, const char* fallback) {
  if (message.tag == TAG_NULL) return fallback;
  TSString* s = ts_to_string(message);
  if (s && s->data && s->data[0]) return s->data;
  return fallback;
}

static void assert_fail(const char* msg) {
  fprintf(stderr, "AssertionError [ERR_ASSERTION]: %s\n", msg ? msg : "assertion failed");
  fflush(stderr);
  TS_THROW(ts_value_string(ts_string_new(msg ? msg : "AssertionError")));
}

static int values_strict_equal(Value a, Value b) {
  if (a.tag != b.tag) {
    /* null and undefined both TAG_NULL in this runtime */
    return 0;
  }
  switch (a.tag) {
    case TAG_NUMBER:
      if (isnan(a.as.number) && isnan(b.as.number)) return 1;
      return a.as.number == b.as.number;
    case TAG_BOOLEAN:
      return a.as.boolean == b.as.boolean;
    case TAG_STRING:
      if (!a.as.string || !b.as.string) return a.as.string == b.as.string;
      return ts_string_equals(a.as.string, b.as.string);
    case TAG_NULL:
      return 1;
    case TAG_FUNCTION:
      return a.as.function == b.as.function;
    case TAG_SYMBOL:
      return a.as.symbol == b.as.symbol;
    case TAG_ARRAY:
    case TAG_OBJECT:
      /* strictEqual does not deep-compare objects */
      return a.as.object == b.as.object || a.as.array == b.as.array;
    default:
      return 0;
  }
}

/* Loose equal: coerce via string/number loosely */
static int values_loose_equal(Value a, Value b) {
  if (a.tag == b.tag) return values_strict_equal(a, b);
  /* number vs string */
  if ((a.tag == TAG_NUMBER && b.tag == TAG_STRING) ||
      (a.tag == TAG_STRING && b.tag == TAG_NUMBER)) {
    return ts_to_number(a) == ts_to_number(b);
  }
  if ((a.tag == TAG_BOOLEAN && b.tag == TAG_NUMBER) ||
      (a.tag == TAG_NUMBER && b.tag == TAG_BOOLEAN)) {
    return ts_to_number(a) == ts_to_number(b);
  }
  if (a.tag == TAG_NULL || b.tag == TAG_NULL) {
    return (a.tag == TAG_NULL && b.tag == TAG_NULL);
  }
  TSString* sa = ts_to_string(a);
  TSString* sb = ts_to_string(b);
  if (sa && sb) return ts_string_equals(sa, sb);
  return 0;
}

static int deep_equal_impl(Value a, Value b, int strict);

static int arrays_deep_equal(TSArray* aa, TSArray* bb, int strict) {
  if (!aa && !bb) return 1;
  if (!aa || !bb) return 0;
  if (aa->length != bb->length) return 0;
  for (int i = 0; i < aa->length; i++) {
    if (!deep_equal_impl(ts_array_get(aa, i), ts_array_get(bb, i), strict)) return 0;
  }
  return 1;
}

typedef struct {
  TSHashMap* other;
  int strict;
  int ok;
} DeepMapCtx;

static void deep_map_check(TSString* key, Value value, void* user) {
  DeepMapCtx* ctx = (DeepMapCtx*)user;
  if (!ctx->ok) return;
  if (!ts_hashmap_has(ctx->other, key)) {
    ctx->ok = 0;
    return;
  }
  Value ov = ts_hashmap_get(ctx->other, key);
  if (!deep_equal_impl(value, ov, ctx->strict)) ctx->ok = 0;
}

static int maps_deep_equal(TSHashMap* a, TSHashMap* b, int strict) {
  if (!a && !b) return 1;
  if (!a || !b) return 0;
  DeepMapCtx ctx1 = { .other = b, .strict = strict, .ok = 1 };
  ts_hashmap_for_each(a, deep_map_check, &ctx1);
  if (!ctx1.ok) return 0;
  DeepMapCtx ctx2 = { .other = a, .strict = strict, .ok = 1 };
  ts_hashmap_for_each(b, deep_map_check, &ctx2);
  return ctx2.ok;
}

static int deep_equal_impl(Value a, Value b, int strict) {
  if (a.tag == TAG_ARRAY && b.tag == TAG_ARRAY) {
    return arrays_deep_equal(a.as.array, b.as.array, strict);
  }
  if (a.tag == TAG_OBJECT && b.tag == TAG_OBJECT) {
    return maps_deep_equal((TSHashMap*)a.as.object, (TSHashMap*)b.as.object, strict);
  }
  if (strict) return values_strict_equal(a, b);
  return values_loose_equal(a, b);
}

static void fail_eq(const char* op, Value actual, Value expected, Value message, const char* fallback) {
  if (message.tag != TAG_NULL) {
    assert_fail(msg_or(message, fallback));
    return;
  }
  TSString* sa = ts_to_string(actual);
  TSString* se = ts_to_string(expected);
  char buf[1024];
  snprintf(buf, sizeof(buf), "%s\n+ actual - expected\n+ '%s'\n- '%s'",
           fallback,
           sa && sa->data ? sa->data : "?",
           se && se->data ? se->data : "?");
  (void)op;
  assert_fail(buf);
}

void node_assert_ok(Value value, Value message) {
  if (!ts_to_boolean(value)) {
    assert_fail(msg_or(message, "The expression evaluated to a falsy value"));
  }
}

void node_assert_assert(Value value, Value message) {
  node_assert_ok(value, message);
}

void node_assert_equal(Value actual, Value expected, Value message) {
  if (!values_loose_equal(actual, expected)) {
    fail_eq("==", actual, expected, message, "Expected values to be loosely equal");
  }
}

void node_assert_notEqual(Value actual, Value expected, Value message) {
  if (values_loose_equal(actual, expected)) {
    assert_fail(msg_or(message, "Expected values to be loosely unequal"));
  }
}

void node_assert_strictEqual(Value actual, Value expected, Value message) {
  if (!values_strict_equal(actual, expected)) {
    fail_eq("===", actual, expected, message, "Expected values to be strictly equal");
  }
}

void node_assert_notStrictEqual(Value actual, Value expected, Value message) {
  if (values_strict_equal(actual, expected)) {
    assert_fail(msg_or(message, "Expected values to be strictly unequal"));
  }
}

void node_assert_deepEqual(Value actual, Value expected, Value message) {
  if (!deep_equal_impl(actual, expected, 0)) {
    fail_eq("deepEqual", actual, expected, message, "Expected values to be deeply equal");
  }
}

void node_assert_deepStrictEqual(Value actual, Value expected, Value message) {
  if (!deep_equal_impl(actual, expected, 1)) {
    fail_eq("deepStrictEqual", actual, expected, message, "Expected values to be deeply and strictly equal");
  }
}

void node_assert_notDeepEqual(Value actual, Value expected, Value message) {
  if (deep_equal_impl(actual, expected, 0)) {
    assert_fail(msg_or(message, "Expected values not to be deeply equal"));
  }
}

void node_assert_notDeepStrictEqual(Value actual, Value expected, Value message) {
  if (deep_equal_impl(actual, expected, 1)) {
    assert_fail(msg_or(message, "Expected values not to be deeply and strictly equal"));
  }
}

void node_assert_fail(Value message) {
  assert_fail(msg_or(message, "Failed"));
}

void node_assert_ifError(Value value) {
  if (value.tag == TAG_NULL) return;
  if (value.tag == TAG_BOOLEAN && !value.as.boolean) return;
  TSString* s = ts_to_string(value);
  assert_fail(s && s->data ? s->data : "ifError got truthy value");
}

typedef Value (*ZeroArgFn)(void);

void node_assert_throws(Value fn, Value errorOrMessage) {
  if (fn.tag != TAG_FUNCTION || !fn.as.function) {
    assert_fail("The \"fn\" argument must be of type function");
    return;
  }
  ZeroArgFn f = (ZeroArgFn)fn.as.function;
  int threw = 0;
  TS_TRY {
    f();
  } TS_CATCH {
    threw = 1;
  }
  if (!threw) {
    assert_fail(msg_or(errorOrMessage, "Missing expected exception"));
  }
}

void node_assert_doesNotThrow(Value fn, Value message) {
  if (fn.tag != TAG_FUNCTION || !fn.as.function) {
    assert_fail("The \"fn\" argument must be of type function");
    return;
  }
  ZeroArgFn f = (ZeroArgFn)fn.as.function;
  int threw = 0;
  TS_TRY {
    f();
  } TS_CATCH {
    threw = 1;
  }
  if (threw) {
    assert_fail(msg_or(message, "Got unwanted exception"));
  }
}

/* Simple substring match when "regexp" is a string; full RegExp not available. */
void node_assert_match(Value str, Value regexp, Value message) {
  TSString* s = ts_to_string(str);
  TSString* pat = ts_to_string(regexp);
  if (!s || !s->data || !pat || !pat->data) {
    assert_fail(msg_or(message, "The input did not match the regular expression"));
    return;
  }
  if (strstr(s->data, pat->data) == NULL) {
    assert_fail(msg_or(message, "The input did not match the regular expression"));
  }
}

void node_assert_doesNotMatch(Value str, Value regexp, Value message) {
  TSString* s = ts_to_string(str);
  TSString* pat = ts_to_string(regexp);
  if (!s || !s->data || !pat || !pat->data) return;
  if (strstr(s->data, pat->data) != NULL) {
    assert_fail(msg_or(message, "The input was expected to not match the regular expression"));
  }
}
