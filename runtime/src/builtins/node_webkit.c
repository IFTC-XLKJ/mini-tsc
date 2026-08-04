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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <netdb.h>
#include <time.h>

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
  /* Async WebSocket bridge */
  WsBridge* bridge;
  /* Drag regions received from frontend */
  DragRect* dragRegions;
  int dragRegionCount;
  DragRect* dragExcludes;
  int dragExcludeCount;
} WebViewInstance;

/* ---- WebSocket bridge for addJavaScriptInterface ----
 * A WebSocket server on 127.0.0.1:PORT is started for each WebView instance.
 * The injected JS shim connects via ws://127.0.0.1:PORT and exchanges JSON
 * messages.  This works on https:// pages without mixed-content issues. */

typedef struct {
  int listen_fd;
  int client_fd;
  int port;
  int handshake_done;
  char* recv_buf;
  int recv_len;
  int recv_cap;
} WsBridge;

/* Parsed request queued from WebSocket handler and consumed by the main GTK loop. */
typedef struct SchemeRequest {
  struct SchemeRequest* next;
  WebViewInstance* inst;
  char* iface;   /* interface name */
  char* method;  /* method name */
  char* args;    /* JSON args array string */
  char* id;      /* promise ID */
} SchemeRequest;

/* Parsed request queued from WebSocket handler and consumed by the main GTK loop. */
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

/* ---- Message queue (thread-safe) ---- */
static SchemeRequest* g_scheme_queue = NULL;
static GMutex g_scheme_mu;

/* ==================== WebSocket Bridge ==================== */

static int bridge_is_unsafe_port(int port) {
  static const int restricted[] = {
    1,7,9,11,13,15,17,19,20,21,22,23,25,37,42,43,53,
    77,79,87,95,101,102,103,104,109,110,111,113,115,117,119,
    123,135,139,143,179,389,427,465,512,513,514,515,526,530,
    531,532,540,548,556,563,587,601,636,993,995,2049,3659,
    4045,6000,6665,6666,6667,6668,6669,6697,7000,7100,
    8000,8001,8002,8008,8009,8080,8081,8082,8083,8084,
    8085,8086,8087,8088,8089,8090,8888,9000,9001,9090,9091,
    9441,9999,10000,10001,10002,10003,10004,10005,10006,
    10007,10008,10009,50000,50001,50002,50003,50004,50005,
    10080, -1
  };
  for (int i = 0; restricted[i] != -1; i++) {
    if (port == restricted[i]) return 1;
  }
  return 0;
}

static int bridge_init(WebViewInstance* inst) {
  if (inst->bridge) return inst->bridge->port;
  for (int attempt = 0; attempt < 50; attempt++) {
    WsBridge* b = (WsBridge*)calloc(1, sizeof(WsBridge));
    if (!b) return -1;
    b->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (b->listen_fd < 0) { free(b); return -1; }
    int opt = 1;
    setsockopt(b->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(b->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      close(b->listen_fd); free(b); continue;
    }
    socklen_t addrlen = sizeof(addr);
    if (getsockname(b->listen_fd, (struct sockaddr*)&addr, &addrlen) < 0) {
      close(b->listen_fd); free(b); continue;
    }
    b->port = ntohs(addr.sin_port);
    if (bridge_is_unsafe_port(b->port)) {
      fprintf(stderr, "WebKitGTK: Port %d is unsafe, retrying...\n", b->port);
      close(b->listen_fd); free(b); continue;
    }
    if (listen(b->listen_fd, 1) < 0) {
      close(b->listen_fd); free(b); continue;
    }
    /* Non-blocking */
    fcntl(b->listen_fd, F_SETFL, O_NONBLOCK);
    b->client_fd = -1;
    b->handshake_done = 0;
    b->recv_cap = 4096;
    b->recv_buf = (char*)malloc(b->recv_cap);
    if (b->recv_buf) b->recv_buf[0] = '\0';
    b->recv_len = 0;
    inst->bridge = b;
    fprintf(stderr, "WebKitGTK: Bridge server listening on port %d\n", b->port);
    return b->port;
  }
  fprintf(stderr, "WebKitGTK: Failed to find a safe port after 50 attempts\n");
  return -1;
}

static void bridge_close_client(WsBridge* b) {
  if (!b) return;
  if (b->client_fd >= 0) { close(b->client_fd); b->client_fd = -1; }
  b->handshake_done = 0;
  b->recv_len = 0;
  if (b->recv_buf) b->recv_buf[0] = '\0';
  fprintf(stderr, "WebKitGTK: Bridge client disconnected\n");
}

static void bridge_shutdown(WebViewInstance* inst) {
  if (!inst || !inst->bridge) return;
  WsBridge* b = inst->bridge;
  bridge_close_client(b);
  if (b->listen_fd >= 0) { close(b->listen_fd); b->listen_fd = -1; }
  free(b->recv_buf); free(b); inst->bridge = NULL;
  fprintf(stderr, "WebKitGTK: Bridge server shut down\n");
}

static int bridge_send_frame(int fd, const char* data, int len) {
  if (fd < 0) return -1;
  unsigned char header[10];
  int hdrlen = 0;
  header[0] = 0x81;
  if (len < 126) { header[1] = (unsigned char)len; hdrlen = 2; }
  else if (len <= 0xFFFF) { header[1] = 126; header[2] = (unsigned char)(len >> 8); header[3] = (unsigned char)(len & 0xFF); hdrlen = 4; }
  else return -1;
  if (send(fd, header, hdrlen, 0) != hdrlen) return -1;
  if (len > 0 && send(fd, data, len, 0) != len) return -1;
  return 0;
}

static void bridge_append_recv(WsBridge* b, const char* data, int len) {
  if (!b || !data || len <= 0) return;
  if (b->recv_len + len > b->recv_cap) {
    b->recv_cap = b->recv_len + len + 4096;
    b->recv_buf = (char*)realloc(b->recv_buf, b->recv_cap);
  }
  memcpy(b->recv_buf + b->recv_len, data, len);
  b->recv_len += len;
}

static int bridge_try_parse_frame(WsBridge* b, char** out_payload, int* out_len) {
  if (!b || b->recv_len < 2) return 0;
  unsigned char* buf = (unsigned char*)b->recv_buf;
  int opcode = buf[0] & 0x0F;
  int masked = buf[1] & 0x80;
  uint64_t payload_len = buf[1] & 0x7F;
  int header_len = 2;
  if (payload_len == 126) {
    header_len += 2;
    if (b->recv_len < header_len) return 0;
    payload_len = ((uint64_t)buf[2] << 8) | (uint64_t)buf[3];
  } else if (payload_len == 127) {
    header_len += 8;
    if (b->recv_len < header_len) return 0;
    payload_len = 0;
    for (int i = 0; i < 8; i++) payload_len = (payload_len << 8) | (uint64_t)buf[2+i];
  }
  if (masked) header_len += 4;
  if (b->recv_len < header_len) return 0;
  if (payload_len > 65536) return -1;
  int total_len = header_len + (int)payload_len;
  if (b->recv_len < total_len) return 0;
  char* payload = (char*)malloc((size_t)payload_len + 1);
  if (!payload) return -1;
  if (payload_len > 0) {
    memcpy(payload, b->recv_buf + header_len, (size_t)payload_len);
    if (masked) {
      unsigned char* mask = buf + header_len - 4;
      for (uint64_t i = 0; i < payload_len; i++) payload[i] ^= mask[i % 4];
    }
  }
  payload[payload_len] = '\0';
  int remaining = b->recv_len - total_len;
  if (remaining > 0) memmove(b->recv_buf, b->recv_buf + total_len, remaining);
  b->recv_len = remaining;
  *out_payload = payload;
  *out_len = (int)payload_len;
  return opcode;
}

/* SHA-1 + Base64 for WebSocket handshake (inline, no external deps) */
static void ws_sha1(const unsigned char* data, size_t len, unsigned char digest[20]) {
  uint32_t h0=0x67452301, h1=0xEFCDAB89, h2=0x98BADCFE, h3=0x10325476, h4=0xC3D2E1F0;
  size_t ml = len * 8;
  size_t newlen = len + 1;
  while (newlen % 64 != 56) newlen++;
  newlen += 8;
  unsigned char* msg = (unsigned char*)calloc(newlen, 1);
  memcpy(msg, data, len);
  msg[len] = 0x80;
  for (int i = 0; i < 8; i++) msg[newlen-1-i] = (unsigned char)(ml >> (i*8));
  for (size_t offset = 0; offset < newlen; offset += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = ((uint32_t)msg[offset+i*4]<<24)|((uint32_t)msg[offset+i*4+1]<<16)|((uint32_t)msg[offset+i*4+2]<<8)|(uint32_t)msg[offset+i*4+3];
    for (int i = 16; i < 80; i++) { uint32_t t=w[i-3]^w[i-8]^w[i-14]^w[i-16]; w[i]=(t<<1)|(t>>31); }
    uint32_t a=h0,b=h1,c=h2,d=h3,e=h4;
    for (int i = 0; i < 80; i++) {
      uint32_t f,t;
      if (i<20) { f=(b&c)|((~b)&d); t=0x5A827999; }
      else if (i<40) { f=b^c^d; t=0x6ED9EBA1; }
      else if (i<60) { f=(b&c)|(b&d)|(c&d); t=0x8F1BBCDC; }
      else { f=b^c^d; t=0xCA62C1D6; }
      uint32_t tmp=((a<<5)|(a>>27))+f+e+t+w[i]; e=d; d=c; c=(b<<30)|(b>>2); b=a; a=tmp;
    }
    h0+=a; h1+=b; h2+=c; h3+=d; h4+=e;
  }
  free(msg);
  digest[0]=(h0>>24);digest[1]=(h0>>16);digest[2]=(h0>>8);digest[3]=h0;
  digest[4]=(h1>>24);digest[5]=(h1>>16);digest[6]=(h1>>8);digest[7]=h1;
  digest[8]=(h2>>24);digest[9]=(h2>>16);digest[10]=(h2>>8);digest[11]=h2;
  digest[12]=(h3>>24);digest[13]=(h3>>16);digest[14]=(h3>>8);digest[15]=h3;
  digest[16]=(h4>>24);digest[17]=(h4>>16);digest[18]=(h4>>8);digest[19]=h4;
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void ws_base64(const unsigned char* data, int len, char* out) {
  int j=0;
  for(int i=0;i<len;i+=3){
    uint32_t a=(uint32_t)data[i]; uint32_t b=(i+1<len)?(uint32_t)data[i+1]:0; uint32_t c=(i+2<len)?(uint32_t)data[i+2]:0;
    uint32_t triple=(a<<16)|(b<<8)|c;
    out[j++]=B64[(triple>>18)&0x3F]; out[j++]=B64[(triple>>12)&0x3F];
    out[j++]=(i+1<len)?B64[(triple>>6)&0x3F]:'='; out[j++]=(i+2<len)?B64[triple&0x3F]:'=';
  }
  out[j]='\0';
}

static int bridge_handle_handshake(WsBridge* b) {
  char temp[2048];
  int n = recv(b->client_fd, temp, sizeof(temp), 0);
  if (n > 0) bridge_append_recv(b, temp, n);
  else if (n == 0) { bridge_close_client(b); return -1; }
  else return 0;
  if (b->recv_len < 4) return 0;
  if (b->recv_len > 8192) { bridge_close_client(b); return -1; }
  b->recv_buf[b->recv_len] = '\0';
  if (!strstr(b->recv_buf, "\r\n\r\n")) return 0;
  if (!strstr(b->recv_buf,"Upgrade: websocket") && !strstr(b->recv_buf,"upgrade: websocket")) { bridge_close_client(b); return -1; }
  char* key_line = strstr(b->recv_buf,"Sec-WebSocket-Key:");
  if (!key_line) key_line = strstr(b->recv_buf,"sec-websocket-key:");
  if (!key_line) { bridge_close_client(b); return -1; }
  char* ks = key_line + 18;
  while(*ks==' '||*ks=='\t') ks++;
  char key[128]={0}; int ki=0;
  while(*ks && *ks!='\r' && *ks!='\n' && ki<127) key[ki++]=*ks++;
  while(ki>0 && (key[ki-1]==' '||key[ki-1]=='\t')) { key[ki-1]='\0'; ki--; }
  /* Compute accept key */
  const char* GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  size_t klen = strlen(key);
  size_t glen = strlen(GUID);
  unsigned char* combined = (unsigned char*)malloc(klen + glen);
  memcpy(combined, key, klen);
  memcpy(combined + klen, GUID, glen);
  unsigned char digest[20];
  ws_sha1(combined, klen + glen, digest);
  free(combined);
  char accept_key[64];
  ws_base64(digest, 20, accept_key);
  char resp[512];
  int rlen = snprintf(resp, sizeof(resp),
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Accept: %s\r\n\r\n", accept_key);
  int total_sent = 0;
  while (total_sent < rlen) {
    int sent = send(b->client_fd, resp + total_sent, rlen - total_sent, 0);
    if (sent > 0) total_sent += sent;
    else { bridge_close_client(b); return -1; }
  }
  b->handshake_done = 1;
  b->recv_len = 0;
  if (b->recv_buf) b->recv_buf[0] = '\0';
  fprintf(stderr, "WebKitGTK: Bridge handshake done\n");
  return 1;
}

/* Poll the WebSocket bridge for incoming connections and messages. */
static void bridge_poll(WebViewInstance* inst) {
  if (!inst || !inst->bridge) return;
  WsBridge* b = inst->bridge;
  /* Accept new connections */
  if (b->listen_fd >= 0) {
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int fd = accept(b->listen_fd, (struct sockaddr*)&addr, &addrlen);
    if (fd >= 0) {
      if (b->client_fd >= 0) bridge_close_client(b);
      fcntl(fd, F_SETFL, O_NONBLOCK);
      b->client_fd = fd;
      b->handshake_done = 0;
      b->recv_len = 0;
      if (b->recv_buf) b->recv_buf[0] = '\0';
      fprintf(stderr, "WebKitGTK: Bridge client connected\n");
    }
  }
  if (b->client_fd < 0) return;
  if (!b->handshake_done) {
    int r = bridge_handle_handshake(b);
    if (r <= 0) return;
  }
  char temp[4096];
  int n = recv(b->client_fd, temp, sizeof(temp), 0);
  if (n > 0) bridge_append_recv(b, temp, n);
  else if (n == 0) { bridge_close_client(b); return; }
  else { int err = errno; if (err != EAGAIN && err != EWOULDBLOCK) { bridge_close_client(b); return; } }
  while (1) {
    char* payload = NULL; int payload_len = 0;
    int opcode = bridge_try_parse_frame(b, &payload, &payload_len);
    if (opcode == 0) break;
    if (opcode < 0) { bridge_close_client(b); return; }
    if (opcode == 0x08) { bridge_close_client(b); free(payload); return; }
    if (opcode == 0x09) {
      unsigned char pong[2] = {0x8A, 0x00};
      send(b->client_fd, pong, 2, 0);
      free(payload); continue;
    }
    if (opcode == 0x01 || opcode == 0x02) {
      /* Dispatch message */
      if (inst->interfaceMethods && payload) {
        SchemeRequest* req = (SchemeRequest*)calloc(1, sizeof(SchemeRequest));
        req->inst = inst;
        /* Parse JSON: {__if, __m, __a, __id} */
        TSString* msgStr = ts_string_new(payload);
        Value parsed = ts_json_parse(msgStr);
        ts_string_free(msgStr);
        if (parsed.tag == TAG_OBJECT && parsed.as.object) {
          TSHashMap* map = (TSHashMap*)parsed.as.object;
          Value ifVal = ts_hashmap_get(map, ts_string_new("__if"));
          Value mVal = ts_hashmap_get(map, ts_string_new("__m"));
          Value aVal = ts_hashmap_get(map, ts_string_new("__a"));
          Value idVal = ts_hashmap_get(map, ts_string_new("__id"));
          if (ifVal.tag == TAG_STRING && ifVal.as.string && mVal.tag == TAG_STRING && mVal.as.string) {
            req->iface = strdup(ifVal.as.string->data);
            req->method = strdup(mVal.as.string->data);
            /* Serialize args back to JSON string */
            TSString* argsJson = ts_json_stringify(aVal);
            req->args = argsJson ? strdup(argsJson->data) : strdup("[]");
            if (argsJson) ts_string_free(argsJson);
            req->id = (idVal.tag == TAG_STRING && idVal.as.string) ? strdup(idVal.as.string->data) : strdup("");
            g_mutex_lock(&g_scheme_mu);
            req->next = g_scheme_queue;
            g_scheme_queue = req;
            g_mutex_unlock(&g_scheme_mu);
          } else {
            free(req);
          }
        } else {
          free(req);
        }
      }
    }
    free(payload);
  }
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

/* Send a promise result to JS via WebSocket frame: {__id, __res} or {__id, __err}. */
static void bridge_resolve_promise_ws(WebViewInstance* inst, const char* id,
                                       TSPromise* p) {
  if (!inst || !p || !inst->bridge) return;
  WsBridge* b = inst->bridge;
  if (b->client_fd < 0 || !b->handshake_done) return;
  TSString* resultJson = ts_json_stringify(p->result);
  const char* val = resultJson ? resultJson->data : "null";
  int isErr = (p->state == PROMISE_REJECTED) ? 1 : 0;
  char resp[131072];
  snprintf(resp, sizeof(resp), "{\"__id\":\"%s\",\"__%s\":%s}",
           id, isErr ? "err" : "res", val);
  bridge_send_frame(b->client_fd, resp, (int)strlen(resp));
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
      if (bridge_inst_is_alive(e->inst) && e->inst->bridge &&
          e->inst->bridge->client_fd >= 0 && e->inst->bridge->handshake_done) {
        bridge_resolve_promise_ws(e->inst, e->id, p);
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
    /* Method not found — return error via WebSocket */
    if (id && id[0] && inst->bridge && inst->bridge->client_fd >= 0 && inst->bridge->handshake_done) {
      char resp[1024];
      snprintf(resp, sizeof(resp),
        "{\"__id\":\"%s\",\"__err\":\"method not found\"}", id);
      bridge_send_frame(inst->bridge->client_fd, resp, (int)strlen(resp));
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
      } else      if (p->state == PROMISE_REJECTED) {
        if (id && id[0] && inst->bridge && inst->bridge->client_fd >= 0 && inst->bridge->handshake_done) {
          TSString* rj = ts_json_stringify(p->result);
          char resp[131072];
          snprintf(resp, sizeof(resp),
            "{\"__id\":\"%s\",\"__err\":%s}",
            id, rj ? rj->data : "null");
          bridge_send_frame(inst->bridge->client_fd, resp, (int)strlen(resp));
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

  /* Send result back to JS via WebSocket frame */
  if (id && id[0] && inst->bridge && inst->bridge->client_fd >= 0 && inst->bridge->handshake_done) {
    TSString* resultJson = ts_json_stringify(result);
    char resp[131072];
    snprintf(resp, sizeof(resp), "{\"__id\":\"%s\",\"__res\":%s}",
             id, resultJson ? resultJson->data : "null");
    bridge_send_frame(inst->bridge->client_fd, resp, (int)strlen(resp));
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
                                    int port) {
  StrBuilder sb;
  sb_init(&sb);
  if (!sb.buf) return NULL;

  sb_append(&sb, "(function() {\n");
  sb_append(&sb, "  var NAME = \""); sb_append(&sb, name); sb_append(&sb, "\";\n");
  sb_append(&sb, "  var PORT = "); {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    sb_append(&sb, port_str);
  }
  sb_append(&sb, ";\n");
  sb_append(&sb, "  var G = window.__mjbBr;\n");
  sb_append(&sb, "  if (!G) { G = {pending:{}, queue:[], port:PORT}; window.__mjbBr = G; }\n");
  sb_append(&sb, "  function gid() { return Math.random().toString(36).substring(2,11)+Date.now().toString(36); }\n");
  sb_append(&sb, "  function flush() { while(G.queue.length && G.ws && G.ws.readyState===1){ G.ws.send(G.queue.shift()); } }\n");
  sb_append(&sb, "  function onmsg(ev){var d=JSON.parse(ev.data);if(d.__id&&G.pending[d.__id]){if(d.__err){G.pending[d.__id].rej(new Error(d.__err));}else{G.pending[d.__id].res(d.__res);}delete G.pending[d.__id];}}\n");
  sb_append(&sb, "  function conn(){\n");
  sb_append(&sb, "    if (G.ws && (G.ws.readyState===0||G.ws.readyState===1)) return;\n");
  sb_append(&sb, "    if (G.ws){ try{ G.ws.close(); }catch(e){} }\n");
  sb_append(&sb, "    G.ws = new WebSocket('ws://127.0.0.1:'+G.port);\n");
  sb_append(&sb, "    G.ws.onopen = function(){ flush(); };\n");
  sb_append(&sb, "    G.ws.onmessage = onmsg;\n");
  sb_append(&sb, "    G.ws.onclose = function(){ setTimeout(conn, 500); };\n");
  sb_append(&sb, "    G.ws.onerror = function(){ if(G.ws){ try{G.ws.close();}catch(e){} } };\n");
  sb_append(&sb, "  }\n");
  sb_append(&sb, "  conn();\n");
  sb_append(&sb, "  G.gid = gid;\n");
  sb_append(&sb, "  window[NAME] = window[NAME] || {};\n");

  for (int32_t i = 0; i < methods->capacity; i++) {
    if (!methods->entries[i].occupied) continue;
    TSString* key = methods->entries[i].key;
    sb_append(&sb, "  window[NAME][\"");
    sb_append(&sb, key->data);
    sb_append(&sb, "\"] = function() {\n");
    sb_append(&sb, "    var args = Array.prototype.slice.call(arguments);\n");
    sb_append(&sb, "    return new Promise(function(res, rej) {\n");
    sb_append(&sb, "      var id = G.gid();\n");
    sb_append(&sb, "      var msg = JSON.stringify({__if:NAME,__m:\"");
    sb_append(&sb, key->data);
    sb_append(&sb, "\",__a:args,__id:id});\n");
    sb_append(&sb, "      G.pending[id] = {res:res, rej:rej};\n");
    sb_append(&sb, "      if (!G.ws || G.ws.readyState !== 1) conn();\n");
    sb_append(&sb, "      if (G.ws && G.ws.readyState === 1) G.ws.send(msg);\n");
    sb_append(&sb, "      else G.queue.push(msg);\n");
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
    /* Poll WebSocket bridges for all instances */
    for (int i = 0; i < MAX_INSTANCES; i++) {
      if (g_instances[i] && g_instances[i]->bridge) {
        bridge_poll(g_instances[i]);
      }
    }
    /* Process queued messages from WebSocket/URI scheme */
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

  /* Initialize WebSocket bridge */
  int port = bridge_init(inst);
  if (port < 0) {
    fprintf(stderr, "WebKitGTK: Failed to initialize bridge\n");
    return ts_value_undefined();
  }

  /* Build JS shim and store for re-injection on every navigation.
   * The shim uses ws://127.0.0.1:PORT to communicate with native code. */
  char* jsCode = build_interface_script(ifName, methodsMap, port);
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
