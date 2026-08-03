/* node_webkit.c — Linux WebKitGTK backend for the `webview` module.
 * Exports the same node_webview_* C API as Windows (node_webview.c / WebView2). */
#include "node_webview.h"
#include "ts_features.h"
#include "runtime.h"

#include <webkit2/webkit2.h>
#include <glib.h>

#if !defined(WEBKIT_LOAD_FAILED)
#define WEBKIT_LOAD_FAILED ((WebKitLoadEvent)0)
#endif

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
  int x, y, w, h;
} DragRect;

typedef struct {
  GtkWidget* window;
  WebKitWebView* webview;
  char* url;
  int width;
  int height;
  char* title;
  char* icon;
  int show;
  int center;
  int ready;
  int frame;
  int transparent;
  int devTools;
  int shadow;
  int roundedCorners;
  int resizable;
  int closing;
  TSHashMap* listeners;
  TSHashMap* interfaces;
  TSHashMap* interfaceMethods;
  DragRect* dragRegions;
  int dragRegionCount;
  DragRect* dragExcludes;
  int dragExcludeCount;
} WebViewInstance;

#define MAX_INSTANCES 32
static WebViewInstance* g_instances[MAX_INSTANCES] = {0};
static int g_instanceCount = 0;
static int g_loopRunning = 0;
static int g_gtk_inited = 0;

static void webkit_ensure_gtk(void) {
  if (g_gtk_inited) return;
  gtk_init(NULL, NULL);
  g_gtk_inited = 1;
}

static void webview_register_instance(WebViewInstance* inst) {
  if (!inst) return;
  inst->closing = 0;
  for (int i = 0; i < MAX_INSTANCES; i++) {
    if (!g_instances[i]) {
      g_instances[i] = inst;
      g_instanceCount++;
      return;
    }
  }
}

static void webview_unregister_instance(WebViewInstance* inst) {
  if (!inst) return;
  for (int i = 0; i < MAX_INSTANCES; i++) {
    if (g_instances[i] == inst) {
      g_instances[i] = NULL;
      g_instanceCount--;
      return;
    }
  }
}

static WebViewInstance* webview_from_self(Value self) {
  if (self.tag != TAG_OBJECT || !self.as.object) return NULL;
  return (WebViewInstance*)self.as.object;
}

static void webview_emit(WebViewInstance* inst, const char* event, Value arg) {
  if (!inst || !inst->listeners) return;
  Value arr = ts_hashmap_get(inst->listeners, ts_string_new(event));
  if (arr.tag != TAG_ARRAY || !arr.as.array) return;
  for (int i = 0; i < arr.as.array->length; i++) {
    Value fn = ts_array_get(arr.as.array, i);
    Value args[1];
    args[0] = arg;
    ts_value_call(fn, args, 1);
  }
}

static void webview_emit0(WebViewInstance* inst, const char* event) {
  webview_emit(inst, event, ts_value_undefined());
}

static GtkWindow* webview_toplevel(WebViewInstance* inst) {
  if (!inst || !inst->window) return NULL;
  return GTK_WINDOW(inst->window);
}

static void on_load_changed(WebKitWebView* wv, WebKitLoadEvent load_event, gpointer data) {
  WebViewInstance* inst = (WebViewInstance*)data;
  (void)wv;
  if (!inst) return;
  if (load_event == WEBKIT_LOAD_STARTED) {
    webview_emit0(inst, "navigate");
  } else if (load_event == WEBKIT_LOAD_FINISHED) {
    inst->ready = 1;
    webview_emit0(inst, "ready");
    webview_emit0(inst, "load");
  } else if (load_event == WEBKIT_LOAD_FAILED) {
    webview_emit(inst, "error", ts_value_string(ts_string_new("load failed")));
  }
}

static void on_title_changed(WebKitWebView* wv, GParamSpec* pspec, gpointer data) {
  WebViewInstance* inst = (WebViewInstance*)data;
  (void)pspec;
  if (!inst) return;
  const gchar* t = webkit_web_view_get_title(wv);
  if (t) {
    free(inst->title);
    inst->title = strdup(t);
    webview_emit(inst, "title", ts_value_string(ts_string_new(t)));
  }
}

static gboolean on_configure(GtkWidget* widget, GdkEventConfigure* event, gpointer data) {
  WebViewInstance* inst = (WebViewInstance*)data;
  (void)widget;
  if (!inst || !event) return FALSE;
  inst->width = event->width;
  inst->height = event->height;
  webview_emit0(inst, "resize");
  return FALSE;
}

static void on_window_destroy(GtkWidget* widget, gpointer data) {
  WebViewInstance* inst = (WebViewInstance*)data;
  (void)widget;
  if (!inst || inst->closing) return;
  inst->closing = 1;
  webview_emit0(inst, "close");
  webview_unregister_instance(inst);
  inst->window = NULL;
  inst->webview = NULL;
}

static void navigate_to_url(WebViewInstance* inst) {
  if (!inst || !inst->webview || !inst->url) return;
  webkit_web_view_load_uri(inst->webview, inst->url);
}

static void apply_window_options(WebViewInstance* inst) {
  GtkWindow* win = webview_toplevel(inst);
  if (!win) return;

  if (inst->title) {
    gtk_window_set_title(win, inst->title);
  }
  gtk_window_set_default_size(win, inst->width > 0 ? inst->width : 800,
                              inst->height > 0 ? inst->height : 600);
  gtk_window_set_resizable(win, inst->resizable ? TRUE : FALSE);

  if (!inst->frame) {
    gtk_window_set_decorated(win, FALSE);
  }

  if (inst->icon && inst->icon[0]) {
    GError* err = NULL;
    gtk_window_set_icon_from_file(win, inst->icon, &err);
    if (err) g_error_free(err);
  }

  if (inst->center) {
    gtk_window_set_position(win, GTK_WIN_POS_CENTER);
  }
}

static void parse_options(WebViewInstance* inst, Value options) {
  inst->width = 800;
  inst->height = 600;
  inst->show = 1;
  inst->center = 1;
  inst->frame = 1;
  inst->transparent = 0;
  inst->devTools = 1;
  inst->resizable = 1;
  inst->shadow = 0;
  inst->roundedCorners = 0;

  if (options.tag != TAG_OBJECT || !options.as.object) return;
  TSHashMap* map = (TSHashMap*)options.as.object;
  Value v;

  v = ts_hashmap_get(map, ts_string_new("url"));
  if (v.tag == TAG_STRING && v.as.string && v.as.string->data) {
    inst->url = strdup(v.as.string->data);
  }

  v = ts_hashmap_get(map, ts_string_new("width"));
  if (v.tag == TAG_NUMBER) inst->width = (int)v.as.number;

  v = ts_hashmap_get(map, ts_string_new("height"));
  if (v.tag == TAG_NUMBER) inst->height = (int)v.as.number;

  v = ts_hashmap_get(map, ts_string_new("title"));
  if (v.tag == TAG_STRING && v.as.string && v.as.string->data) {
    inst->title = strdup(v.as.string->data);
  }

  v = ts_hashmap_get(map, ts_string_new("icon"));
  if (v.tag == TAG_STRING && v.as.string && v.as.string->data) {
    inst->icon = strdup(v.as.string->data);
  }

  v = ts_hashmap_get(map, ts_string_new("show"));
  if (v.tag != TAG_NULL) inst->show = ts_to_boolean(v);

  v = ts_hashmap_get(map, ts_string_new("center"));
  if (v.tag != TAG_NULL) inst->center = ts_to_boolean(v);

  v = ts_hashmap_get(map, ts_string_new("frame"));
  if (v.tag != TAG_NULL) inst->frame = ts_to_boolean(v);

  v = ts_hashmap_get(map, ts_string_new("frameless"));
  if (v.tag != TAG_NULL) inst->frame = ts_to_boolean(v) ? 0 : 1;

  v = ts_hashmap_get(map, ts_string_new("transparent"));
  if (v.tag != TAG_NULL) inst->transparent = ts_to_boolean(v);

  v = ts_hashmap_get(map, ts_string_new("devTools"));
  if (v.tag != TAG_NULL) inst->devTools = ts_to_boolean(v);

  v = ts_hashmap_get(map, ts_string_new("shadow"));
  if (v.tag != TAG_NULL) inst->shadow = ts_to_boolean(v);

  v = ts_hashmap_get(map, ts_string_new("roundedCorners"));
  if (v.tag != TAG_NULL) inst->roundedCorners = ts_to_boolean(v);

  v = ts_hashmap_get(map, ts_string_new("resizable"));
  if (v.tag != TAG_NULL) inst->resizable = ts_to_boolean(v);
}

/* ---------- Public API (same symbols as Windows node_webview.c) ---------- */

Value node_webview_isAvailable(void) {
  /* WebKitGTK present at link time ⇒ available */
  return ts_value_boolean(1);
}

Value node_webview_WebView(Value options) {
  webkit_ensure_gtk();

  WebViewInstance* inst = (WebViewInstance*)calloc(1, sizeof(WebViewInstance));
  if (!inst) return ts_value_undefined();

  parse_options(inst, options);

  inst->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  if (!inst->window) {
    free(inst->url);
    free(inst->title);
    free(inst->icon);
    free(inst);
    return ts_value_undefined();
  }

  apply_window_options(inst);

  inst->webview = WEBKIT_WEB_VIEW(webkit_web_view_new());
  gtk_container_add(GTK_CONTAINER(inst->window), GTK_WIDGET(inst->webview));

  if (inst->devTools) {
    WebKitSettings* settings = webkit_web_view_get_settings(inst->webview);
    webkit_settings_set_enable_developer_extras(settings, TRUE);
  }

  g_signal_connect(inst->webview, "load-changed", G_CALLBACK(on_load_changed), inst);
  g_signal_connect(inst->webview, "notify::title", G_CALLBACK(on_title_changed), inst);
  g_signal_connect(inst->window, "configure-event", G_CALLBACK(on_configure), inst);
  g_signal_connect(inst->window, "destroy", G_CALLBACK(on_window_destroy), inst);

  webview_register_instance(inst);

  if (inst->url) {
    navigate_to_url(inst);
  }

  if (inst->show) {
    gtk_widget_show_all(inst->window);
  }

  return ts_value_object((void*)inst);
}

Value node_webview_loadURL(Value self, Value url) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst) return ts_value_undefined();
  free(inst->url);
  inst->url = NULL;
  if (url.tag == TAG_STRING && url.as.string && url.as.string->data) {
    inst->url = strdup(url.as.string->data);
  }
  navigate_to_url(inst);
  return ts_value_undefined();
}

Value node_webview_navigate(Value self, Value url) {
  return node_webview_loadURL(self, url);
}

Value node_webview_loadHTML(Value self, Value html) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst || !inst->webview) return ts_value_undefined();
  if (html.tag == TAG_STRING && html.as.string && html.as.string->data) {
    webkit_web_view_load_html(inst->webview, html.as.string->data, NULL);
  }
  return ts_value_undefined();
}

static void webview_run_js(WebViewInstance* inst, const char* script) {
  if (!inst || !inst->webview || !script) return;
  /* Prefer non-deprecated evaluate_javascript when available (WebKitGTK 2.40+). */
#if defined(WEBKIT_CHECK_VERSION) && WEBKIT_CHECK_VERSION(2, 40, 0)
  webkit_web_view_evaluate_javascript(inst->webview, script, -1,
                                      NULL, NULL, NULL, NULL, NULL);
#else
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  webkit_web_view_run_javascript(inst->webview, script, NULL, NULL, NULL);
  G_GNUC_END_IGNORE_DEPRECATIONS
#endif
}

Value node_webview_evaluate(Value self, Value script) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst) return ts_value_undefined();
  if (script.tag != TAG_STRING || !script.as.string || !script.as.string->data) {
    return ts_value_undefined();
  }
  webview_run_js(inst, script.as.string->data);
  return ts_value_undefined();
}

Value node_webview_executeJavaScript(Value self, Value script) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst) return ts_value_undefined();
  if (script.tag != TAG_STRING || !script.as.string || !script.as.string->data) {
    return ts_value_undefined();
  }
  webview_run_js(inst, script.as.string->data);
  return ts_value_undefined();
}

Value node_webview_setTitle(Value self, Value title) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst || !inst->window) return ts_value_undefined();
  free(inst->title);
  inst->title = NULL;
  if (title.tag == TAG_STRING && title.as.string && title.as.string->data) {
    inst->title = strdup(title.as.string->data);
    gtk_window_set_title(GTK_WINDOW(inst->window), inst->title);
  }
  return ts_value_undefined();
}

Value node_webview_setSize(Value self, Value width, Value height) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst || !inst->window) return ts_value_undefined();
  int w = (width.tag == TAG_NUMBER) ? (int)width.as.number : inst->width;
  int h = (height.tag == TAG_NUMBER) ? (int)height.as.number : inst->height;
  inst->width = w;
  inst->height = h;
  gtk_window_resize(GTK_WINDOW(inst->window), w, h);
  return ts_value_undefined();
}

Value node_webview_setIcon(Value self, Value iconPath) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst || !inst->window) return ts_value_undefined();
  if (iconPath.tag == TAG_STRING && iconPath.as.string && iconPath.as.string->data) {
    free(inst->icon);
    inst->icon = strdup(iconPath.as.string->data);
    GError* err = NULL;
    gtk_window_set_icon_from_file(GTK_WINDOW(inst->window), inst->icon, &err);
    if (err) g_error_free(err);
  }
  return ts_value_undefined();
}

Value node_webview_setPosition(Value self, Value x, Value y) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst || !inst->window) return ts_value_undefined();
  int px = (x.tag == TAG_NUMBER) ? (int)x.as.number : 0;
  int py = (y.tag == TAG_NUMBER) ? (int)y.as.number : 0;
  gtk_window_move(GTK_WINDOW(inst->window), px, py);
  return ts_value_undefined();
}

Value node_webview_center(Value self) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst || !inst->window) return ts_value_undefined();
  gtk_window_set_position(GTK_WINDOW(inst->window), GTK_WIN_POS_CENTER);
  return ts_value_undefined();
}

Value node_webview_show(Value self) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst || !inst->window) return ts_value_undefined();
  gtk_widget_show_all(inst->window);
  return ts_value_undefined();
}

Value node_webview_hide(Value self) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst || !inst->window) return ts_value_undefined();
  gtk_widget_hide(inst->window);
  return ts_value_undefined();
}

Value node_webview_focus(Value self) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst || !inst->window) return ts_value_undefined();
  gtk_window_present(GTK_WINDOW(inst->window));
  return ts_value_undefined();
}

Value node_webview_minimize(Value self) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst || !inst->window) return ts_value_undefined();
  gtk_window_iconify(GTK_WINDOW(inst->window));
  return ts_value_undefined();
}

Value node_webview_maximize(Value self) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst || !inst->window) return ts_value_undefined();
  gtk_window_maximize(GTK_WINDOW(inst->window));
  return ts_value_undefined();
}

Value node_webview_unmaximize(Value self) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst || !inst->window) return ts_value_undefined();
  gtk_window_unmaximize(GTK_WINDOW(inst->window));
  return ts_value_undefined();
}

Value node_webview_close(Value self) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst || !inst->window) return ts_value_undefined();
  gtk_widget_destroy(inst->window);
  free(inst->url);
  free(inst->title);
  free(inst->icon);
  inst->url = NULL;
  inst->title = NULL;
  inst->icon = NULL;
  if (inst->dragRegions) {
    free(inst->dragRegions);
    inst->dragRegions = NULL;
  }
  inst->dragRegionCount = 0;
  if (inst->dragExcludes) {
    free(inst->dragExcludes);
    inst->dragExcludes = NULL;
  }
  inst->dragExcludeCount = 0;
  return ts_value_undefined();
}

Value node_webview_restart(Value self) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst || !inst->webview) return ts_value_undefined();
  inst->ready = 0;
  if (inst->url) {
    navigate_to_url(inst);
  } else {
    webkit_web_view_reload(inst->webview);
  }
  return ts_value_undefined();
}

Value node_webview_run(Value self) {
  (void)self;
  webkit_ensure_gtk();

  if (g_loopRunning) {
    return ts_value_undefined();
  }

  if (g_instanceCount <= 0) {
    fprintf(stderr, "WebView: no instances to run\n");
    return ts_value_undefined();
  }

  g_loopRunning = 1;
  /* gtk_main blocks until all windows are destroyed (on_window_destroy may
   * call gtk_main_quit when last instance is gone). */
  while (g_instanceCount > 0) {
    while (gtk_events_pending()) {
      gtk_main_iteration_do(FALSE);
    }
#if defined(TS_NEED_MODULE_HTTP)
    {
      extern void node_http_server_poll(void);
      node_http_server_poll();
    }
#endif
    g_usleep(5000);
  }
  g_loopRunning = 0;
  return ts_value_undefined();
}

Value node_webview_on(Value self, Value event, Value callback) {
  if (self.tag != TAG_OBJECT || !self.as.object) return self;
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst) return self;

  if (!inst->listeners) {
    inst->listeners = ts_hashmap_new();
  }

  TSString* evName = ts_to_string(event);
  if (!evName) return self;

  Value arr = ts_hashmap_get(inst->listeners, evName);
  if (arr.tag != TAG_ARRAY || !arr.as.array) {
    arr = ts_value_array(ts_array_new());
    ts_hashmap_set(inst->listeners, evName, arr);
  }
  ts_array_push(arr.as.array, callback);
  return self;
}

Value node_webview_once(Value self, Value event, Value callback) {
  return node_webview_on(self, event, callback);
}

Value node_webview_off(Value self, Value event, Value callback) {
  if (self.tag != TAG_OBJECT || !self.as.object) return self;
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->listeners) return self;

  TSString* evName = ts_to_string(event);
  if (!evName) return self;

  Value arr = ts_hashmap_get(inst->listeners, evName);
  if (arr.tag != TAG_ARRAY || !arr.as.array) return self;

  TSArray* old = arr.as.array;
  TSArray* neu = ts_array_new();
  int removed = 0;
  for (int i = 0; i < old->length; i++) {
    Value fn = ts_array_get(old, i);
    if (!removed && fn.tag == callback.tag && fn.as.function == callback.as.function) {
      removed = 1;
      continue;
    }
    ts_array_push(neu, fn);
  }
  ts_hashmap_set(inst->listeners, evName, ts_value_array(neu));
  return self;
}

Value node_webview_get_ready(Value self) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst) return ts_value_boolean(0);
  return ts_value_boolean(inst->ready);
}

Value node_webview_get_url(Value self) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst || !inst->url) return ts_value_string(ts_string_new(""));
  return ts_value_string(ts_string_new(inst->url));
}

Value node_webview_addJavaScriptInterface(Value self, Value name, Value methods) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst) return ts_value_undefined();
  if (name.tag != TAG_STRING || !name.as.string || !name.as.string->data) {
    return ts_value_undefined();
  }
  if (methods.tag != TAG_OBJECT || !methods.as.object) {
    return ts_value_undefined();
  }

  /* Store methods for a future WebSocket bridge (Windows parity). For now,
   * inject a stub window[name] so page scripts don't throw ReferenceError. */
  if (!inst->interfaceMethods) {
    inst->interfaceMethods = ts_hashmap_new();
  }
  if (!inst->interfaces) {
    inst->interfaces = ts_hashmap_new();
  }

  TSHashMap* methodsMap = (TSHashMap*)methods.as.object;
  const char* ifName = name.as.string->data;

  for (int32_t i = 0; i < methodsMap->capacity; i++) {
    if (!methodsMap->entries[i].occupied) continue;
    TSString* key = methodsMap->entries[i].key;
    Value fn = methodsMap->entries[i].value;
    size_t keyLen = strlen(ifName) + 1 + strlen(key->data) + 1;
    char* compositeKey = (char*)malloc(keyLen);
    if (!compositeKey) continue;
    snprintf(compositeKey, keyLen, "%s.%s", ifName, key->data);
    ts_hashmap_set(inst->interfaceMethods, ts_string_new(compositeKey), fn);
    free(compositeKey);
  }

  char buf[1024];
  snprintf(buf, sizeof(buf),
           "(function(){ window[\"%s\"]=window[\"%s\"]||{}; })();",
           ifName, ifName);
  webview_run_js(inst, buf);
  ts_hashmap_set(inst->interfaces, ts_string_new(ifName),
                 ts_value_string(ts_string_new(buf)));
  return ts_value_undefined();
}

Value node_webview_removeJavaScriptInterface(Value self, Value name) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst) return ts_value_undefined();
  if (name.tag != TAG_STRING || !name.as.string || !name.as.string->data) {
    return ts_value_undefined();
  }
  if (inst->interfaces) {
    ts_hashmap_set(inst->interfaces, name.as.string, ts_value_undefined());
  }
  return ts_value_undefined();
}
