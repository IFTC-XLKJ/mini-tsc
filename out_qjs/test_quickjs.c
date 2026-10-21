#include "test_quickjs.h"

#include "node_quickjs.h"
#include "node_process.h"

static int __init_done_test_quickjs = 0;

TSString* __closure_quickjs_0(Value s);
Value __closure_quickjs_0_tsv(Value a0, Value a1, Value a2, Value a3);
void test_quickjs_entry();

TSString* __closure_quickjs_0(Value s) {
  return ts_string_concat(ts_string_new("ts:"), ts_to_string(s));
 return (TSString*){0}; }

Value __closure_quickjs_0_tsv(Value a0, Value a1, Value a2, Value a3) {
  return ts_value_string(__closure_quickjs_0(a0));
 return ts_value_undefined(); }

void test_quickjs_entry() {
  Value ctx = node_quickjs_createContext();
  ts_console_log(node_quickjs_eval(ctx, ts_value_string(ts_string_new("1 + 2 * 3"))));
  ts_console_log(node_quickjs_eval(ctx, ts_value_string(ts_string_new("'hello ' + 'world'"))));
  Value arr = node_quickjs_eval(ctx, ts_value_string(ts_string_new("[1, 2, 3, 4]")));
  ts_console_log(ts_value_number((double)(((TSArray*)arr.as.object)->length)));
  ts_console_log(ts_value_string(ts_to_string(ts_array_get(((TSArray*)arr.as.object), (int32_t)(0)))));
  ts_console_log(ts_value_string(ts_to_string(ts_array_get(((TSArray*)arr.as.object), (int32_t)(3)))));
  Value obj = node_quickjs_eval(ctx, ts_value_string(ts_string_new("({ name: 'Alice', age: 30 })")));
  ts_console_log(ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)obj.as.object), ts_string_new("name")))));
  ts_console_log(ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)obj.as.object), ts_string_new("age")))));
  node_quickjs_setGlobal(ctx, ts_value_string(ts_string_new("answer")), ts_value_number(42));
  ts_console_log(node_quickjs_eval(ctx, ts_value_string(ts_string_new("answer * 2"))));
  node_quickjs_eval(ctx, ts_value_string(ts_string_new("function double(x) { return x * 2; }")));
  ts_console_log(node_quickjs_eval(ctx, ts_value_string(ts_string_new("double(21)"))));
  node_quickjs_eval(ctx, ts_value_string(ts_string_new("var greeting = 'hi from js';")));
  ts_console_log(node_quickjs_getGlobal(ctx, ts_value_string(ts_string_new("greeting"))));
  Value scale = node_quickjs_eval(ctx, ts_value_string(ts_string_new("(x) => x * 10")));
  ts_console_log(node_quickjs_call(ctx, scale, ts_value_array(ts_array_from_values((Value[]){ts_value_number(4)}, 1))));
  Value join2 = node_quickjs_eval(ctx, ts_value_string(ts_string_new("(a, b) => a + '-' + b")));
  ts_console_log(node_quickjs_call(ctx, join2, ts_value_array(ts_array_from_values((Value[]){ts_value_string(ts_string_new("x")), ts_value_string(ts_string_new("y"))}, 2))));
  Value seven = node_quickjs_eval(ctx, ts_value_string(ts_string_new("() => 7")));
  ts_console_log(node_quickjs_call(ctx, seven, ts_value_array(ts_array_new())));
  Value dbl = node_quickjs_getGlobal(ctx, ts_value_string(ts_string_new("double")));
  ts_console_log(node_quickjs_call(ctx, dbl, ts_value_array(ts_array_from_values((Value[]){ts_value_number(50)}, 1))));
  node_quickjs_setGlobal(ctx, ts_value_string(ts_string_new("tag")), ts_value_function((void*)__closure_quickjs_0_tsv));
  ts_console_log(node_quickjs_eval(ctx, ts_value_string(ts_string_new("tag('a')"))));
  ts_console_log(node_quickjs_eval(ctx, ts_value_string(ts_string_new("[1, 2, 3].map(function (n) { return tag('n' + n); }).join(',')"))));
  TS_TRY {
  node_quickjs_eval(ctx, ts_value_string(ts_string_new("throw new Error('boom');")));
} TS_CATCH {
  Value e = _ts_current_error.error_value;
{
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("caught:")), ts_value_string(ts_to_string(({ Value __em = e; (__em.tag == TAG_STRING) ? __em : ((__em.tag == TAG_OBJECT && __em.as.object) ? ts_hashmap_get((TSHashMap*)__em.as.object, ts_string_new("message")) : ts_value_string(ts_string_new(""))); })))}, 2);
}
}
  Value usage = node_quickjs_getMemoryUsage(ctx);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("usage mallocSize:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)usage.as.object), ts_string_new("mallocSize"))))}, 2);
  node_quickjs_setMemoryLimit(ctx, ts_value_number(256 * 1024 * 1024));
  ts_console_log(node_quickjs_eval(ctx, ts_value_string(ts_string_new("'under limit'"))));
  node_quickjs_setMemoryLimit(ctx, ts_value_number(1024 * 1024));
  int tripped = 0;
  TS_TRY {
  node_quickjs_eval(ctx, ts_value_string(ts_string_new("var a = []; for (var i = 0; i < 1000000; i++) a.push('xxxxxxxxxx');")));
} TS_CATCH {
  Value e = _ts_current_error.error_value;
{
  tripped = 1;
}
}
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("limit tripped:")), ts_value_boolean(tripped)}, 2);
  node_quickjs_setMemoryLimit(ctx, ts_value_number(0));
  ts_console_log(node_quickjs_eval(ctx, ts_value_string(ts_string_new("'unlimited again'"))));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("version:")), node_quickjs_version()}, 2);
  node_quickjs_setRuntimeInfo(ctx, ts_value_string(ts_string_new("mini-tsc-quickjs-test")));
  node_quickjs_setGCThreshold(ctx, ts_value_number(4 * 1024 * 1024));
  node_quickjs_setMaxStackSize(ctx, ts_value_number(4 * 1024 * 1024));
  node_quickjs_gc(ctx);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("after gc:")), node_quickjs_eval(ctx, ts_value_string(ts_string_new("'ok'")))}, 2);
  node_quickjs_setGlobal(ctx, ts_value_string(ts_string_new("answer")), ts_value_number(42));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("hasGlobal answer:")), node_quickjs_hasGlobal(ctx, ts_value_string(ts_string_new("answer")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("typeofGlobal answer:")), node_quickjs_typeofGlobal(ctx, ts_value_string(ts_string_new("answer")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("hasGlobal nope:")), node_quickjs_hasGlobal(ctx, ts_value_string(ts_string_new("nope")))}, 2);
  Value gk = node_quickjs_globalKeys(ctx);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("globalKeys is array:")), node_quickjs_isArray(gk)}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("deleteGlobal answer:")), node_quickjs_deleteGlobal(ctx, ts_value_string(ts_string_new("answer")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("hasGlobal after delete:")), node_quickjs_hasGlobal(ctx, ts_value_string(ts_string_new("answer")))}, 2);
  Value parsed = node_quickjs_parseJSON(ctx, ts_value_string(ts_string_new("{\"a\":1,\"b\":[2,3]}")));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("parseJSON a:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)parsed.as.object), ts_string_new("a"))))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("parseJSON b0:")), ts_value_string(ts_to_string(ts_array_get(((TSArray*)ts_hashmap_get(((TSHashMap*)parsed.as.object), ts_string_new("b")).as.object), (int32_t)(0))))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("stringify:")), node_quickjs_stringify(ctx, parsed)}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("evalJSON:")), node_quickjs_evalJSON(ctx, ts_value_string(ts_string_new("({ x: 1, y: { z: 2 } })")))}, 2);
  node_quickjs_setGlobalJSON(ctx, ts_value_string(ts_string_new("cfg")), parsed);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("getGlobalJSON:")), node_quickjs_getGlobalJSON(ctx, ts_value_string(ts_string_new("cfg")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("roundtrip:")), node_quickjs_eval(ctx, ts_value_string(ts_string_new("cfg.a + cfg.b[1]")))}, 2);
  node_quickjs_eval(ctx, ts_value_string(ts_string_new("function greet(prefix) { return prefix + ':' + this.name; }")));
  Value greet = node_quickjs_getGlobal(ctx, ts_value_string(ts_string_new("greet")));
  Value who = node_quickjs_eval(ctx, ts_value_string(ts_string_new("({ name: 'Bob' })")));
  Value whoRef = node_quickjs_ref(ctx, who);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("apply:")), node_quickjs_apply(ctx, greet, whoRef, ts_value_array(ts_array_from_values((Value[]){ts_value_string(ts_string_new("hi"))}, 1)))}, 2);
  node_quickjs_eval(ctx, ts_value_string(ts_string_new("function Point(x, y) { this.x = x; this.y = y; }")));
  Value Point = node_quickjs_getGlobal(ctx, ts_value_string(ts_string_new("Point")));
  Value pRef = node_quickjs_newInstance(ctx, Point, ts_value_array(ts_array_from_values((Value[]){ts_value_number(3), ts_value_number(4)}, 2)));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("newInstance x:")), node_quickjs_getProperty(ctx, pRef, ts_value_string(ts_string_new("x")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("newInstance y:")), node_quickjs_getProperty(ctx, pRef, ts_value_string(ts_string_new("y")))}, 2);
  Value compiled = node_quickjs_compile(ctx, ts_value_string(ts_string_new("1 + 2 + 3")));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("compile result:")), compiled}, 2);
  Value objRef = node_quickjs_evalRef(ctx, ts_value_string(ts_string_new("({ name: 'Alice', age: 30 })")));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("keys:")), node_quickjs_getKeys(ctx, objRef)}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("values:")), node_quickjs_getValues(ctx, objRef)}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("entries len:")), node_quickjs_arrayLength(ctx, node_quickjs_getEntries(ctx, objRef))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("get name:")), node_quickjs_getProperty(ctx, objRef, ts_value_string(ts_string_new("name")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("has age:")), node_quickjs_hasProperty(ctx, objRef, ts_value_string(ts_string_new("age")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("setProperty:")), node_quickjs_setProperty(ctx, objRef, ts_value_string(ts_string_new("age")), ts_value_number(31))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("get age after set:")), node_quickjs_getProperty(ctx, objRef, ts_value_string(ts_string_new("age")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("delete city:")), node_quickjs_deleteProperty(ctx, objRef, ts_value_string(ts_string_new("age")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("has age after del:")), node_quickjs_hasProperty(ctx, objRef, ts_value_string(ts_string_new("age")))}, 2);
  Value arrRef = node_quickjs_evalRef(ctx, ts_value_string(ts_string_new("[1, 2, 3]")));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("arrayLength:")), node_quickjs_arrayLength(ctx, arrRef)}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("arrayPush:")), node_quickjs_arrayPush(ctx, arrRef, ts_value_number(4))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("get idx3:")), node_quickjs_getProperty(ctx, arrRef, ts_value_number(3))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("deref:")), node_quickjs_deref(ctx, arrRef)}, 2);
  Value made = node_quickjs_createObject(ctx, ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("hello"), ts_value_string(ts_string_new("world"))); map; })));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("createObject:")), node_quickjs_getProperty(ctx, made, ts_value_string(ts_string_new("hello")))}, 2);
  Value madeArr = node_quickjs_createArray(ctx, ts_value_array(ts_array_from_values((Value[]){ts_value_string(ts_string_new("a")), ts_value_string(ts_string_new("b"))}, 2)));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("createArray length:")), node_quickjs_arrayLength(ctx, madeArr)}, 2);
  node_quickjs_releaseHandle(ctx, objRef);
  node_quickjs_releaseHandle(ctx, arrRef);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("typeof number:")), node_quickjs_typeofValue(ts_value_number(1))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("typeof string:")), node_quickjs_typeofValue(ts_value_string(ts_string_new("s")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("typeof fn handle:")), node_quickjs_typeofValue(greet)}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("isArray:")), node_quickjs_isArray(node_quickjs_eval(ctx, ts_value_string(ts_string_new("[1]"))))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("isObject:")), node_quickjs_isObject(objRef)}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("isString:")), node_quickjs_isString(ts_value_string(ts_string_new("x")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("isNumber:")), node_quickjs_isNumber(ts_value_number(5))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("isBoolean:")), node_quickjs_isBoolean(ts_value_boolean(1))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("isCallable:")), node_quickjs_isCallable(greet)}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("toNumber:")), node_quickjs_toNumber(ctx, ts_value_string(ts_string_new("42")))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("toString:")), node_quickjs_toStringValue(ctx, ts_value_number(3.5))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("toBoolean:")), node_quickjs_toBoolean(ctx, ts_value_number(0))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("strictEqual:")), node_quickjs_strictEqual(ctx, ts_value_number(1), ts_value_number(1))}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("strictEqual false:")), node_quickjs_strictEqual(ctx, ts_value_string(ts_string_new("1")), ts_value_number(1))}, 2);
  node_quickjs_setTimeLimit(ctx, ts_value_number(200));
  int timedOut = 0;
  TS_TRY {
  node_quickjs_eval(ctx, ts_value_string(ts_string_new("while (true) {}")));
} TS_CATCH {
  Value e = _ts_current_error.error_value;
{
  timedOut = 1;
}
}
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("time limit tripped:")), ts_value_boolean(timedOut)}, 2);
  node_quickjs_setTimeLimit(ctx, ts_value_number(0));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("limit cleared:")), node_quickjs_getTimeLimit(ctx)}, 2);
  node_quickjs_eval(ctx, ts_value_string(ts_string_new("var order = []; Promise.resolve().then(function () { order.push('micro'); });")));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("pending jobs:")), node_quickjs_runPendingJobs(ctx)}, 2);
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("order:")), node_quickjs_eval(ctx, ts_value_string(ts_string_new("order.join(',')")))}, 2);
  node_quickjs_evalModule(ctx, ts_value_string(ts_string_new("globalThis.modValue = 6 * 7;")));
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("evalModule:")), node_quickjs_getGlobal(ctx, ts_value_string(ts_string_new("modValue")))}, 2);
  node_quickjs_dispose(ctx);
  puts("quickjs tests passed!");
}

void __init_test_quickjs(void) {
  if (__init_done_test_quickjs) return;
  __init_done_test_quickjs = 1;
}
