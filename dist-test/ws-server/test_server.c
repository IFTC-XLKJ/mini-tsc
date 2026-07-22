#include "test_server.h"

#include "node_http.h"
#include "node_path.h"
#include "node_process.h"
#include "node_fs.h"

static int __init_done_test_server = 0;

Value __closure_server_2(Value writer, Value i);
Value __closure_server_3(Value wss, Value event);
Value __closure_server_4(Value event);
Value __closure_server_5(Value event);
Value __closure_server_0(Value req);
Value __closure_server_6();
void test_server_entry();
double randomInt(double min, double max);

Value __closure_server_2(Value writer, Value i) {
  ts_writable_stream_write(writer, ts_value_string(ts_string_concat(ts_string_concat(ts_string_concat(ts_string_concat(ts_string_new("id: "), ts_to_string(i)), ts_string_new("\nevent: tick\ndata: chunk ")), ts_to_string(i)), ts_string_new("\n\n"))));
  if ((ts_to_number(i) == (double)(5))) {
  ts_writable_stream_close(writer);
}
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_server_3(Value wss, Value event) {
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("Received message:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)event.as.object), ts_string_new("data"))))}, 2);
  ts_websocket_send(wss, ts_value_string(ts_string_concat(ts_string_new("Echo: "), ts_to_string(ts_hashmap_get(((TSHashMap*)event.as.object), ts_string_new("data"))))));
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_server_4(Value event) {
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("Closed:")), event}, 2);
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_server_5(Value event) {
  ts_console_error_multi((Value[]){ts_value_string(ts_string_new("Error:")), ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)event.as.object), ts_string_new("type"))))}, 2);
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_server_0(Value req) {
  ts_console_log(ts_value_string(ts_to_string(ts_hashmap_get(((TSHashMap*)req.as.object), ts_string_new("url")))));
  TSString* host = ({ Value __or_l = ts_hashmap_get(((TSHashMap*)ts_hashmap_get(((TSHashMap*)req.as.object), ts_string_new("headers")).as.object), ts_string_new("host")); TSString* __or_r = ts_to_boolean(__or_l) ? ts_to_string(__or_l) : ts_string_new("localhost"); __or_r; /*__ts_str*/ });
  Value url = ts_url_new(ts_string_concat(ts_string_concat(ts_string_new("http://"), host), ({ Value __or_l = ts_hashmap_get(((TSHashMap*)req.as.object), ts_string_new("url")); TSString* __or_r = ts_to_boolean(__or_l) ? ts_to_string(__or_l) : ts_string_new("/"); __or_r; /*__ts_str*/ })));
  ts_console_log(ts_value_string(ts_url_pathname(url)));
  if (ts_string_equals(ts_url_pathname(url), ts_string_new("/test-file"))) {
  Value file = ts_await(node_fs_readFile(node_path_join((Value[]){ts_value_string(ts_string_new(__ts_dirname)), ts_value_string(ts_string_new("../mini-tsc.zip"))}, 2), ts_value_null()));
  ts_console_log_multi((Value[]){file, ts_value_number((double)(ts_buffer_length(file)))}, 2);
  return ts_response_new(file, ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("headers"), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("Content-Type"), ts_value_string(ts_string_new("application/zip")));
  ts_hashmap_set(map, ts_string_new("Content-Disposition"), ts_value_string(ts_string_new("attachment; filename=mini-tsc.zip"))); map; }))); map; })));
}
  if (ts_string_equals(ts_url_pathname(url), ts_string_new("/test-stream"))) {
  Value writableStream = ts_writable_stream_new();
  Value __destruct_1 = writableStream;
  Value getWriter = ts_writable_stream_get_writer(__destruct_1);
  Value writer = ts_value_call0(getWriter);
  if (!ts_to_boolean(writer)) {
  return ts_response_new(ts_value_string(ts_string_new("No body writer available")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("status"), ts_value_number(500)); map; })));
}
  for (double i = 1; i <= 5; i++) {
  ts_set_timeout(ts_bind_function((void*)__closure_server_2, (Value[]){writer, ts_value_number(i)}, 2), ts_value_number(i * randomInt(1000, 5000)), NULL, 0);
}
  return ts_response_new(writer, ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("headers"), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("Content-Type"), ts_value_string(ts_string_new("text/event-stream"))); map; }))); map; })));
}
  if (ts_string_equals(ts_url_pathname(url), ts_string_new("/test-websocket"))) {
  if (!ts_string_equals(ts_to_string(ts_hashmap_get(((TSHashMap*)ts_hashmap_get(((TSHashMap*)req.as.object), ts_string_new("headers")).as.object), ts_string_new("upgrade"))), ts_string_new("websocket"))) {
  return ts_response_new(ts_value_null(), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("status"), ts_value_number(426)); map; })));
}
  Value wss = ts_websocket_server_new();
  ts_websocket_set_handler(wss, ts_string_new("onmessage"), ts_bind_function((void*)__closure_server_3, (Value[]){wss}, 1));
  ts_websocket_set_handler(wss, ts_string_new("onclose"), ts_value_function((void*)__closure_server_4));
  ts_websocket_set_handler(wss, ts_string_new("onerror"), ts_value_function((void*)__closure_server_5));
  return ts_response_new(wss, ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("headers"), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("Sec-WebSocket-Protocol"), ts_value_string(ts_string_new("chat, superchat"))); map; }))); map; })));
}
  return ts_response_new(ts_value_string(ts_string_new("Hello, World!")), ts_value_null());
 return ts_value_undefined(); }

Value __closure_server_6() {
  puts("Server is running on port 3000");
  return ts_value_undefined();
 return ts_value_undefined(); }

void test_server_entry() {
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new(__ts_dirname)), ts_value_string(ts_string_new(__ts_filename))}, 2);
  puts("Starting server...");
  Value server = node_http_createServer(ts_value_function((void*)__closure_server_0));
  node_http_server_listen(server, ts_value_number(3000), ts_value_function((void*)__closure_server_6));
}

double randomInt(double min, double max) {
  return ts_math_floor(ts_math_random() * (max - min + 1)) + min;
 return (double){0}; }

void __init_test_server(void) {
  if (__init_done_test_server) return;
  __init_done_test_server = 1;
}
