#include "test_websocket.h"

#include "node_process.h"

static int __init_done_test_websocket = 0;

Value __closure_websocket_0(Value ws);
Value __closure_websocket_1(Value ws, Value event);
Value __closure_websocket_2(Value error);
Value __closure_websocket_3();
void test_websocket_entry();

Value __closure_websocket_0(Value ws) {
  puts("Connected");
  ts_websocket_send(ws, ts_value_string(ts_string_new("Hello, World")));
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_websocket_1(Value ws, Value event) {
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("Received message:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)event.as.object), ts_string_new("data"))))}, 2);
  ts_websocket_close(ws, ts_value_undefined(), ts_value_undefined());
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_websocket_2(Value error) {
  ts_console_error_multi((Value[]){ts_value_string(ts_string_new("Error:")), error}, 2);
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_websocket_3() {
  puts("Closed");
  return ts_value_undefined();
 return ts_value_undefined(); }

void test_websocket_entry() {
  puts("start");
  Value ws = ts_websocket_new(ts_string_new("ws://localhost:3000/test-websocket"));
  ts_websocket_set_handler(ws, ts_string_new("onopen"), ts_bind_function((void*)__closure_websocket_0, (Value[]){ws}, 1));
  ts_websocket_set_handler(ws, ts_string_new("onmessage"), ts_bind_function((void*)__closure_websocket_1, (Value[]){ws}, 1));
  ts_websocket_set_handler(ws, ts_string_new("onerror"), ts_value_function((void*)__closure_websocket_2));
  ts_websocket_set_handler(ws, ts_string_new("onclose"), ts_value_function((void*)__closure_websocket_3));
}

void __init_test_websocket(void) {
  if (__init_done_test_websocket) return;
  __init_done_test_websocket = 1;
}
