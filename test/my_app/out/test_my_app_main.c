#include "test_my_app_main.h"

#include "node_webview.h"
#include "node_process.h"

static int __init_done_test_my_app_main = 0;

Value __closure_main_0();
void test_my_app_main_entry();
extern Value node_webview_WebView(Value);

Value __closure_main_0() {
  puts("WebView ready");
  return ts_value_undefined();
 return ts_value_undefined(); }

void test_my_app_main_entry() {
  puts("Hello from my_app!");
  Value webView = node_webview_WebView(ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("url"), ts_value_string(ts_string_new("https://iftc.koyeb.app/mini-tsc")));
  ts_hashmap_set(map, ts_string_new("width"), ts_value_number(800));
  ts_hashmap_set(map, ts_string_new("height"), ts_value_number(600));
  ts_hashmap_set(map, ts_string_new("title"), ts_value_string(ts_string_new("My App")));
  ts_hashmap_set(map, ts_string_new("icon"), ts_value_string(ts_string_new("icon.ico")));
  ts_hashmap_set(map, ts_string_new("show"), ts_value_boolean(1));
  ts_hashmap_set(map, ts_string_new("center"), ts_value_boolean(1));
  ts_hashmap_set(map, ts_string_new("frame"), ts_value_boolean(0));
  ts_hashmap_set(map, ts_string_new("transparent"), ts_value_boolean(1));
  ts_hashmap_set(map, ts_string_new("devTools"), ts_value_boolean(1)); map; })));
  node_webview_run(webView);
  node_events_on(webView, ts_value_string(ts_string_new("ready")), ts_value_function((void*)__closure_main_0));
  node_webview_executeJavaScript(webView, ts_value_string(ts_string_new("console.log('Hello from my_app!');")));
  Value webView2 = node_webview_WebView(ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("url"), ts_value_string(ts_string_new("https://iftc.koyeb.app/mini-tsc")));
  ts_hashmap_set(map, ts_string_new("width"), ts_value_number(800));
  ts_hashmap_set(map, ts_string_new("height"), ts_value_number(600));
  ts_hashmap_set(map, ts_string_new("title"), ts_value_string(ts_string_new("My App")));
  ts_hashmap_set(map, ts_string_new("icon"), ts_value_string(ts_string_new("icon.ico")));
  ts_hashmap_set(map, ts_string_new("show"), ts_value_boolean(1));
  ts_hashmap_set(map, ts_string_new("center"), ts_value_boolean(1));
  ts_hashmap_set(map, ts_string_new("frame"), ts_value_boolean(0));
  ts_hashmap_set(map, ts_string_new("transparent"), ts_value_boolean(1));
  ts_hashmap_set(map, ts_string_new("devTools"), ts_value_boolean(1)); map; })));
  node_webview_run(webView2);
  node_webview_executeJavaScript(webView2, ts_value_string(ts_string_new("console.log('Hello from my_app!');")));
}

void __init_test_my_app_main(void) {
  if (__init_done_test_my_app_main) return;
  __init_done_test_my_app_main = 1;
}
