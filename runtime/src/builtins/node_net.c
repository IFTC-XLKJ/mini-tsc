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
