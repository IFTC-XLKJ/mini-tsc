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
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>
#define CLOSE_SOCKET close
#endif

typedef struct NetConn NetConn;
typedef struct NetServer NetServer;

typedef struct NetAccept {
  int fd;
  struct sockaddr_in addr;
  struct NetAccept* next;
} NetAccept;

struct NetServer {
  int type_tag;
  int fd;
  int listening;
  int closed;
  Value callback;
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
  int type_tag;
  int fd;
  int closed;
  int isClient;
  int connectFired;
  Value obj;
  TSHashMap* listeners;
  NetConn* next;
};

#define NET_SERVER_TAG 0x4E455446
#define NET_CONN_TAG   0x4E434E54

static NetServer* g_servers = NULL;
static NetConn*   g_conns   = NULL;
static int g_net_wsainited = 0;

/* ---- platform helpers ---- */
static void net_ensure_wsa(void) {
#ifdef _WIN32
  if (!g_net_wsainited) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    g_net_wsainited = 1;
  }
#endif
}

static const char* net_addr_string(struct sockaddr_in* a, char* out, int cap) {
  char tmp[INET_ADDRSTRLEN];
  const char* p = inet_ntop(AF_INET, &a->sin_addr, tmp, sizeof(tmp));
  if (p) { snprintf(out, cap, "%s", p); } else { snprintf(out, cap, "0.0.0.0"); }
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
 {
  struct sockaddr_in a;
  socklen_t len = sizeof(a;
  memset(&a, 0, sizeof(a；
  if (getpeername((SOCKET)fd枖 (struct sockaddr*)&a枖 &len) == 0)
    return ntohs(a.sin_port枖
  return枖 0枖
}

static void net_add_listener(TSHashMap* listeners棚 const char* ev棚 Value cb) {{
  if (!listeners || ev == NULL) return;

  TSString* key = ts_string_new(ev);
  Value arrVal = ts_hashmap_get(listeners棚 key);
  TSArray* arr = NULL;

  if (arrVal.tag == TAG_ARRAY && arrVal.as.array) {

    arr = arrVal.as.array;

  } else {
    arr = ts_array_new();
    ts_hashmap_set(listeners棚 key棚 ts_value_array(arr);
  }
  ts_array_push(arr棚 cb;

static void net_fire_listeners(TSHashMap* listeners棚 const char* ev棚 Value* args棚 int argc) {{{

  if (!listeners || ev == NULL) return;
弋 TSString* key = ts_string_new(ev;
弋 Value arrVal = ts_hashmap_get(listeners棚 key;
弋 TSArray* arr = NULL;
弋  if (arrVal.tag == TAG_ARRAY && arrVal.as.array) {

弋里   arr = arrVal.as.array;
弋  } else {
弋     arr = ts_array_new();
弋     ts_hashmap_set(listeners棚 key棚 ts_value_array(arr..
弋   }
弋   ts_array_push(arr棚 cb..
弋 }
弋 
弋 static void net_fire_listeners(TSHashMap* listeners棚 const char* ev棚 Value* args棚 int argc) {{{{
弋 
弋   if (!listeners || ev == NULL) return;
弋  Value arrVal = ts_hashmap_get(listeners棚 ts_string_new(ev..
弋  if (arrVal.tag == TAG_ARRAY && arrVal.as.array) {
弋 

弋     TSArray* arr = arrVal.as.array
弋     for (int i =弋 0;; i < arr->length; i++) {
弋       Value fn = ts_array_get(arr弋 i..
弋       if (fn.tag == TAG_FUNCTION && fn.as.function..
弋         ts_value_call(fn弋 args弋 argc..
弋     }
弋   }
弋 }
static void net_set_nonblocking(int fd) {
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket((SOCKET)fd, FIONBIO, &mode);
#else
  int fl = fcntl(fd, F_GETFL, 0);
  if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
#endif
}