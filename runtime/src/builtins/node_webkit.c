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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

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
  /* Drag regions received from frontend */
  DragRect* dragRegions;
  int dragRegionCount;
  DragRect* dragExcludes;
  int dragExcludeCount;
} WebViewInstance;

/* ---- URI scheme bridge for addJavaScriptInterface ----
 * Replaces the old WebSocket bridge.  A custom URI scheme (mini-tsc://) is
 * registered with WebKit; the injected JS shim calls fetch('mini-tsc://...')
 * to invoke native methods.  Custom schemes are local/secure — no mixed-
 * content restrictions, so this works on https:// pages too. */

/* Parsed request queued from the scheme handler (runs in a background
 * thread) and consumed by the main GTK loop. */
typedef struct SchemeRequest {
  struct SchemeRequest* next;
  WebViewInstance* inst;
  char* iface;   /* interface name */
  char* method;  /* method name */
  char* args;    /* JSON args array string */
  char* id;      /* promise ID */
} SchemeRequest;

#define MAX_INSTANCES 32
static WebViewInstance* g_instances[MAX_INSTANCES] = {0};
static int g_instanceCount = 0;
static int g_loopRunning = 0;
static int g_gtk_inited = 0;

/* ---- Custom URI scheme request queue (thread-safe) ---- */
static SchemeRequest* g_scheme_queue = NULL;
static GMutex g_scheme_mu;

/* ---- Scheme handler: finish a request with a JSON body ---- */
static void scheme_finish_json(WebKitURISchemeRequest* request,
                               const char* body, int status) {
  gsize len = strlen(body);
  GInputStream* stream = g_memory_input_stream_new_from_data(body, len, NULL);
  WebKitURISchemeResponse* response = webkit_uri_scheme_response_new(stream, len);
  g_object_unref(stream);
  webkit_uri_scheme_response_set_content_type(response, "application/json");
  webkit_uri_scheme_response_set_status(response, status, NULL);
  webkit_uri_scheme_request_finish_with_response(request, response);
  g_object_unref(response);
}

/* URL-decode a percent-encoded string (caller frees result). */
static char* url_decode(const char* src) {
  if (!src) return NULL;
  size_t slen = strlen(src);
  char* out = (char*)malloc(slen + 1);
  if (!out) return NULL;
  char* dst = out;
  for (size_t i = 0; i < slen; i++) {
    if (src[i] == '%' && i + 2 < slen) {
      char hex[3] = {src[i+1], src[i+2], 0};
      *dst++ = (char)strtol(hex, NULL, 16);
      i += 2;
    } else if (src[i] == '+') {
      *dst++ = ' ';
    } else {
      *dst++ = src[i];
    }
  }
  *dst = '\0';
  return out;
}

/* Callback registered with webkit_web_context_register_uri_scheme().
 * Runs in a WebKit I/O thread — must not call GTK/WebKit APIs directly. */
static void handle_mini_tsc_scheme(WebKitURISchemeRequest* request,
                                   gpointer user_data) {
  (void)user_data;
  const char* uri = webkit_uri_scheme_request_get_uri(request);
  if (!uri) {
    scheme_finish_json(request, "{\"__err\":\"bad uri\"}", 400);
    return;
  }

  /* Parse: mini-tsc://call/{webview_ptr_hex}/{iface}/{method}?args=...&id=... */
  const char* p = strstr(uri, "mini-tsc://call/");
  if (!p) {
    scheme_finish_json(request, "{\"__err\":\"bad path\"}", 400);
    return;
  }
  p += 16; /* skip "mini-tsc://call/" */

  /* Parse webview pointer hex */
  char* end = NULL;
  unsigned long wv_hex = strtoul(p, &end, 16);
  if (!end || *end != '/') {
    scheme_finish_json(request, "{\"__err\":\"bad wv ptr\"}", 400);
    return;
  }
  WebKitWebView* wv = (WebKitWebView*)(uintptr_t)wv_hex;
  p = end + 1;

  /* Parse iface name (until next '/') */
  const char* iface_start = p;
  while (*p && *p != '/') p++;
  size_t iface_len = (size_t)(p - iface_start);
  if (*p != '/') {
    scheme_finish_json(request, "{\"__err\":\"bad iface\"}", 400);
    return;
  }
  char* iface = strndup(iface_start, iface_len);
  p++; /* skip '/' */

  /* Parse method name (until '?' or end) */
  const char* method_start = p;
  while (*p && *p != '?') p++;
  size_t method_len = (size_t)(p - method_start);
  char* method = strndup(method_start, method_len);

  /* Parse query parameters */
  char* args_str = NULL;
  char* id_str = NULL;
  if (*p == '?') {
    p++; /* skip '?' */
    while (*p) {
      if (strncmp(p, "args=", 5) == 0) {
        p += 5;
        const char* val_start = p;
        while (*p && *p != '&') p++;
        args_str = url_decode(val_start);
      } else if (strncmp(p, "id=", 3) == 0) {
        p += 3;
        const char* val_start = p;
        while (*p && *p != '&') p++;
        id_str = url_decode(val_start);
      } else {
        while (*p && *p != '&') p++;
      }
      if (*p == '&') p++;
    }
  }

  if (!args_str) args_str = strdup("[]");
  if (!id_str) id_str = strdup("");

  /* Find the WebViewInstance */
  WebViewInstance* inst = NULL;
  for (int i = 0; i < MAX_INSTANCES; i++) {
    if (g_instances[i] && g_instances[i]->webview == wv) {
      inst = g_instances[i];
      break;
    }
  }
  if (!inst) {
    free(iface); free(method); free(args_str); free(id_str);
    scheme_finish_json(request, "{\"__err\":\"instance not found\"}", 404);
    return;
  }

  /* Enqueue request for main thread processing */
  SchemeRequest* req = (SchemeRequest*)calloc(1, sizeof(SchemeRequest));
  req->inst = inst;
  req->iface = iface;
  req->method = method;
  req->args = args_str;
  req->id = id_str;

  g_mutex_lock(&g_scheme_mu);
  req->next = g_scheme_queue;
  g_scheme_queue = req;
  g_mutex_unlock(&g_scheme_mu);

  /* Return a "pending" response — the main loop will call
   * G.dispatch(id, result, isErr) via webview_run_js() when done. */
  scheme_finish_json(request, "{\"__pending\":true}", 200);
}

/* Forward declarations */
static void webview_run_js(WebViewInstance* inst, const char* script);

static int bridge_inst_is_alive(WebViewInstance* inst) {
  if (!inst) return 0;
  for (int i = 0; i < MAX_INSTANCES; i++) {
    if (g_instances[i] == inst) return 1;
  }
  return 0;
}

/* Dispatch a pending promise result to JS via G.dispatch(id, val, isErr). */
static void bridge_resolve_promise_js(WebViewInstance* inst, const char* id,
                                       TSPromise* p) {
  if (!inst || !p) return;
  TSString* resultJson = ts_json_stringify(p->result);
  const char* val = resultJson ? resultJson->data : "null";
  int isErr = (p->state == PROMISE_REJECTED) ? 1 : 0;
  /* Build JS that resolves/rejects the pending promise.
   * val is already a JSON-encoded literal (string, number, null, etc.)
   * from ts_json_stringify, so it can be inlined directly in JS. */
  char js[131072];
  snprintf(js, sizeof(js),
    "(function(){"
    "var b=window.__mjbBr;"
    "if(!b)return;"
    "var e=b.pending['%s'];"
    "if(e){delete b.pending['%s'];"
    "%s%s%s;}"
    "b.dispatch&&b.dispatch('%s',%s,%d);"
    "})();",
    id, id,
    isErr ? "e.rej(new Error(" : "e.res(",
    val,
    isErr ? "))" : ")",
    id, val, isErr);
  webview_run_js(inst, js);
  if (resultJson) ts_string_free(resultJson);
}

/* ---------- async Promise responses for addJavaScriptInterface ---------- */
typedef struct PendingPromiseEntry {
  struct PendingPromiseEntry* next;
  char id[48];
  WebViewInstance* inst;
  TSPromise* promise;
} PendingPromiseEntry;

static PendingPromiseEntry* g_pending_promises = NULL;

static void bridge_poll_pending_promises(void) {
  PendingPromiseEntry** prev = &g_pending_promises;
  while (*prev) {
    PendingPromiseEntry* e = *prev;
    TSPromise* p = e->promise;
    if (p && p->type_tag == PROMISE_TAG && p->state != PROMISE_PENDING) {
      if (bridge_inst_is_alive(e->inst)) {
        bridge_resolve_promise_js(e->inst, e->id, p);
      }
      *prev = e->next;
      free(e);
    } else {
      prev = &e->next;
    }
  }
}

static void bridge_promise_pending(WebViewInstance* inst, const char* id, TSPromise* p) {
  PendingPromiseEntry* e = (PendingPromiseEntry*)malloc(sizeof(PendingPromiseEntry));
  if (!e) return;
  memset(e, 0, sizeof(*e));
  strncpy(e->id, id, sizeof(e->id) - 1);
  e->id[sizeof(e->id) - 1] = '\0';
  e->inst = inst;
  e->promise = p;
  e->next = g_pending_promises;
  g_pending_promises = e;
}

static void webkit_ensure_gtk(void) {
  if (g_gtk_inited) return;
  /* Force WebKit to render directly to the window surface instead of using
   * an offscreen compositor — fixes white screen on remote/headless X11. */
  g_setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", FALSE);
  g_setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", FALSE);
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

static gboolean webview_open_devtools_idle(gpointer data);

static gboolean on_load_failed(WebKitWebView* wv, WebKitLoadEvent load_event,
                                gchar* failing_uri, GError* error, gpointer data) {
  (void)wv; (void)load_event;
  WebViewInstance* inst = (WebViewInstance*)data;
  if (!inst) return FALSE;
  fprintf(stderr, "WebKitGTK: load-failed uri=%s error=%s\n",
          failing_uri ? failing_uri : "(null)",
          error ? error->message : "(null)");
  webview_emit(inst, "error", ts_value_string(ts_string_new(
      error ? error->message : "load failed")));
  return FALSE;
}

static void on_load_changed(WebKitWebView* wv, WebKitLoadEvent load_event, gpointer data) {
  WebViewInstance* inst = (WebViewInstance*)data;
  if (!inst) return;
  const char* names[] = {"STARTED", "REDIRECTED", "COMMITTED", "FINISHED"};
  int idx = (load_event >= 0 && load_event <= 3) ? load_event : -1;
  fprintf(stderr, "WebKitGTK: on_load_changed event=%d (%s) url=%s\n",
          load_event, idx >= 0 ? names[idx] : "?",
          inst->url ? inst->url : "(null)");
  if (load_event == WEBKIT_LOAD_STARTED) {
    webview_emit0(inst, "navigate");
  } else if (load_event == WEBKIT_LOAD_FINISHED) {
    inst->ready = 1;
    /* Re-inject interface scripts for the newly loaded document. */
    if (inst->interfaces && inst->webview) {
      for (size_t i = 0; i < inst->interfaces->capacity; i++) {
        if (inst->interfaces->entries[i].occupied) {
          Value val = inst->interfaces->entries[i].value;
          if (val.tag == TAG_STRING && val.as.string && val.as.string->data) {
            webview_run_js(inst, val.as.string->data);
          }
        }
      }
    }
    webview_emit0(inst, "ready");
    webview_emit0(inst, "load");
    fprintf(stderr, "WebKitGTK: Page loaded OK\n");
    /* Open devtools — use timeout so the window is fully mapped first. */
    if (inst->devTools) {
      g_timeout_add(500, webview_open_devtools_idle, inst);
    }
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
  if (!inst || !inst->webview || !inst->url) {
    fprintf(stderr, "WebKitGTK: navigate_to_url skipped (inst=%p webview=%p url=%s)\n",
            (void*)inst, inst ? (void*)inst->webview : NULL, inst && inst->url ? inst->url : "(null)");
    return;
  }
  fprintf(stderr, "WebKitGTK: navigate_to_url url=%s\n", inst->url);
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

/* ==================== Message dispatch ==================== */

/* Dispatch a single queued message (called from main thread). */
static void bridge_dispatch_message(WebViewInstance* inst,
                                    const char* iface, const char* method,
                                    const char* args_json, const char* id) {
  if (!inst || !inst->interfaceMethods || !iface || !method) return;

  /* Look up the native callback: "iface.method" */
  size_t keyLen = strlen(iface) + 1 + strlen(method) + 1;
  char* compositeKey = (char*)malloc(keyLen);
  if (!compositeKey) return;
  snprintf(compositeKey, keyLen, "%s.%s", iface, method);
  Value cb = ts_hashmap_get(inst->interfaceMethods, ts_string_new(compositeKey));
  free(compositeKey);

  if ((cb.tag != TAG_FUNCTION || !cb.as.function) &&
      !(cb.tag == TAG_OBJECT && cb.as.object && *(int32_t*)cb.as.object == BOUND_FN_TAG)) {
    /* Method not found — return error */
    if (id && id[0]) {
      char js_buf[1024];
      snprintf(js_buf, sizeof(js_buf),
        "(function(){var b=window.__mjbBr;if(b){b.dispatch('%s',"
        "'method not found',true);}})();", id);
      webview_run_js(inst, js_buf);
    }
    return;
  }

  /* Parse args JSON array */
  TSString* argsStr = ts_string_new(args_json ? args_json : "[]");
  Value argsVal = ts_json_parse(argsStr);
  ts_string_free(argsStr);

  TSArray* argsArr = (argsVal.tag == TAG_ARRAY && argsVal.as.array) ? argsVal.as.array : NULL;
  int argc = argsArr ? argsArr->length : 0;
  static Value callArgs[16];
  for (int i = 0; i < argc && i < 16; i++) {
    callArgs[i] = ts_array_get(argsArr, i);
  }

  Value result = ts_value_call(cb, callArgs, argc);

  /* If the handler returns a Promise, handle it specially. */
  if (ts_value_is_promise(result)) {
    TSPromise* p = (TSPromise*)result.as.object;
    if (p && p->type_tag == PROMISE_TAG) {
      if (p->state == PROMISE_FULFILLED) {
        result = p->result;
      } else if (p->state == PROMISE_REJECTED) {
        if (id && id[0]) {
          TSString* rj = ts_json_stringify(p->result);
          char js_buf[131072];
          snprintf(js_buf, sizeof(js_buf),
            "(function(){var b=window.__mjbBr;if(b&&b.dispatch)"
            "b.dispatch('%s',%s,true);})();",
            id, rj ? rj->data : "null");
          webview_run_js(inst, js_buf);
          if (rj) ts_string_free(rj);
        }
        return;
      } else {
        /* PENDING: enqueue for later delivery */
        if (id && id[0]) {
          bridge_promise_pending(inst, id, p);
        }
        return;
      }
    }
  }

  /* Send result back to JS */
  if (id && id[0]) {
    TSString* resultJson = ts_json_stringify(result);
    char js_buf[131072];
    snprintf(js_buf, sizeof(js_buf),
      "(function(){var b=window.__mjbBr;if(b&&b.dispatch)"
      "b.dispatch('%s',%s,false);})();",
      id, resultJson ? resultJson->data : "null");
    webview_run_js(inst, js_buf);
    if (resultJson) ts_string_free(resultJson);
  }

  fflush(stdout);
  fflush(stderr);
}

/* Process all queued scheme requests (called from main GTK thread). */
static void process_scheme_queue(void) {
  /* Drain the queue under the lock. */
  g_mutex_lock(&g_scheme_mu);
  SchemeRequest* head = g_scheme_queue;
  g_scheme_queue = NULL;
  g_mutex_unlock(&g_scheme_mu);

  while (head) {
    SchemeRequest* req = head;
    head = head->next;

    if (req->inst && bridge_inst_is_alive(req->inst)) {
      bridge_dispatch_message(req->inst, req->iface, req->method,
                              req->args, req->id);
    }
    free(req->iface);
    free(req->method);
    free(req->args);
    free(req->id);
    free(req);
  }
}

/* ==================== JS shim builder for addJavaScriptInterface ==================== */

/* helper: dynamic string builder */
typedef struct {
  char* buf;
  size_t cap;
  size_t len;
} StrBuilder;

static void sb_init(StrBuilder* sb) {
  sb->cap = 256;
  sb->buf = (char*)malloc(sb->cap);
  sb->len = 0;
  if (sb->buf) sb->buf[0] = '\0';
}

static void sb_append(StrBuilder* sb, const char* text) {
  if (!sb->buf) return;
  size_t tlen = strlen(text);
  while (sb->len + tlen + 1 > sb->cap) {
    sb->cap = sb->cap * 2 + tlen + 1;
    sb->buf = (char*)realloc(sb->buf, sb->cap);
  }
  memcpy(sb->buf + sb->len, text, tlen);
  sb->len += tlen;
  sb->buf[sb->len] = '\0';
}

static char* sb_take(StrBuilder* sb) {
  char* result = sb->buf;
  sb->buf = NULL;
  sb->cap = 0;
  sb->len = 0;
  return result;
}

static char* build_interface_script(const char* name, TSHashMap* methods,
                                    WebKitWebView* wv) {
  StrBuilder sb;
  sb_init(&sb);
  if (!sb.buf) return NULL;

  sb_append(&sb, "(function() {\n");
  sb_append(&sb, "  var NAME = \""); sb_append(&sb, name); sb_append(&sb, "\";\n");
  sb_append(&sb, "  var WVPTR = \"");
  {
    char hex[32];
    snprintf(hex, sizeof(hex), "%lx", (unsigned long)(uintptr_t)wv);
    sb_append(&sb, hex);
  }
  sb_append(&sb, "\";\n");
  sb_append(&sb, "  var G = window.__mjbBr;\n");
  sb_append(&sb, "  if (!G) { G = {pending:{},queue:[]}; window.__mjbBr = G; }\n");
  sb_append(&sb, "  function gid() { return Math.random().toString(36).substring(2,11)+Date.now().toString(36); }\n");
  /* dispatch: called by native to resolve/reject a pending promise */
  sb_append(&sb, "  G.dispatch = function(id, val, isErr) {\n");
  sb_append(&sb, "    var p = G.pending[id]; if (p) { delete G.pending[id];\n");
  sb_append(&sb, "      if (isErr) p.rej(new Error(val)); else p.res(val);\n");
  sb_append(&sb, "    }\n");
  sb_append(&sb, "  };\n");
  /* processQueue: drain G.queue and call native via custom URI scheme */
  sb_append(&sb, "  G.processQueue = function() {\n");
  sb_append(&sb, "    while (G.queue.length > 0) {\n");
  sb_append(&sb, "      var msg = G.queue.shift();\n");
  sb_append(&sb, "      var m = JSON.parse(msg);\n");
  sb_append(&sb, "      var url = 'mini-tsc://call/' + WVPTR + '/' + NAME + '/' + m.__m\n");
  sb_append(&sb, "        + '?args=' + encodeURIComponent(JSON.stringify(m.__a||[]))\n");
  sb_append(&sb, "        + '&id=' + encodeURIComponent(m.__id);\n");
  sb_append(&sb, "      fetch(url).then(function(r){return r.text();}).then(function(t){\n");
  sb_append(&sb, "        try { var d = JSON.parse(t); if (!d.__pending) {\n");
  sb_append(&sb, "          if (d.__err) G.dispatch(m.__id, d.__err, true);\n");
  sb_append(&sb, "          else G.dispatch(m.__id, d.__res, false);\n");
  sb_append(&sb, "        }} catch(e) {}\n");
  sb_append(&sb, "      }).catch(function(){});\n");
  sb_append(&sb, "    }\n");
  sb_append(&sb, "  };\n");
  /* Periodic timer to drain the queue */
  sb_append(&sb, "  if (!G._timer) G._timer = setInterval(G.processQueue, 10);\n");
  sb_append(&sb, "  window[NAME] = window[NAME] || {};\n");

  for (int32_t i = 0; i < methods->capacity; i++) {
    if (!methods->entries[i].occupied) continue;
    TSString* key = methods->entries[i].key;
    sb_append(&sb, "  window[NAME][\"");
    sb_append(&sb, key->data);
    sb_append(&sb, "\"] = function() {\n");
    sb_append(&sb, "    var args = Array.prototype.slice.call(arguments);\n");
    sb_append(&sb, "    return new Promise(function(res, rej) {\n");
    sb_append(&sb, "      var id = gid();\n");
    sb_append(&sb, "      G.pending[id] = {res:res, rej:rej};\n");
    sb_append(&sb, "      G.queue.push(JSON.stringify({__if:NAME,__m:\"");
    sb_append(&sb, key->data);
    sb_append(&sb, "\",__a:args,__id:id}));\n");
    sb_append(&sb, "      G.processQueue();\n");
    sb_append(&sb, "      setTimeout(function(){ if(G.pending[id]){ delete G.pending[id]; rej(new Error('Timeout')); } }, 30000);\n");
    sb_append(&sb, "    });\n");
    sb_append(&sb, "  };\n");
  }

  sb_append(&sb, "})();\n");
  return sb_take(&sb);
}

/* ---------- Public API (same symbols as Windows node_webview.c) ---------- */

static gboolean webview_open_devtools_idle(gpointer data) {
  WebViewInstance* inst = (WebViewInstance*)data;
  if (!inst || !inst->webview) return G_SOURCE_REMOVE;
  WebKitWebInspector* inspector = webkit_web_view_get_inspector(inst->webview);
  if (inspector) {
    webkit_web_inspector_show(inspector);
    fprintf(stderr, "WebKitGTK: DevTools opened (inspector=%p)\n", (void*)inspector);
  } else {
    fprintf(stderr, "WebKitGTK: DevTools FAILED - inspector is NULL\n");
  }
  return G_SOURCE_REMOVE;
}

Value node_webview_isAvailable(void) {
  /* WebKitGTK present at link time ⇒ available */
  return ts_value_boolean(1);
}

Value node_webview_WebView(Value options) {
  webkit_ensure_gtk();

  /* Register the custom URI scheme on the default WebKitWebContext.
   * This must be done before any WebView is created.  The handler
   * is called from a WebKit I/O thread — it enqueues the request
   * and returns a pending response; the main thread processes it. */
  {
    static int scheme_registered = 0;
    if (!scheme_registered) {
      WebKitWebContext* ctx = webkit_web_context_get_default();
      webkit_web_context_register_uri_scheme(ctx, "mini-tsc",
                                             handle_mini_tsc_scheme, NULL, NULL);
      scheme_registered = 1;
      fprintf(stderr, "WebKitGTK: Registered mini-tsc:// URI scheme\n");
    }
  }

  WebViewInstance* inst = (WebViewInstance*)calloc(1, sizeof(WebViewInstance));
  if (!inst) return ts_value_undefined();
  parse_options(inst, options);
  inst->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  apply_window_options(inst);

  inst->webview = WEBKIT_WEB_VIEW(webkit_web_view_new());
  /* Use a GtkScrolledWindow with NEVER scroll policy to clip the webview.
   * This prevents the webview's content height from propagating to the
   * window — the webview renders at its allocated size and handles
   * scrolling internally, while the window stays at its configured size. */
  {
    GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_NEVER);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(inst->webview));
    gtk_container_add(GTK_CONTAINER(inst->window), scroll);
  }

  /* Configure WebKitSettings for full functionality. */
  {
    WebKitSettings* settings = webkit_web_view_get_settings(inst->webview);
    webkit_settings_set_enable_developer_extras(settings, TRUE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, TRUE);
    webkit_settings_set_enable_javascript(settings, TRUE);
  }

  // === Transparent background fix ===
  if (inst->transparent) {
    gtk_widget_set_app_paintable(inst->window, TRUE);
    GtkCssProvider *provider = gtk_css_provider_new();
    char *css = g_strdup_printf("window { background-color: rgba(0,0,0,0); }");
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(inst->window),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_free(css);
    g_object_unref(provider);
  }
  g_signal_connect(inst->webview, "load-changed", G_CALLBACK(on_load_changed), inst);
  g_signal_connect(inst->webview, "load-failed", G_CALLBACK(on_load_failed), inst);
  g_signal_connect(inst->webview, "notify::title", G_CALLBACK(on_title_changed), inst);
  g_signal_connect(inst->window, "configure-event", G_CALLBACK(on_configure), inst);
  g_signal_connect(inst->window, "destroy", G_CALLBACK(on_window_destroy), inst);
  webview_register_instance(inst);
  if (inst->show) {
    gtk_widget_show_all(inst->window);
  }
  fprintf(stderr, "WebKitGTK: WebView created url=%s devTools=%d\n",
          inst->url ? inst->url : "(null)", inst->devTools);
  if (inst->url) {
    navigate_to_url(inst);
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
  while (g_instanceCount > 0) {
    while (gtk_events_pending()) {
      gtk_main_iteration_do(FALSE);
    }
    /* Process queued URI scheme requests from JS */
    process_scheme_queue();
    /* Poll pending async Promise results */
    bridge_poll_pending_promises();
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

  TSHashMap* methodsMap = (TSHashMap*)methods.as.object;
  const char* ifName = name.as.string->data;

  /* Store callbacks */
  if (!inst->interfaceMethods) {
    inst->interfaceMethods = ts_hashmap_new();
  }
  for (int32_t i = 0; i < methodsMap->capacity; i++) {
    if (!methodsMap->entries[i].occupied) continue;
    TSString* key = methodsMap->entries[i].key;
    Value fn = methodsMap->entries[i].value;
    if (fn.tag != TAG_FUNCTION && !(fn.tag == TAG_OBJECT && fn.as.object && *(int32_t*)fn.as.object == BOUND_FN_TAG)) continue;
    size_t keyLen = strlen(ifName) + 1 + strlen(key->data) + 1;
    char* compositeKey = (char*)malloc(keyLen);
    snprintf(compositeKey, keyLen, "%s.%s", ifName, key->data);
    ts_hashmap_set(inst->interfaceMethods, ts_string_new(compositeKey), fn);
    free(compositeKey);
  }

  /* Build JS shim and store for re-injection on every navigation.
   * The shim uses fetch('mini-tsc://call/...') — no WebSocket needed. */
  char* jsCode = build_interface_script(ifName, methodsMap, inst->webview);
  if (!jsCode) return ts_value_undefined();

  if (!inst->interfaces) {
    inst->interfaces = ts_hashmap_new();
  }
  TSString* keyStr = ts_string_new(ifName);
  /* Remove old script string if present */
  Value oldVal = ts_hashmap_get(inst->interfaces, keyStr);
  if (oldVal.tag == TAG_STRING && oldVal.as.string) {
    ts_string_free(oldVal.as.string);
  }
  Value scriptVal;
  scriptVal.tag = TAG_STRING;
  scriptVal.as.string = ts_string_new(jsCode);
  ts_hashmap_set(inst->interfaces, keyStr, scriptVal);
  ts_string_free(keyStr);

  /* If webview already ready, inject immediately into current page */
  if (inst->webview) {
    webview_run_js(inst, jsCode);
  }

  free(jsCode);
  return ts_value_undefined();
}

Value node_webview_removeJavaScriptInterface(Value self, Value name) {
  WebViewInstance* inst = webview_from_self(self);
  if (!inst) return ts_value_undefined();
  if (name.tag != TAG_STRING || !name.as.string || !name.as.string->data) {
    return ts_value_undefined();
  }

  /* Remove stored script string */
  if (inst->interfaces) {
    Value idVal = ts_hashmap_get(inst->interfaces, name.as.string);
    if (idVal.tag == TAG_STRING && idVal.as.string) {
      ts_string_free(idVal.as.string);
    }
    ts_hashmap_set(inst->interfaces, name.as.string, ts_value_undefined());
  }

  /* Remove callbacks */
  if (inst->interfaceMethods) {
    const char* prefix = name.as.string->data;
    size_t prefixLen = strlen(prefix);
    TSHashMap* newMap = ts_hashmap_new();
    for (int32_t i = 0; i < inst->interfaceMethods->capacity; i++) {
      if (!inst->interfaceMethods->entries[i].occupied) continue;
      TSString* key = inst->interfaceMethods->entries[i].key;
      if (strlen(key->data) <= prefixLen || strncmp(key->data, prefix, prefixLen) != 0 || key->data[prefixLen] != '.') {
        ts_hashmap_set(newMap, key, inst->interfaceMethods->entries[i].value);
      }
    }
    /* Swap pointer; old map is leaked intentionally */
    inst->interfaceMethods = newMap;
  }

  return ts_value_undefined();
}
