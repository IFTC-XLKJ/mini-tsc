#ifndef NODE_ASSERT_H
#define NODE_ASSERT_H

#include "runtime.h"

void node_assert_ok(Value value, Value message);
void node_assert_equal(Value actual, Value expected, Value message);
void node_assert_notEqual(Value actual, Value expected, Value message);
void node_assert_strictEqual(Value actual, Value expected, Value message);
void node_assert_notStrictEqual(Value actual, Value expected, Value message);
void node_assert_deepEqual(Value actual, Value expected, Value message);
void node_assert_deepStrictEqual(Value actual, Value expected, Value message);
void node_assert_notDeepEqual(Value actual, Value expected, Value message);
void node_assert_notDeepStrictEqual(Value actual, Value expected, Value message);
void node_assert_fail(Value message);
void node_assert_ifError(Value value);
void node_assert_throws(Value fn, Value errorOrMessage);
void node_assert_doesNotThrow(Value fn, Value message);
void node_assert_match(Value str, Value regexp, Value message);
void node_assert_doesNotMatch(Value str, Value regexp, Value message);

/* assert(value) — same as ok, for default-export call style */
void node_assert_assert(Value value, Value message);

#endif /* NODE_ASSERT_H */
