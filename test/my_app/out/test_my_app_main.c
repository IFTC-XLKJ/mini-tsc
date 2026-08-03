#include "test_my_app_main.h"

#include "node_webview.h"
#include "node_process.h"

static int __init_done_test_my_app_main = 0;

Value __closure_main_0(Value name);
Value __closure_main_0_tsv(Value a0, Value a1, Value a2, Value a3);
Value __closure_main_1();
Value __closure_main_1_tsv(Value a0, Value a1, Value a2, Value a3);
Value __closure_main_2(Value webView);
Value __closure_main_2_tsv(Value a0, Value a1, Value a2, Value a3);
Value __closure_main_3(Value webView);
Value __closure_main_3_tsv(Value a0, Value a1, Value a2, Value a3);
Value __closure_main_4(Value message);
Value __closure_main_4_tsv(Value a0, Value a1, Value a2, Value a3);
Value __closure_main_5();
Value __closure_main_5_tsv(Value a0, Value a1, Value a2, Value a3);
Value __closure_main_6();
Value __closure_main_6_tsv(Value a0, Value a1, Value a2, Value a3);
Value __closure_main_7(Value title);
Value __closure_main_7_tsv(Value a0, Value a1, Value a2, Value a3);
Value __closure_main_8(Value error);
Value __closure_main_8_tsv(Value a0, Value a1, Value a2, Value a3);
Value __closure_main_9();
Value __closure_main_9_tsv(Value a0, Value a1, Value a2, Value a3);
Value __closure_main_10(Value webView2);
Value __closure_main_10_tsv(Value a0, Value a1, Value a2, Value a3);
void test_my_app_main_entry();
extern Value node_webview_WebView(Value);

Value __closure_main_0(Value name) {
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("Native greet called:")), name}, 2);
  return ts_value_string(ts_string_concat(ts_string_new("Hello, "), ts_string_concat(ts_to_string(name), ts_string_new("!"))));
 return ts_value_undefined(); }

Value __closure_main_0_tsv(Value a0, Value a1, Value a2, Value a3) {
  return __closure_main_0(a0);
 return ts_value_undefined(); }

Value __closure_main_1() {
  puts("Native getEnv called");
  return node_process_env();
 return ts_value_undefined(); }

Value __closure_main_1_tsv(Value a0, Value a1, Value a2, Value a3) {
  return __closure_main_1();
 return ts_value_undefined(); }

Value __closure_main_2(Value webView) {
  puts("WebView ready");
  node_webview_executeJavaScript(webView, ts_value_string(ts_string_new("console.log('Hello from my_app!');")));
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_main_2_tsv(Value a0, Value a1, Value a2, Value a3) {
  return __closure_main_2(a0);
 return ts_value_undefined(); }

Value __closure_main_3(Value webView) {
  puts("WebView loaded");
  node_webview_executeJavaScript(webView, ts_value_string(ts_string_new("if (window.miniTscBridge) { window.miniTscBridge.greet('world').then(r => console.log('greet result:', r)); }")));
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_main_3_tsv(Value a0, Value a1, Value a2, Value a3) {
  return __closure_main_3(a0);
 return ts_value_undefined(); }

Value __closure_main_4(Value message) {
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("WebView message:")), message}, 2);
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_main_4_tsv(Value a0, Value a1, Value a2, Value a3) {
  return __closure_main_4(a0);
 return ts_value_undefined(); }

Value __closure_main_5() {
  puts("WebView navigate");
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_main_5_tsv(Value a0, Value a1, Value a2, Value a3) {
  return __closure_main_5();
 return ts_value_undefined(); }

Value __closure_main_6() {
  puts("WebView resized");
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_main_6_tsv(Value a0, Value a1, Value a2, Value a3) {
  return __closure_main_6();
 return ts_value_undefined(); }

Value __closure_main_7(Value title) {
  ts_console_log_multi((Value[]){ts_value_string(ts_string_new("WebView title:")), title}, 2);
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_main_7_tsv(Value a0, Value a1, Value a2, Value a3) {
  return __closure_main_7(a0);
 return ts_value_undefined(); }

Value __closure_main_8(Value error) {
  ts_console_error_multi((Value[]){ts_value_string(ts_string_new("WebView error:")), error}, 2);
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_main_8_tsv(Value a0, Value a1, Value a2, Value a3) {
  return __closure_main_8(a0);
 return ts_value_undefined(); }

Value __closure_main_9() {
  puts("WebView closed");
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_main_9_tsv(Value a0, Value a1, Value a2, Value a3) {
  return __closure_main_9();
 return ts_value_undefined(); }

Value __closure_main_10(Value webView2) {
  puts("WebView2 loaded");
  node_webview_executeJavaScript(webView2, ts_value_string(ts_string_new("\n            document.body.setAttribute('data-minitsc-drag-region', 'true');\n        ")));
  return ts_value_undefined();
 return ts_value_undefined(); }

Value __closure_main_10_tsv(Value a0, Value a1, Value a2, Value a3) {
  return __closure_main_10(a0);
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
  ts_hashmap_set(map, ts_string_new("shadow"), ts_value_boolean(1));
  ts_hashmap_set(map, ts_string_new("resizable"), ts_value_boolean(0));
  ts_hashmap_set(map, ts_string_new("roundedCorners"), ts_value_boolean(1));
  ts_hashmap_set(map, ts_string_new("devTools"), ts_value_boolean(1)); map; })));
  node_webview_addJavaScriptInterface(webView, ts_value_string(ts_string_new("miniTscBridge")), ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ts_hashmap_set(map, ts_string_new("greet"), ts_value_function((void*)__closure_main_0_tsv));
  ts_hashmap_set(map, ts_string_new("getEnv"), ts_value_function((void*)__closure_main_1_tsv)); map; })));
  node_webview_on(webView, ts_value_string(ts_string_new("ready")), ts_bind_function((void*)__closure_main_2_tsv, (Value[]){webView}, 1));
  node_webview_on(webView, ts_value_string(ts_string_new("load")), ts_bind_function((void*)__closure_main_3_tsv, (Value[]){webView}, 1));
  node_webview_on(webView, ts_value_string(ts_string_new("message")), ts_value_function((void*)__closure_main_4_tsv));
  node_webview_on(webView, ts_value_string(ts_string_new("navigate")), ts_value_function((void*)__closure_main_5_tsv));
  node_webview_on(webView, ts_value_string(ts_string_new("resize")), ts_value_function((void*)__closure_main_6_tsv));
  node_webview_on(webView, ts_value_string(ts_string_new("title")), ts_value_function((void*)__closure_main_7_tsv));
  node_webview_on(webView, ts_value_string(ts_string_new("error")), ts_value_function((void*)__closure_main_8_tsv));
  node_webview_on(webView, ts_value_string(ts_string_new("close")), ts_value_function((void*)__closure_main_9_tsv));
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
  node_webview_on(webView2, ts_value_string(ts_string_new("load")), ts_bind_function((void*)__closure_main_10_tsv, (Value[]){webView2}, 1));
  node_webview_run(webView);
}

void __init_test_my_app_main(void) {
  if (__init_done_test_my_app_main) return;
  __init_done_test_my_app_main = 1;
}
