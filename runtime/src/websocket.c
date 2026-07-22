/* WebSocket / WebSocketServer runtime — minimal RFC 6455 implementation.
 * Single-threaded, non-blocking poll via ts_websocket_poll().
 * Client: new WebSocket(url) → TCP connect → HTTP upgrade → onopen → msg loop.
 * Server: new WebSocketServer() + Response(wss) → http_upgrade → msg loop. */

#include "runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#define CLOSE_SOCKET closesocket
#define TS_SOCK_ERR WSAGetLastError()
#define TS_SOCK_EWOULDBLOCK WSAEWOULDBLOCK
#define TS_SOCK_EINPROGRESS WSAEINPROGRESS
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
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
#define TS_SOCK_ERR errno
#define TS_SOCK_EWOULDBLOCK EWOULDBLOCK
#define TS_SOCK_EINPROGRESS EINPROGRESS
#endif

/* ------------------------------------------------------------------ */
/* Internal globals                                                     */
/* ------------------------------------------------------------------ */
static WebSocket* g_ws_list = NULL;        /* linked list of all active */
static int g_ws_count = 0;
static int g_ws_polling = 0;               /* re-entrancy guard         */
static int g_ws_wsa_inited = 0;

static void ws_ensure_wsa(void) {
#ifdef _WIN32
  if (!g_ws_wsa_inited) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    g_ws_wsa_inited = 1;
  }
#endif
}

/* ------------------------------------------------------------------ */
/* Base64 encode                                                       */
/* ------------------------------------------------------------------ */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static TSString* base64_encode(const uint8_t* data, int len) {
  int out_len = ((len + 2) / 3) * 4;
  char* buf = (char*)malloc((size_t)out_len + 1);
  if (!buf) return ts_string_new("");
  int j = 0;
  for (int i = 0; i < len; i += 3) {
    uint32_t a = (uint32_t)data[i];
    uint32_t b = (i+1 < len) ? (uint32_t)data[i+1] : 0;
    uint32_t c = (i+2 < len) ? (uint32_t)data[i+2] : 0;
    uint32_t triple = (a << 16) | (b << 8) | c;
    buf[j++] = B64[(triple >> 18) & 0x3F];
    buf[j++] = B64[(triple >> 12) & 0x3F];
    buf[j++] = (i+1 < len) ? B64[(triple >> 6) & 0x3F] : '=';
    buf[j++] = (i+2 < len) ? B64[triple & 0x3F] : '=';
  }
  buf[j] = '\0';
  TSString* s = ts_string_new(buf);
  free(buf);
  return s;
}

/* ------------------------------------------------------------------ */
/* Inline SHA-1 for handshake (WS key = base64(sha1(key + GUID)))      */
/* ------------------------------------------------------------------ */
#define SHA1_BLOCK 64
typedef struct {
  uint32_t state[5];
  uint64_t count;
  uint8_t buffer[SHA1_BLOCK];
} WsSha1;

static void sha1_transform(uint32_t state[5], const uint8_t block[64]) {
  uint32_t a,b,c,d,e,w[80];
  for (int i = 0; i < 16; i++)
    w[i] = ((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|
           ((uint32_t)block[i*4+2]<<8)|(uint32_t)block[i*4+3];
  for (int i = 16; i < 80; i++) {
    uint32_t tmp = w[i-3]^w[i-8]^w[i-14]^w[i-16];
    w[i] = (tmp<<1)|(tmp>>31);
  }
  a=state[0]; b=state[1]; c=state[2]; d=state[3]; e=state[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f,t;
    if (i<20)      { f=(b&c)|((~b)&d);          t=0x5A827999; }
    else if (i<40) { f=b^c^d;                    t=0x6ED9EBA1; }
    else if (i<60) { f=(b&c)|(b&d)|(c&d);        t=0x8F1BBCDC; }
    else           { f=b^c^d;                    t=0xCA62C1D6; }
    uint32_t tmp = ((a<<5)|(a>>27))+f+e+t+w[i]; e=d; d=c; c=(b<<30)|(b>>2); b=a; a=tmp;
  }
  state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e;
}

static void ws_sha1_init(WsSha1* h) {
  h->state[0]=0x67452301; h->state[1]=0xEFCDAB89;
  h->state[2]=0x98BADCFE; h->state[3]=0x10325476;
  h->state[4]=0xC3D2E1F0;
  h->count=0;
  memset(h->buffer, 0, SHA1_BLOCK);
}

static void ws_sha1_update(WsSha1* h, const uint8_t* data, size_t len) {
  size_t idx = (size_t)(h->count & 63);
  h->count += len;
  for (size_t i = 0; i < len; i++) {
    h->buffer[idx++] = data[i];
    if (idx == 64) { sha1_transform(h->state, h->buffer); idx = 0; }
  }
}

static void ws_sha1_final(WsSha1* h, uint8_t digest[20]) {
  uint64_t bits = h->count * 8;
  uint32_t idx = (uint32_t)(h->count & 63);
  h->buffer[idx++] = 0x80;
  if (idx > 56) { while (idx < 64) h->buffer[idx++] = 0; sha1_transform(h->state, h->buffer); idx = 0; }
  while (idx < 56) h->buffer[idx++] = 0;
  h->buffer[56]=(uint8_t)(bits>>24); h->buffer[57]=(uint8_t)(bits>>16);
  h->buffer[58]=(uint8_t)(bits>>8);  h->buffer[59]=(uint8_t)bits;
  sha1_transform(h->state, h->buffer);
  for (int i = 0; i < 5; i++) {
    digest[i*4]=(uint8_t)(h->state[i]>>24); digest[i*4+1]=(uint8_t)(h->state[i]>>16);
    digest[i*4+2]=(uint8_t)(h->state[i]>>8); digest[i*4+3]=(uint8_t)h->state[i];
  }
}

static const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static TSString* compute_accept_key(TSString* key) {
  if (!key || !key->data) return ts_string_new("");
  /* SHA1(key + GUID) */
  size_t glen = strlen(WS_GUID);
  size_t klen = (size_t)key->length;
  size_t total = klen + glen;
  uint8_t* buf = (uint8_t*)malloc(total);
  if (!buf) return ts_string_new("");
  memcpy(buf, key->data, klen);
  memcpy(buf + klen, WS_GUID, glen);
  uint8_t digest[20];
  WsSha1 sha;
  ws_sha1_init(&sha);
  ws_sha1_update(&sha, buf, total);
  ws_sha1_final(&sha, digest);
  free(buf);
  return base64_encode(digest, 20);
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static void ws_set_nonblocking(int fd) {
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket(fd, FIONBIO, &mode);
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

static void ws_set_nodelay(int fd) {
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
}

/* Send raw bytes; returns bytes sent or -1 on error. */
static int ws_send_raw(int fd, const void* data, size_t len) {
  const char* p = (const char*)data;
  size_t sent = 0;
  while (sent < len) {
    int n = (int)send(fd, p + sent, (int)(len - sent), 0);
    if (n <= 0) return -1;
    sent += (size_t)n;
  }
  return (int)sent;
}

/* Receive up to `cap` bytes; returns bytes read (0=closed, -1=nothing). */
static int ws_recv(int fd, char* buf, int cap) {
  int n = (int)recv(fd, buf, cap, 0);
  return n;
}

/* Extract header value from a raw HTTP request. */
static const char* ws_get_header(const char* req, const char* name, char* buf, int bufsz) {
  const char* p = strstr(req, "\r\n");
  if (!p) return NULL;
  p += 2;
  size_t nlen = strlen(name);
  while (*p && !(p[0]=='\r' && p[1]=='\n')) {
    if (strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
      p += nlen + 1;
      while (*p == ' ') p++;
      int i = 0;
      while (*p && *p != '\r' && i < bufsz-1) buf[i++] = *p++;
      buf[i] = '\0';
      return buf;
    }
    const char* nl = strstr(p, "\r\n");
    if (!nl) break;
    p = nl + 2;
  }
  return NULL;
}

/* ------------------------------------------------------------------ */
/* Global linked-list management                                        */
/* ------------------------------------------------------------------ */
static void ws_list_add(WebSocket* ws) {
  ws->type_tag = WEBSOCKET_TAG;
  if (ws->is_server) ws->type_tag = WEBSOCKET_SERVER_TAG;
  ws->onopen = ts_value_undefined();
  ws->onmessage = ts_value_undefined();
  ws->onerror = ts_value_undefined();
  ws->onclose = ts_value_undefined();
  ws->listeners = ts_hashmap_new();
  ws->next = g_ws_list;
  g_ws_list = ws;
  g_ws_count++;
}

static void ws_list_remove(WebSocket* ws) {
  WebSocket** pp = &g_ws_list;
  while (*pp) {
    if (*pp == ws) { *pp = ws->next; g_ws_count--; return; }
    pp = &((*pp)->next);
  }
}

/* ------------------------------------------------------------------ */
/* Fire event handler                                                  */
/* ------------------------------------------------------------------ */
static void ws_fire_listeners(WebSocket* ws, const char* key, Value eventData) {
  if (!ws || !ws->listeners || !key) return;
  Value arr = ts_hashmap_get(ws->listeners, ts_string_new(key));
  if (arr.tag == TAG_ARRAY && arr.as.array) {
    for (int i = 0; i < arr.as.array->length; i++) {
      Value fn = ts_array_get(arr.as.array, i);
      if (fn.tag == TAG_FUNCTION && fn.as.function) {
        ts_value_call(fn, &eventData, 1);
      } else if (fn.tag == TAG_OBJECT && fn.as.object &&
                 *((int32_t*)fn.as.object) == BOUND_FN_TAG) {
        ts_value_call(fn, &eventData, 1);
      }
    }
  }
}

static void ws_fire_event(WebSocket* ws, const char* event, Value eventData) {
  if (!ws) return;
  Value handler = ts_value_undefined();
  if (strcmp(event,"onopen")==0)   handler = ws->onopen;
  if (strcmp(event,"onmessage")==0)handler = ws->onmessage;
  if (strcmp(event,"onerror")==0)  handler = ws->onerror;
  if (strcmp(event,"onclose")==0)  handler = ws->onclose;

  if (handler.tag == TAG_FUNCTION && handler.as.function) {
    ts_value_call(handler, &eventData, 1);
  } else if (handler.tag == TAG_OBJECT && handler.as.object &&
             *((int32_t*)handler.as.object) == BOUND_FN_TAG) {
    ts_value_call(handler, &eventData, 1);
  }

  /* addEventListener keys are bare names ("message") and also "onmessage" */
  ws_fire_listeners(ws, event, eventData);
  if (strcmp(event, "onopen") == 0) ws_fire_listeners(ws, "open", eventData);
  else if (strcmp(event, "onmessage") == 0) ws_fire_listeners(ws, "message", eventData);
  else if (strcmp(event, "onerror") == 0) ws_fire_listeners(ws, "error", eventData);
  else if (strcmp(event, "onclose") == 0) ws_fire_listeners(ws, "close", eventData);
}

/* ------------------------------------------------------------------ */
/* WebSocket event object { type, data }                               */
/* ------------------------------------------------------------------ */
static Value ws_make_event(const char* type, Value data) {
  TSHashMap* ev = ts_hashmap_new();
  ts_hashmap_set(ev, ts_string_new("type"), ts_value_string(ts_string_new(type)));
  ts_hashmap_set(ev, ts_string_new("data"), data);
  return ts_value_object(ev);
}

/* ------------------------------------------------------------------ */
/* WebSocket frame encode/decode                                       */
/* ------------------------------------------------------------------ */
/* Send a text frame. mask=1 for client→server (RFC 6455). */
static int ws_send_frame(int fd, const char* data, int len, int mask) {
  uint8_t header[14];
  int hdrlen = 0;
  header[0] = 0x81; /* FIN + text opcode */
  uint8_t mask_bit = mask ? 0x80 : 0;
  if (len < 126) {
    header[1] = (uint8_t)(mask_bit | (uint8_t)len);
    hdrlen = 2;
  } else if (len <= 0xFFFF) {
    header[1] = (uint8_t)(mask_bit | 126);
    header[2] = (uint8_t)(len >> 8);
    header[3] = (uint8_t)(len & 0xFF);
    hdrlen = 4;
  } else {
    header[1] = (uint8_t)(mask_bit | 127);
    for (int i = 0; i < 8; i++)
      header[2+i] = (uint8_t)(len >> (56 - i*8));
    hdrlen = 10;
  }
  uint8_t mask_key[4] = {0};
  if (mask) {
    for (int i = 0; i < 4; i++) mask_key[i] = (uint8_t)(rand() & 0xFF);
    memcpy(header + hdrlen, mask_key, 4);
    hdrlen += 4;
  }
  if (ws_send_raw(fd, header, (size_t)hdrlen) < 0) return -1;
  if (len > 0) {
    if (mask) {
      char* masked = (char*)malloc((size_t)len);
      if (!masked) return -1;
      for (int i = 0; i < len; i++)
        masked[i] = (char)(data[i] ^ mask_key[i & 3]);
      int r = ws_send_raw(fd, masked, (size_t)len);
      free(masked);
      if (r < 0) return -1;
    } else {
      if (ws_send_raw(fd, data, (size_t)len) < 0) return -1;
    }
  }
  return 0;
}

/* Send a close frame then close the socket. */
static void ws_send_close(int fd) {
  uint8_t frame[4] = { 0x88, 0x02, 0x03, 0xE8 }; /* FIN+close, code=1000 */
  ws_send_raw(fd, frame, 4);
}

/* ------------------------------------------------------------------ */
/* Client: TCP connect + HTTP upgrade                                  */
/* ------------------------------------------------------------------ */
static void ws_client_connect(WebSocket* ws) {
  if (!ws || !ws->url || !ws->url->data) return;
  ws_ensure_wsa();

  /* Parse "ws://host[:port][/path]" */
  const char* url = ws->url->data;
  const char* host_start = strstr(url, "://");
  if (!host_start) { ws->readyState = WS_CLOSED; return; }
  host_start += 3;

  char host[256] = {0};
  int port = 80;
  const char* path_start = strchr(host_start, '/');
  const char* colon = strchr(host_start, ':');

  if (colon && (!path_start || colon < path_start)) {
    int hlen = (int)(colon - host_start);
    if (hlen > 255) hlen = 255;
    memcpy(host, host_start, (size_t)hlen);
    host[hlen] = '\0';
    port = atoi(colon + 1);
    if (port <= 0 || port > 65535) port = 80;
  } else {
    int hlen = path_start ? (int)(path_start - host_start) : (int)strlen(host_start);
    if (hlen > 255) hlen = 255;
    memcpy(host, host_start, (size_t)hlen);
    host[hlen] = '\0';
  }

  const char* path = path_start ? path_start : "/";

  /* Resolve hostname */
  struct hostent* he = gethostbyname(host);
  if (!he || !he->h_addr_list[0]) {
    ws->readyState = WS_CLOSED;
    Value err = ws_make_event("error", ts_value_string(ts_string_new("DNS resolution failed")));
    ws->readyState = WS_CLOSED;
    ws_fire_event(ws, "onerror", err);
    ws_fire_event(ws, "onclose", ts_value_undefined());
    return;
  }

  int sock = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    ws->readyState = WS_CLOSED;
    Value err = ws_make_event("error", ts_value_string(ts_string_new("Socket creation failed")));
    ws_fire_event(ws, "onerror", err);
    ws_fire_event(ws, "onclose", ts_value_undefined());
    return;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    CLOSE_SOCKET(sock);
    ws->readyState = WS_CLOSED;
    Value err = ws_make_event("error", ts_value_string(ts_string_new("Connection failed")));
    ws_fire_event(ws, "onerror", err);
    ws_fire_event(ws, "onclose", ts_value_undefined());
    return;
  }

  ws_set_nodelay(sock);
  ws_set_nonblocking(sock);
  ws->fd = sock;

  /* Generate Sec-WebSocket-Key */
  uint8_t key_raw[16];
  srand((unsigned)(uintptr_t)ws ^ (unsigned)time(NULL));
  for (int i = 0; i < 16; i++) key_raw[i] = (uint8_t)(rand() & 0xFF);
  TSString* ws_key = base64_encode(key_raw, 16);
  TSString* accept_key = compute_accept_key(ws_key);

  /* Send HTTP upgrade request */
  char upgrade_req[2048];
  int rlen = snprintf(upgrade_req, sizeof(upgrade_req),
    "GET %s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: %.*s\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "Origin: http://%s\r\n"
    "\r\n",
    path, host, ws_key->length, ws_key->data, host);

  if (ws_send_raw(sock, upgrade_req, (size_t)rlen) < 0) {
    CLOSE_SOCKET(sock);
    ws->readyState = WS_CLOSED;
    Value err = ws_make_event("error", ts_value_string(ts_string_new("Upgrade request send failed")));
    ws_fire_event(ws, "onerror", err);
    ws_fire_event(ws, "onclose", ts_value_undefined());
    return;
  }

  /* Wait for HTTP 101 response (simplified: poll briefly) */
  ws->readyState = WS_CONNECTING;
  char resp_buf[4096];
  int resp_len = 0;
  for (int attempt = 0; attempt < 50; attempt++) {
    int n = ws_recv(sock, resp_buf + resp_len, (int)(sizeof(resp_buf) - 1 - (size_t)resp_len));
    if (n > 0) {
      resp_len += n;
      resp_buf[resp_len] = '\0';
      if (strstr(resp_buf, "\r\n\r\n")) break;
    } else if (n == 0) {
      ws->readyState = WS_CLOSED;
      CLOSE_SOCKET(sock);
      ws->fd = -1;
      Value err = ws_make_event("error", ts_value_string(ts_string_new("Connection closed during handshake")));
      ws_fire_event(ws, "onerror", err);
      ws_fire_event(ws, "onclose", ts_value_undefined());
      return;
    } else {
#ifdef _WIN32
      if (WSAGetLastError() != TS_SOCK_EWOULDBLOCK) {
#else
      if (errno != EWOULDBLOCK && errno != EINPROGRESS) {
#endif
        ws->readyState = WS_CLOSED;
        CLOSE_SOCKET(sock);
        ws->fd = -1;
        Value err = ws_make_event("error", ts_value_string(ts_string_new("Handshake recv failed")));
        ws_fire_event(ws, "onerror", err);
        ws_fire_event(ws, "onclose", ts_value_undefined());
        return;
      }
#ifdef _WIN32
      Sleep(20);
#else
      usleep(20000);
#endif
    }
  }

  /* Verify 101 Switching Protocols */
  if (strstr(resp_buf, " 101")) {
    ws->readyState = WS_OPEN;
    ws->handshake_done = 1;
    ws_fire_event(ws, "onopen", ts_value_undefined());
  } else {
    ws->readyState = WS_CLOSED;
    CLOSE_SOCKET(sock);
    ws->fd = -1;
    Value err_data = ws_make_event("error", ts_value_string(ts_string_new("Handshake failed (non-101)")));
    ws_fire_event(ws, "onerror", err_data);
    ws_fire_event(ws, "onclose", ts_value_undefined());
  }
}

/* ------------------------------------------------------------------ */
/* Server: complete HTTP 101 upgrade on existing client_fd             */
/* ------------------------------------------------------------------ */
void ts_websocket_http_upgrade(int client_fd, FetchResponse* fr,
                               const char* initial_req, int initial_len) {
  if (!fr || !fr->stream) return;
  WebSocket* wss = (WebSocket*)fr->stream;
  if (wss->type_tag != WEBSOCKET_SERVER_TAG && wss->type_tag != WEBSOCKET_TAG) return;

  /* Extract Sec-WebSocket-Key from the initial request */
  char key_buf[256] = {0};
  const char* key = ws_get_header(initial_req, "Sec-WebSocket-Key", key_buf, sizeof(key_buf));
  if (!key) {
    /* Key not found — try reading more from socket */
    char extra[4096];
    int total = initial_len;
    char* full_buf = (char*)malloc((size_t)initial_len + sizeof(extra) + 1);
    if (!full_buf) return;
    memcpy(full_buf, initial_req, (size_t)initial_len);
    for (int attempt = 0; attempt < 20; attempt++) {
      int n = ws_recv(client_fd, extra, (int)sizeof(extra));
      if (n > 0) {
        memcpy(full_buf + total, extra, (size_t)n);
        total += n;
        full_buf[total] = '\0';
        key = ws_get_header(full_buf, "Sec-WebSocket-Key", key_buf, sizeof(key_buf));
        if (key) break;
      } else {
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
      }
    }
    free(full_buf);
    if (!key) {
      /* Still no key — send 400 Bad Request */
      const char* bad = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      ws_send_raw(client_fd, bad, strlen(bad));
      CLOSE_SOCKET(client_fd);
      return;
    }
  }

  TSString* ws_key = ts_string_new(key);
  TSString* accept_key = compute_accept_key(ws_key);

  /* Build Sec-WebSocket-Protocol from response headers if set */
  char proto_buf[512] = {0};
  const char* proto = NULL;
  if (fr->headers) {
    Value pv = ts_hashmap_get(fr->headers, ts_string_new("Sec-WebSocket-Protocol"));
    if (pv.tag == TAG_STRING && pv.as.string && pv.as.string->data) {
      proto = pv.as.string->data;
      snprintf(proto_buf, sizeof(proto_buf), "Sec-WebSocket-Protocol: %s\r\n", proto);
    }
  }

  /* Send 101 Switching Protocols */
  char upgrade_resp[2048];
  int rlen = snprintf(upgrade_resp, sizeof(upgrade_resp),
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Accept: %.*s\r\n"
    "%s"
    "\r\n",
    accept_key->length, accept_key->data,
    proto ? proto_buf : "");

  ws_send_raw(client_fd, upgrade_resp, (size_t)rlen);

  /* Bind the existing WebSocketServer instance to this client socket.
   * Handlers (onmessage/onclose/…) were set on `wss` before Response(wss). */
  wss->fd = client_fd;
  wss->readyState = WS_OPEN;
  wss->handshake_done = 1;
  wss->is_server = 1;
  wss->type_tag = WEBSOCKET_SERVER_TAG;
  ws_set_nonblocking(client_fd);
  ws_set_nodelay(client_fd);
  ws_fire_event(wss, "onopen", ts_value_undefined());
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

Value ts_websocket_new(TSString* url) {
  WebSocket* ws = (WebSocket*)malloc(sizeof(WebSocket));
  memset(ws, 0, sizeof(WebSocket));
  ws->type_tag = WEBSOCKET_TAG;
  ws->fd = -1;
  ws->readyState = WS_CONNECTING;
  ws->is_server = 0;
  ws->url = url ? ts_string_new(url->data) : ts_string_new("");
  ws->next = NULL;
  ws_list_add(ws);

  /* Defer TCP connect until ts_websocket_poll so callers can assign
   * onopen/onmessage/onerror/onclose after `new WebSocket(url)`. */
  return ts_value_object(ws);
}

Value ts_websocket_server_new(void) {
  WebSocket* wss = (WebSocket*)malloc(sizeof(WebSocket));
  memset(wss, 0, sizeof(WebSocket));
  wss->type_tag = WEBSOCKET_SERVER_TAG;
  wss->fd = -1;
  wss->readyState = WS_OPEN;  /* server is "ready" once created */
  wss->is_server = 1;
  wss->url = ts_string_new("");
  wss->next = NULL;
  ws_list_add(wss);
  return ts_value_object(wss);
}

int ts_websocket_is(Value v) {
  if (v.tag != TAG_OBJECT || !v.as.object) return 0;
  int32_t tag = *((int32_t*)v.as.object);
  return tag == WEBSOCKET_TAG;
}

int ts_websocket_server_is(Value v) {
  if (v.tag != TAG_OBJECT || !v.as.object) return 0;
  int32_t tag = *((int32_t*)v.as.object);
  return tag == WEBSOCKET_SERVER_TAG;
}

WebSocket* ts_websocket_from_value(Value v) {
  if (v.tag != TAG_OBJECT || !v.as.object) return NULL;
  return (WebSocket*)v.as.object;
}

Value ts_websocket_send(Value wsVal, Value data) {
  WebSocket* ws = ts_websocket_from_value(wsVal);
  if (!ws || ws->fd < 0 || ws->readyState != WS_OPEN) return ts_value_undefined();
  TSString* s = ts_to_string(data);
  if (s && s->data && s->length > 0) {
    /* Client frames must be masked; server frames must not. */
    int mask = ws->is_server ? 0 : 1;
    ws_send_frame(ws->fd, s->data, s->length, mask);
  }
  return ts_value_undefined();
}

Value ts_websocket_close(Value wsVal, Value code, Value reason) {
  WebSocket* ws = ts_websocket_from_value(wsVal);
  if (!ws || ws->fd < 0) return ts_value_undefined();
  if (ws->readyState == WS_OPEN || ws->readyState == WS_CONNECTING) {
    ws->readyState = WS_CLOSING;
    ws_send_close(ws->fd);
    ws->readyState = WS_CLOSED;
    CLOSE_SOCKET(ws->fd);
    ws->fd = -1;
    Value ev = ts_value_undefined();
    ws_fire_event(ws, "onclose", ev);
  }
  return ts_value_undefined();
}

double ts_websocket_readyState(Value wsVal) {
  WebSocket* ws = ts_websocket_from_value(wsVal);
  if (!ws) return -1;
  return (double)ws->readyState;
}

Value ts_websocket_set_handler(Value wsVal, TSString* name, Value fn) {
  WebSocket* ws = ts_websocket_from_value(wsVal);
  if (!ws || !name || !name->data) return ts_value_undefined();
  const char* n = name->data;
  if (strcmp(n,"onopen")==0)    ws->onopen = fn;
  if (strcmp(n,"onmessage")==0) ws->onmessage = fn;
  if (strcmp(n,"onerror")==0)   ws->onerror = fn;
  if (strcmp(n,"onclose")==0)   ws->onclose = fn;
  return ts_value_undefined();
}

Value ts_websocket_get_handler(Value wsVal, TSString* name) {
  WebSocket* ws = ts_websocket_from_value(wsVal);
  if (!ws || !name || !name->data) return ts_value_undefined();
  const char* n = name->data;
  if (strcmp(n,"onopen")==0)    return ws->onopen;
  if (strcmp(n,"onmessage")==0) return ws->onmessage;
  if (strcmp(n,"onerror")==0)   return ws->onerror;
  if (strcmp(n,"onclose")==0)   return ws->onclose;
  return ts_value_undefined();
}

Value ts_websocket_add_event_listener(Value wsVal, TSString* type, Value fn) {
  WebSocket* ws = ts_websocket_from_value(wsVal);
  if (!ws || !type || !type->data) return ts_value_undefined();
  Value arr = ts_hashmap_get(ws->listeners, type);
  if (arr.tag != TAG_ARRAY || !arr.as.array) {
    TSArray* a = ts_array_new();
    ts_array_push(a, fn);
    ts_hashmap_set(ws->listeners, type, ts_value_array(a));
  } else {
    ts_array_push(arr.as.array, fn);
  }
  return ts_value_undefined();
}

Value ts_websocket_remove_event_listener(Value wsVal, TSString* type, Value fn) {
  WebSocket* ws = ts_websocket_from_value(wsVal);
  if (!ws || !type || !type->data) return ts_value_undefined();
  Value arr = ts_hashmap_get(ws->listeners, type);
  if (arr.tag == TAG_ARRAY && arr.as.array) {
    /* Remove all matching — simple pass */
    TSArray* a = arr.as.array;
    TSArray* filtered = ts_array_new();
    for (int i = 0; i < a->length; i++) {
      Value entry = ts_array_get(a, i);
      if (entry.as.function != fn.as.function)
        ts_array_push(filtered, entry);
    }
    ts_hashmap_set(ws->listeners, type, ts_value_array(filtered));
  }
  return ts_value_undefined();
}

/* ------------------------------------------------------------------ */
/* Frame receive (non-blocking)                                        */
/* ------------------------------------------------------------------ */
static void ws_recv_frames(WebSocket* ws) {
  if (!ws || ws->fd < 0 || ws->readyState != WS_OPEN) return;

  char hdr[2];
  /* Peek header bytes (2 minimum) */
  int n = ws_recv(ws->fd, hdr, 2);
  if (n == 0) { /* peer closed */
    ws->readyState = WS_CLOSED;
    CLOSE_SOCKET(ws->fd);
    ws->fd = -1;
    ws_fire_event(ws, "onclose", ts_value_undefined());
    return;
  }
  if (n < 0) return; /* nothing yet */
  if (n < 2) return; /* incomplete */

  int opcode = hdr[0] & 0x0F;
  int masked = (hdr[1] & 0x80) != 0;
  uint64_t payload_len = hdr[1] & 0x7F;

  if (payload_len == 126) {
    uint8_t ext[2];
    if (ws_recv(ws->fd, (char*)ext, 2) < 2) return;
    payload_len = ((uint64_t)ext[0] << 8) | ext[1];
  } else if (payload_len == 127) {
    uint8_t ext[8];
    if (ws_recv(ws->fd, (char*)ext, 8) < 8) return;
    payload_len = 0;
    for (int i = 0; i < 8; i++)
      payload_len = (payload_len << 8) | ext[i];
  }

  uint8_t mask[4] = {0};
  if (masked) {
    if (ws_recv(ws->fd, (char*)mask, 4) < 4) return;
  }

  /* Read payload */
  char* payload = NULL;
  if (payload_len > 0 && payload_len < 1024 * 1024) {
    payload = (char*)malloc((size_t)payload_len);
    if (!payload) return;
    size_t remaining = (size_t)payload_len;
    size_t total_read = 0;
    while (total_read < remaining) {
      int r = ws_recv(ws->fd, payload + total_read, (int)(remaining - total_read));
      if (r > 0) total_read += (size_t)r;
      else if (r == 0) { free(payload); ws->readyState = WS_CLOSED; CLOSE_SOCKET(ws->fd); ws->fd = -1; ws_fire_event(ws,"onclose",ts_value_undefined()); return; }
      else break; /* EAGAIN */
    }
  }

  /* Unmask if client frame */
  if (masked && payload) {
    for (uint64_t i = 0; i < payload_len; i++)
      payload[i] ^= mask[i & 3];
  }

  switch (opcode) {
    case 0x1: { /* text frame */
      Value data = ts_value_string(
        payload ? ts_string_new_len(payload, (int32_t)payload_len) : ts_string_new(""));
      Value ev = ws_make_event("message", data);
      ws_fire_event(ws, "onmessage", ev);
      /* Optional echo on server side is left to user handlers */
      break;
    }
    case 0x2: { /* binary frame — treat as string for now */
      Value data = ts_value_string(ts_string_new_len(payload, (int32_t)payload_len));
      Value ev = ws_make_event("message", data);
      ws_fire_event(ws, "onmessage", ev);
      break;
    }
    case 0x8: { /* close */
      ws->readyState = WS_CLOSED;
      if (ws->fd >= 0) { CLOSE_SOCKET(ws->fd); ws->fd = -1; }
      ws_fire_event(ws, "onclose", ts_value_undefined());
      break;
    }
    case 0x9: { /* ping → pong */
      if (ws->fd >= 0) {
        uint8_t pong[2] = { 0x8A, 0x00 };
        ws_send_raw(ws->fd, pong, 2);
      }
      break;
    }
    default:
      break;
  }
  if (payload) free(payload);
}

/* ------------------------------------------------------------------ */
/* Event-loop integration (called from timer/promise pump)             */
/* ------------------------------------------------------------------ */
int ts_websocket_pending(void) {
  /* Count only live / connecting sockets. Unbound server placeholders (fd < 0,
   * is_server) are kept in the list but do not alone keep the process alive —
   * HTTP server's accept loop owns the process. */
  for (WebSocket* ws = g_ws_list; ws; ws = ws->next) {
    if (ws->readyState == WS_CONNECTING) return 1; /* client about to connect or handshaking */
    if ((ws->readyState == WS_OPEN || ws->readyState == WS_CLOSING) && ws->fd >= 0) return 1;
  }
  return 0;
}

void ts_websocket_poll(void) {
  if (g_ws_polling) return; /* re-entrancy guard */
  g_ws_polling = 1;

  WebSocket* ws = g_ws_list;
  while (ws) {
    WebSocket* next_ws = ws->next;
    if (ws->readyState == WS_CONNECTING && !ws->is_server && ws->fd < 0) {
      /* Deferred client connect after handlers are registered */
      ws_client_connect(ws);
    } else if (ws->readyState == WS_OPEN && ws->fd >= 0) {
      ws_recv_frames(ws);
    } else if (ws->readyState == WS_CONNECTING && ws->fd >= 0) {
      /* Client: attempt to read upgrade response if not done yet */
      char buf[4096];
      int n = ws_recv(ws->fd, buf, (int)sizeof(buf) - 1);
      if (n > 0) {
        buf[n] = '\0';
        if (strstr(buf, " 101")) {
          ws->readyState = WS_OPEN;
          ws->handshake_done = 1;
          ws_fire_event(ws, "onopen", ts_value_undefined());
        } else {
          ws->readyState = WS_CLOSED;
          CLOSE_SOCKET(ws->fd);
          ws->fd = -1;
          ws_fire_event(ws, "onclose", ts_value_undefined());
        }
      } else if (n == 0) {
        ws->readyState = WS_CLOSED;
        CLOSE_SOCKET(ws->fd);
        ws->fd = -1;
        ws_fire_event(ws, "onclose", ts_value_undefined());
      }
    }
    ws = next_ws;
  }

  g_ws_polling = 0;
}
