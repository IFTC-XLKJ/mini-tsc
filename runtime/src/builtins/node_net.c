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
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>
#define CLOSE_SOCKET close
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
  pthread_mutex_ptr q_mu;
  pthread_t thread;
#endif
  NetServer* next;
};

struct NetConn {
  int type_tag;   /* NET_CONN_TAG */
  int fd;
:
  int closed;
  int isClient;
  int connectFired;
  Value obj;          /* back-reference to the TS-visible socket object */
  TSHashMap* listeners;   /* owned by NetConn, mirrored on the object */
  NetConn* next;
};

#define NET_SERVER_TAG 0x4E455446  /* 'NETS' */
#define NET_CONN_TAG   0x4E434E54  /* 'NCNT' */

static NetServer* g_servers = NULL;
static NetConn*   g_conns   = NULL;
static int g_net_wsainited = 0;

/* ------------------------------------------------------------------ */
/* Platform helpers                                                      */
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
  if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK;
#endif
}