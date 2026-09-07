#define _CRT_SECURE_NO_WARNINGS
#include "node_net.h"
#include "ts_features.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#define CLOSE_SOCKET closesocket
#define NET_SOCK_WOULD WSAEWOULDBLOCK
#define _strcasecmp _stricmp
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>
#define CLOSE_SOCKET close
#define NET_SOCK_WOULD EWOULDBLOCK
#endif

/* ------------------------------------------------------------------ */
/* Internal structs (stored behind the TS-visible object's "_impl"   */
/* hashmap key).                                                      */
/* ------------------------------------------------------------------ */
typedef struct NetConn NetConn;
typedef struct NetServer NetServer;

typedef struct NetAccept {
  int fd;
  struct sockaddr_in addr;
  struct NetAccept* next;
} NetAccept;

struct NetServer {
  int type_tag;   /* NET_SERVER_TAG */
  int fd;
  int listening;
  int closed;
  Value callback;    /* createServer connection listener */
  TSHashMap* listeners;
  int backlog;
  NetAccept* q_head;
  NetAccept* q_tail;
#ifdef _WIN32
  CRITICAL_SECTION q_mu;
  HANDLE thread;
#else
  pthread_mutex_t q_mu;
  pthread_t thread;
#endif
  NetServer* next;
};

struct NetConn {
  int type_tag;   /* NET_CONN_TAG */
  int fd;
  int closed;
  int isClient;
  int connectFired;
  TSHashMap* listeners;   /* owned by NetConn, mirrored on the object */
  NetConn* next;
};

#define NET_SERVER_TAG 0x4E455446  /* 'NETS' */
#define NET_CONN_TAG   0x4E434E54  /* 'NCNT' */

static NetServer* g_servers = NULL;
static NetConn*   g_conns   = NULL;
static int g_net_wsainited = 0;

/* ------------------------------------------------------------------ */
/* Platform helpers                                                    */
/* ------------------------------------------------------------------ */
static void net_ensure_wsa(void) {
#ifdef _WIN32
  if (!g_net_wsainited) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    g_net_wsainited = 1;
  }
#endif
}

static void net_set_nonblocking(int fd) {
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket((SOCKET)fd, FIONBIO, &mode);
#else
  int fl = fcntl(fd, F_GETFL, 0);
  if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
#endif
}

static const char* net_addr_string(struct sockaddr_in* a, char* out, int cap) {
  char tmp[INET_ADDRSTRLEN];
  const char* p = inet_ntop(AF_INET, &a->sin_addr, tmp, sizeof(tmp));
  snprintf(out, cap, "%s", p ? p : "0.0.0.0");
  return out;
}

static unsigned short net_local_port(int fd) {
  struct sockaddr_in a;
  socklen_t len = sizeof(a);
  memset(&a, 0, sizeof(a));
  if (getsockname((SOCKET)fd, (struct sockaddr*)&a, &len) == 0)
    return ntohs(a.sin_port);
  return 0;
}

static unsigned short net_remote_port(int fd) {
  struct sockaddr_in a;
  socklen_t len = sizeof(a);
  memset(&a, 0, sizeof(a));
  if (getpeername((SOCKET)fd, (struct sockaddr*)&a, &len) == 0)
    return ntohs(a.sin_port);
  return 0;
}

/* ------------------------------------------------------------------ */
/* Listener helpers (listeners hashmap: event -> TSArray of callbacks) */
/* ------------------------------------------------------------------ */
static void net_add_listener(TSHashMap* listeners, const char* ev, Value cb) {
  if (!listeners || ev == NULL) return;
  TSString* key = ts_string_new(ev);
  Value arrVal = ts_hashmap_get(listeners, key);
  TSArray* arr = NULL;
  if (arrVal.tag == TAG_ARRAY && arrVal.as.array) {
    arr = arrVal.as.array;
  } else {
    arr = ts_array_new();
    ts_hashmap_set(listeners, key, ts_value_array(arr));
  }
  ts_array_push(arr, cb);
}

static void net_remove_listener(TSHashMap* listeners, const char* ev, Value cb) {
  if (!listeners || ev == NULL) return;
  TSString* key = ts_string_new(ev);
  Value arrVal = ts_hashmap_get(listeners, key);
  if (arrVal.tag == TAG_ARRAY && arrVal.as.array) {
    TSArray* arr = arrVal.as.array;
    for (int i = 0; i < arr->length; i++) {
      Value cur = ts_array_get(arr, i);
      int match = 0;
      if (cb.tag == TAG_FUNCTION && cur.tag == TAG_FUNCTION)
        match = (cur.as.function == cb.as.function);
      else if (cb.tag == TAG_OBJECT && cur.tag == TAG_OBJECT)
        match = (cur.as.object == cb.as.object);
      if (match) {
        TSArray* neu = ts_array_new();
        for (int j = 0; j < arr->length; j++)
          if (j != i) ts_array_push(neu, ts_array_get(arr, j));
        ts_hashmap_set(listeners, key, ts_value_array(neu));
        break;
      }
    }
  }
}

static void net_fire_listeners(TSHashMap* listeners, const char* ev, Value* args, int argc) {
  if (!listeners || ev == NULL) return;
  const size_t key_buf = 256;
  char onceKey[256];
  /* Regular listeners */
  Value arrVal = ts_hashmap_get(listeners, ts_string_new(ev));
  if (arrVal.tag == TAG_ARRAY && arrVal.as.array) {
    TSArray* arr = arrVal.as.array;
    for (int i = 0; i < arr->length; i++) {
      Value fn = ts_array_get(arr, i);
      if (fn.tag == TAG_FUNCTION && fn.as.function)
        ts_value_call(fn, args, argc);
/* ------------------------------------------------------------------ */
/* Socket object helpers (TS-visible object is a hashmap wrapping a   */
/* NetConn stored under "_impl").                                     */
/* ------------------------------------------------------------------ */
static NetConn* net_conn_from(Value self) {
  if (self.tag != TAG_OBJECT || !self.as.object) return NULL;
  TSHashMap* map = (TSHashMap*)self.as.object;
  Value impl = ts_hashmap_get(map, ts_string_new("_impl"));
  if (impl.tag != TAG_OBJECT || !impl.as.object) return NULL;
  NetConn* c = (NetConn*)impl.as.object;
  return (c && c->type_tag == NET_CONN_TAG) ? c : NULL;
}

static NetServer* net_server_from(Value self) {
  if (self.tag != TAG_OBJECT || !self.as.object) return NULL;
  TSHashMap* map = (TSHashMap*)self.as.object;
  Value impl = ts_hashmap_get(map, ts_string_new("_impl"));
  if (impl.tag != TAG_OBJECT || !impl.as.object) return NULL;
  NetServer* s = (NetServer*)impl.as.object;
  return (s && s->type_tag == NET_SERVER_TAG) ? s : NULL;
}

static void net_conn_setprop(Value self, const char* k, Value v) {
  TSHashMap* m = (TSHashMap*)self.as.object;
  if (m) ts_hashmap_set(m, ts_string_new(k), v);
}

static Value net_socket_new(int fd, int isClient) {
  TSHashMap* o = ts_hashmap_new();
  NetConn* c = (NetConn*)malloc(sizeof(NetConn));
  c->type_tag = NET_CONN_TAG;
  c->fd = fd;
  c->closed = 0;
  c->isClient = isClient;
  c->connectFired = 0;
  c->listeners = ts_hashmap_new();
  c->next = g_conns;
  g_conns = c;

  ts_hashmap_set(o, ts_string_new("_impl"), ts_value_object(c));
  ts_hashmap_set(o, ts_string_new("_listeners"), ts_value_object(c->listeners));
  ts_hashmap_set(o, ts_string_new("_fd"), ts_value_number((double)fd));
  ts_hashmap_set(o, ts_string_new("connecting"), ts_value_boolean(isClient ? 1 : 0));
  ts_hashmap_set(o, ts_string_new("destroyed"), ts_value_boolean(0));
  ts_hashmap_set(o, ts_string_new("readable"), ts_value_boolean(1));
  ts_hashmap_set(o, ts_string_new("writable"), ts_value_boolean(1));
  return ts_value_object(o);
}

static void net_socket_close(Value self, NetConn* c) {
  if (!c || c->closed) return;
  if (c->fd >= 0) { CLOSE_SOCKET(c->fd); c->fd = -1; }
  c->closed = 1;
  net_conn_setprop(self, "destroyed", ts_value_boolean(1));
  net_conn_setprop(self, "readable", ts_value_boolean(0));
  net_conn_setprop(self, "writable", ts_value_boolean(0));
  /* unlink from poll list */
  NetConn** p = &g_conns;
  while (*p) {
    if (*p == c) { *p = c->next; break; }
    p = &(*p)->next;
  }
}

static void net_fire_event(Value self, const char* ev, Value* args, int argc) {
  NetConn* c = net_conn_from(self);
  if (c) net_fire_listeners(c->listeners, ev, args, argc);
}

/* ------------------------------------------------------------------ */
/* Server accept queue + thread                                       */
/* ------------------------------------------------------------------ */
static void net_q_push(NetServer* s, NetAccept* a) {
#ifdef _WIN32
  EnterCriticalSection(&s->q_mu);
#else
  pthread_mutex_lock(&s->q_mu);
#endif
  a->next = NULL;
  if (s->q_tail) s->q_tail->next = a;
  else s->q_head = a;
  s->q_tail = a;
#ifdef _WIN32
  LeaveCriticalSection(&s->q_mu);
#else
  pthread_mutex_unlock(&s->q_mu);
#endif
}

static NetAccept* net_q_pop(NetServer* s) {
  NetAccept* a = NULL;
#ifdef _WIN32
  EnterCriticalSection(&s->q_mu);
#else
  pthread_mutex_lock(&s->q_mu);
#endif
  if (s->q_head) {
    a = s->q_head;
    s->q_head = a->next;
    if (!s->q_head) s->q_tail = NULL;
  }
#ifdef _WIN32
  LeaveCriticalSection(&s->q_mu);
#else
  pthread_mutex_unlock(&s->q_mu);
#endif
  return a;
}

#ifdef _WIN32
static DWORD WINAPI net_accept_thread(LPVOID arg) {
#else
static void* net_accept_thread(void* arg) {
#endif
  NetServer* s = (NetServer*)arg;
  for (;;) {
    if (s->closed || s->fd < 0) break;
    struct sockaddr_in caddr;
    socklen_t clen = sizeof(caddr);
    int fd = (int)accept(s->fd, (struct sockaddr*)&caddr, &clen);
    if (fd < 0) {
#ifdef _WIN32
      Sleep(5);
#else
      usleep(5000);
#endif
      continue;
    }
    net_set_nonblocking(fd);
    NetAccept* na = (NetAccept*)malloc(sizeof(NetAccept));
    na->fd = fd;
    na->addr = caddr;
    na->next = NULL;
    net_q_push(s, na);
  }
#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

/* ------------------------------------------------------------------ */
/* Server helper: build TS-visible Server object                      */
/* ------------------------------------------------------------------ */
static Value net_server_new(Value callback) {
  TSHashMap* o = ts_hashmap_new();
  NetServer* s = (NetServer*)malloc(sizeof(NetServer));
  s->type_tag = NET_SERVER_TAG;
  s->fd = -1;
  s->listening = 0;
  s->closed = 0;
  s->callback = callback;
  s->listeners = ts_hashmap_new();
  s->backlog = 128;
  s->q_head = s->q_tail = NULL;
#ifdef _WIN32
  InitializeCriticalSection(&s->q_mu);
  s->thread = NULL;
/* ====================================================================== */
/*  Top-level module functions                                            */
/* ====================================================================== */
Value node_net_createServer(Value callback) {
  net_ensure_wsa();
  return net_server_new(callback);
}

/* Parse port argument which may be a number, or a 2-arg listen(port, cb) */
static int net_resolve_port(Value portVal) {
  if (portVal.tag == TAG_STRING && portVal.as.string) {
    const char* s = portVal.as.string->data ? portVal.as.string->data : "";
    return atoi(s);
  }
  return (int)ts_to_number(portVal);
}

Value node_net_server_listen(Value serverVal, Value portVal, Value callback) {
  NetServer* s = net_server_from(serverVal);
  if (!s) {
    TS_THROW(ts_value_string(ts_string_new("Invalid net Server object")));
    return ts_value_undefined();
  }
  net_ensure_wsa();
  if (s->closed) {
    TS_THROW(ts_value_string(ts_string_new("Server already closed")));
    return ts_value_undefined();
  }
  if (s->listening) return serverVal;

  int port = net_resolve_port(portVal);
  s->fd = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (s->fd < 0) {
    TS_THROW(ts_value_string(ts_string_new("Failed to create server socket")));
    return ts_value_undefined();
  }
  int opt = 1;
  setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons((uint16_t)port);

  if (bind(s->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    CLOSE_SOCKET(s->fd);
    s->fd = -1;
    Value err = ts_value_string(ts_string_new("Failed to bind"));
    net_fire_listeners(s->listeners, "error", &err, 1);
    TS_THROW(err);
    return ts_value_undefined();
  }
  if (listen(s->fd, s->backlog) < 0) {
    CLOSE_SOCKET(s->fd);
    s->fd = -1;
    TS_THROW(ts_value_string(ts_string_new("Failed to listen")));
    return ts_value_undefined();
  }
  s->listening = 1;
  ts_hashmap_set((TSHashMap*)serverVal.as.object, ts_string_new("listening"), ts_value_boolean(1));

  /* store the newly bound local port on the object so address() works */
  unsigned short lp = net_local_port(s->fd);
  ts_hashmap_set((TSHashMap*)serverVal.as.object, ts_string_new("localPort"), ts_value_number((double)lp));

  /* Launch accept thread */
#ifdef _WIN32
  s->thread = CreateThread(NULL, 0, net_accept_thread, s, 0, NULL);
#else
  pthread_create(&s->thread, NULL, net_accept_thread, s);
#endif

  /* Fire listening event */
  net_fire_listeners(s->listeners, "listening", NULL, 0);
  /* Listen callback */
  if (callback.tag == TAG_FUNCTION && callback.as.function) {
    ts_value_call(callback, NULL, 0);
  }
  return serverVal;
}

Value node_net_server_on(Value serverVal, Value event, Value callback) {
  NetServer* s = net_server_from(serverVal);
  if (!s) return serverVal;
  TSString* ev = ts_to_string(event);
  if (ev && ev->data && callback.tag == TAG_FUNCTION) {
    if (strcmp(ev->data, "connection") == 0) {
      /* store as the default connection listener too */
      s->callback = callback;
    }
    net_add_listener(s->listeners, ev->data, callback);
  }
  return serverVal;
}

Value node_net_server_once(Value serverVal, Value event, Value callback) {
  NetServer* s = net_server_from(serverVal);
  if (!s) return serverVal;
  TSString* ev = ts_to_string(event);
  if (ev && ev->data && callback.tag == TAG_FUNCTION) {
    net_add_listener(s->listeners, ev->data, callback);
  }
  return serverVal;
}

Value node_net_server_off(Value serverVal, Value event, Value callback) {
  NetServer* s = net_server_from(serverVal);
  if (!s) return serverVal;
  TSString* ev = ts_to_string(event);
  if (ev && ev->data) {
    net_remove_listener(s->listeners, ev->data, callback);
  }
  return serverVal;
}

Value node_net_server_close(Value serverVal, Value callback) {
  NetServer* s = net_server_from(serverVal);
  if (!s) return serverVal;
  if (!s->closed) {
    s->closed = 1;
    if (s->fd >= 0) { CLOSE_SOCKET(s->fd); s->fd = -1; }
    s->listening = 0;
    ts_hashmap_set((TSHashMap*)serverVal.as.object, ts_string_new("listening"), ts_value_boolean(0));
    net_fire_listeners(s->listeners, "close", NULL, 0);
  }
  if (callback.tag == TAG_FUNCTION && callback.as.function) {
    ts_value_call(callback, NULL, 0);
  }
  return serverVal;
}

Value node_net_server_address(Value serverVal) {
  NetServer* s = net_server_from(serverVal);
  if (!s) return ts_value_null();
  TSHashMap* info = ts_hashmap_new();
  char host[64];
  unsigned short p = s->listening ? net_local_port(s->fd) : 0;
  snprintf(host, sizeof(host), "0.0.0.0");
  ts_hashmap_set(info, ts_string_new("address"), ts_value_string(ts_string_new(host)));
  ts_hashmap_set(info, ts_string_new("port"), ts_value_number((double)p));
  ts_hashmap_set(info, ts_string_new("family"), ts_value_string(ts_string_new(s->listening ? "IPv4" : "")));
  return ts_value_object(info);
}

Value node_net_server_getConnections(Value serverVal, Value callback) {
  NetServer* s = net_server_from(serverVal);
  int count = 0;
  for (NetConn* c = g_conns; c; c = c->next) {
    if (!c->isClient && !c->closed) count++;
  }
  if (callback.tag == TAG_FUNCTION && callback.as.function) {
    Value args[2];
    args[0] = ts_value_null();
    args[1] = ts_value_number((double)count);
    ts_value_call(callback, args, 2);
  }
  return ts_value_number((double)count);
}

Value node_net_server_ref(Value serverVal) { return serverVal; }
Value node_net_server_unref(Value serverVal) { return serverVal; }
#else
  pthread_mutex_init(&s->q_mu, NULL);
#endif
  s->next = g_servers;
  g_servers = s;

  ts_hashmap_set(o, ts_string_new("_impl"), ts_value_object(s));
  ts_hashmap_set(o, ts_string_new("_listeners"), ts_value_object(s->listeners));
  ts_hashmap_set(o, ts_string_new("listening"), ts_value_boolean(0));
  return ts_value_object(o);
}
    }
  }
  /* once listeners: fire, then clear */
  snprintf(onceKey, key_buf, "%s##once", ev);
  Value onceVal = ts_hashmap_get(listeners, ts_string_new(onceKey));
  if (onceVal.tag == TAG_ARRAY && onceVal.as.array) {
    TSArray* oarr = onceVal.as.array;
    for (int i = 0; i < oarr->length; i++) {
      Value fn = ts_array_get(oarr, i);
      if (fn.tag == TAG_FUNCTION && fn.as.function)
        ts_value_call(fn, args, argc);
    }
    ts_hashmap_set(listeners, ts_string_new(onceKey), ts_value_array(ts_array_new()));
  }
}
