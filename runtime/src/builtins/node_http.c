#define _CRT_SECURE_NO_WARNINGS
#include "node_http.h"
#include "ts_features.h"

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
#include <strings.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>
#define CLOSE_SOCKET close
#define _stricmp strcasecmp
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---------- Server struct ---------- */
typedef struct {
  int type_tag;  /* 0x53525620 = 'SRV ' */
  int server_fd;
  Value callback;
} HttpServer;

#define HTTP_SERVER_TAG 0x53525620

typedef Value (*HttpRequestHandler)(Value req);
typedef Value (*HttpListenCallback)(void);

/* ---------- Thread-safe pending-request queue ---------- */
typedef struct PendingRequest {
  int client_fd;
  Value req_obj;
  char raw_buf[4096];  /* original HTTP request for WebSocket upgrade */
  int raw_len;
  struct PendingRequest* next;
} PendingRequest;

typedef struct {
  PendingRequest* head;
  PendingRequest* tail;
#ifdef _WIN32
  CRITICAL_SECTION mu;
  HANDLE event;  /* manual-reset event for WakeAllConditionVariable */
#else
  pthread_mutex_t mu;
  pthread_cond_t cv;
#endif
} PendingQueue;

static PendingQueue g_pending = {0};
static HttpServer* g_server = NULL;
static int g_server_inited = 0;

#ifdef _WIN32
static HANDLE g_accept_thread = NULL;
#else
static pthread_t g_accept_thread;
static int g_accept_thread_created = 0;
#endif

static void pq_init(PendingQueue* q) {
  q->head = q->tail = NULL;
#ifdef _WIN32
  InitializeCriticalSection(&q->mu);
  q->event = CreateEvent(NULL, TRUE /* manual reset */, FALSE /* initial state */, NULL);
#else
  pthread_mutex_init(&q->mu, NULL);
  pthread_cond_init(&q->cv, NULL);
#endif
}

static void pq_push(PendingQueue* q, PendingRequest* req) {
  req->next = NULL;
#ifdef _WIN32
  EnterCriticalSection(&q->mu);
#else
  pthread_mutex_lock(&q->mu);
#endif
  if (q->tail) q->tail->next = req;
  else q->head = req;
  q->tail = req;
#ifdef _WIN32
  SetEvent(q->event);
  LeaveCriticalSection(&q->mu);
#else
  pthread_cond_signal(&q->cv);
  pthread_mutex_unlock(&q->mu);
#endif
}

static PendingRequest* pq_pop(PendingQueue* q) {
  PendingRequest* req = NULL;
#ifdef _WIN32
  EnterCriticalSection(&q->mu);
  if (q->head) {
    req = q->head;
    q->head = req->next;
    if (!q->head) { q->tail = NULL; ResetEvent(q->event); }
  }
  LeaveCriticalSection(&q->mu);
#else
  pthread_mutex_lock(&q->mu);
  if (q->head) {
    req = q->head;
    q->head = req->next;
    if (!q->head) q->tail = NULL;
  }
  pthread_mutex_unlock(&q->mu);
#endif
  if (req) req->next = NULL;
  return req;
}

static int pq_empty(PendingQueue* q) {
  int empty;
#ifdef _WIN32
  EnterCriticalSection(&q->mu);
  empty = (q->head == NULL);
  LeaveCriticalSection(&q->mu);
#else
  pthread_mutex_lock(&q->mu);
  empty = (q->head == NULL);
  pthread_mutex_unlock(&q->mu);
#endif
  return empty;
}

/* ---------- HTTP helpers ---------- */

static void http_send_all(int client_fd, const char* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    int n = send(client_fd, data + sent, (int)(len - sent), 0);
    if (n <= 0) break;
    sent += (size_t)n;
  }
}

static void http_set_nodelay(int fd) {
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
}

static const char* http_status_text(int status) {
  if (status == 404) return "Not Found";
  if (status == 500) return "Internal Server Error";
  if (status == 201) return "Created";
  if (status == 204) return "No Content";
  if (status == 200) return "OK";
  return "OK";
}

static void http_respond(int client_fd, int status, const char* content_type,
                         const char* body, size_t body_len) {
  char header[1024];
  const char* statusText = http_status_text(status);
  if (!content_type) content_type = "application/octet-stream";
  if (!body) {
    body = "";
    body_len = 0;
  }
  int header_len = snprintf(header, sizeof(header),
    "HTTP/1.1 %d %s\r\n"
    "Content-Type: %s\r\n"
    "Content-Length: %zu\r\n"
    "Connection: close\r\n"
    "\r\n",
    status, statusText, content_type, body_len);
  if (header_len > 0) {
    http_send_all(client_fd, header, (size_t)header_len);
  }
  if (body_len > 0) {
    http_send_all(client_fd, body, body_len);
  }
}

static void http_append_custom_headers(char* buf, size_t cap, size_t* pos, TSHashMap* headers) {
  if (!headers) return;
  for (int32_t i = 0; i < headers->capacity; i++) {
    if (!headers->entries[i].occupied || !headers->entries[i].key) continue;
    const char* k = headers->entries[i].key->data;
    if (!k) continue;
    if (_stricmp(k, "Content-Length") == 0 || _stricmp(k, "content-length") == 0) continue;
    if (_stricmp(k, "Content-Type") == 0 || _stricmp(k, "content-type") == 0) continue;
    if (_stricmp(k, "Transfer-Encoding") == 0 || _stricmp(k, "transfer-encoding") == 0) continue;
    if (_stricmp(k, "Connection") == 0 || _stricmp(k, "connection") == 0) continue;
    if (_stricmp(k, "Cache-Control") == 0 || _stricmp(k, "cache-control") == 0) continue;
    if (_stricmp(k, "X-Stream-Delay-Ms") == 0) continue;
    Value v = headers->entries[i].value;
    TSString* vs = (v.tag == TAG_STRING) ? v.as.string : ts_to_string(v);
    if (!vs || !vs->data) continue;
    if (*pos + (size_t)headers->entries[i].key->length + (size_t)vs->length + 8 >= cap) break;
    *pos += (size_t)snprintf(buf + *pos, cap - *pos, "%s: %s\r\n", k, vs->data);
  }
}

static void http_send_chunk(int client_fd, const char* data, size_t len) {
  char size_line[32];
  int sl = snprintf(size_line, sizeof(size_line), "%zx\r\n", len);
  if (sl > 0) http_send_all(client_fd, size_line, (size_t)sl);
  if (len > 0 && data) http_send_all(client_fd, data, len);
  http_send_all(client_fd, "\r\n", 2);
}

static void http_send_stream_item(int client_fd, Value item) {
  const char* data = "";
  size_t len = 0;
  if (item.tag == TAG_STRING && item.as.string) {
    data = item.as.string->data ? item.as.string->data : "";
    len = (size_t)item.as.string->length;
  } else if (item.tag == TAG_OBJECT && item.as.object &&
             *((int32_t*)item.as.object) == BUFFER_TAG) {
    Buffer* b = (Buffer*)item.as.object;
    data = b->data ? (const char*)b->data : "";
    len = (size_t)(b->length > 0 ? b->length : 0);
  } else {
    TSString* s = ts_to_string(item);
    data = s && s->data ? s->data : "";
    len = s ? (size_t)s->length : 0;
  }
  http_send_chunk(client_fd, data, len);
}

static void http_respond_chunked(int client_fd, FetchResponse* fr) {
  StreamBody* sb = (StreamBody*)fr->stream;
  if (!sb || sb->type_tag != STREAM_BODY_TAG) {
    http_respond(client_fd, fr->status, "text/plain", "", 0);
    return;
  }
  if (!sb->chunks) sb->chunks = ts_array_new();

  http_set_nodelay(client_fd);

  char header[2048];
  size_t hpos = 0;
  const char* ctype = "text/plain; charset=utf-8";
  if (fr->headers) {
    Value ct = ts_hashmap_get(fr->headers, ts_string_new("Content-Type"));
    if (ct.tag != TAG_STRING) ct = ts_hashmap_get(fr->headers, ts_string_new("content-type"));
    if (ct.tag == TAG_STRING && ct.as.string && ct.as.string->data) {
      ctype = ct.as.string->data;
    }
  }

  int is_sse = (strstr(ctype, "text/event-stream") != NULL);
  if (is_sse) {
    hpos += (size_t)snprintf(header + hpos, sizeof(header) - hpos,
      "HTTP/1.1 %d %s\r\n"
      "Content-Type: text/event-stream; charset=utf-8\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Connection: keep-alive\r\n"
      "Cache-Control: no-cache, no-transform\r\n"
      "X-Accel-Buffering: no\r\n",
      fr->status,
      fr->statusText && fr->statusText->data ? fr->statusText->data : http_status_text(fr->status));
  } else {
    hpos += (size_t)snprintf(header + hpos, sizeof(header) - hpos,
      "HTTP/1.1 %d %s\r\n"
      "Content-Type: %s\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Connection: close\r\n"
      "Cache-Control: no-cache\r\n"
      "X-Accel-Buffering: no\r\n",
      fr->status,
      fr->statusText && fr->statusText->data ? fr->statusText->data : http_status_text(fr->status),
      ctype);
  }
  http_append_custom_headers(header, sizeof(header), &hpos, fr->headers);
  if (hpos + 3 < sizeof(header)) {
    header[hpos++] = '\r';
    header[hpos++] = '\n';
    header[hpos] = '\0';
  }
  http_send_all(client_fd, header, hpos);

  int delay = sb->delay_ms;
  if (delay < 0) delay = 0;

  int32_t sent = 0;
  int idle_rounds = 0;
  for (;;) {
    TSArray* chunks = sb->chunks;
    int32_t len = chunks ? chunks->length : 0;

    while (sent < len) {
      http_send_stream_item(client_fd, ts_array_get(chunks, sent));
      sent++;
      if (delay > 0 && sent < len) {
#ifdef _WIN32
        Sleep((DWORD)delay);
#else
        usleep((useconds_t)delay * 1000);
#endif
      }
    }

#if defined(TS_NEED_TIMERS)
    if (ts_timers_pending()) {
      ts_timers_poll();
      idle_rounds = 0;
      continue;
    }
#endif

    if (sent >= (chunks ? chunks->length : 0)) {
      idle_rounds++;
      if (idle_rounds >= 1) break;
    }
  }

  http_send_all(client_fd, "0\r\n\r\n", 5);
}

static int extract_response(Value result, const char** out_body, size_t* out_len,
                            const char** out_ctype, int* out_status,
                            FetchResponse** out_stream_fr) {
  *out_body = "";
  *out_len = 0;
  *out_ctype = "text/plain";
  *out_status = 200;
  *out_stream_fr = NULL;

  if (result.tag == TAG_STRING && result.as.string) {
    *out_body = result.as.string->data ? result.as.string->data : "";
    *out_len = (size_t)result.as.string->length;
    return 0;
  }

  if (result.tag == TAG_OBJECT && result.as.object) {
    int32_t tag = *((int32_t*)result.as.object);

    if (tag == BUFFER_TAG) {
      Buffer* b = (Buffer*)result.as.object;
      *out_body = b->data ? (const char*)b->data : "";
      *out_len = (size_t)(b->length > 0 ? b->length : 0);
      *out_ctype = "application/octet-stream";
      return 0;
    }

    if (tag == FETCH_RESPONSE_TAG) {
      FetchResponse* fr = (FetchResponse*)result.as.object;
      *out_status = fr->status > 0 ? fr->status : 200;
      if (fr->headers) {
        Value ct = ts_hashmap_get(fr->headers, ts_string_new("Content-Type"));
        if (ct.tag != TAG_STRING) ct = ts_hashmap_get(fr->headers, ts_string_new("content-type"));
        if (ct.tag == TAG_STRING && ct.as.string && ct.as.string->data) {
          *out_ctype = ct.as.string->data;
        }
      }
      if (fr->stream && !fr->body_complete) {
        StreamBody* sb = (StreamBody*)fr->stream;
        if (sb && sb->type_tag == STREAM_BODY_TAG) {
          *out_stream_fr = fr;
          return 1;
        }
      }
      if (fr->body) {
        *out_body = fr->body->data ? fr->body->data : "";
        *out_len = (size_t)fr->body->length;
      }
      return 0;
    }

    TSHashMap* map = (TSHashMap*)result.as.object;
    Value bodyVal = ts_hashmap_get(map, ts_string_new("body"));
    if (bodyVal.tag == TAG_STRING && bodyVal.as.string) {
      *out_body = bodyVal.as.string->data ? bodyVal.as.string->data : "";
      *out_len = (size_t)bodyVal.as.string->length;
    } else if (bodyVal.tag == TAG_OBJECT && bodyVal.as.object &&
               *((int32_t*)bodyVal.as.object) == BUFFER_TAG) {
      Buffer* b = (Buffer*)bodyVal.as.object;
      *out_body = b->data ? (const char*)b->data : "";
      *out_len = (size_t)(b->length > 0 ? b->length : 0);
      *out_ctype = "application/octet-stream";
    }
    Value headersVal = ts_hashmap_get(map, ts_string_new("headers"));
    if (headersVal.tag == TAG_OBJECT && headersVal.as.object) {
      Value ct = ts_hashmap_get((TSHashMap*)headersVal.as.object, ts_string_new("Content-Type"));
      if (ct.tag != TAG_STRING) {
        ct = ts_hashmap_get((TSHashMap*)headersVal.as.object, ts_string_new("content-type"));
      }
      if (ct.tag == TAG_STRING && ct.as.string && ct.as.string->data) {
        *out_ctype = ct.as.string->data;
      }
    }
    return 0;
  }

  if (result.tag == TAG_NULL) {
    return 0;
  }

  TSString* s = ts_to_string(result);
  *out_body = s && s->data ? s->data : "";
  *out_len = s ? (size_t)s->length : 0;
  return 0;
}

/* ---------- Parse raw HTTP request into Request object ---------- */
static Value http_parse_request(char* buf, int n) {
  buf[n] = '\0';

  char method[16] = {0};
  char path[256] = {0};
  sscanf(buf, "%15s %255s", method, path);

  char hostHeader[256] = "localhost";
  char* hostLine = strstr(buf, "Host:");
  if (!hostLine) hostLine = strstr(buf, "host:");
  if (hostLine) {
    hostLine += 5;
    while (*hostLine == ' ') hostLine++;
    int hi = 0;
    while (*hostLine && *hostLine != '\r' && *hostLine != '\n' && hi < 255) {
      hostHeader[hi++] = *hostLine++;
    }
    hostHeader[hi] = '\0';
  }

  TSHashMap* req = ts_hashmap_new();
  ts_hashmap_set(req, ts_string_new("method"), ts_value_string(ts_string_new(method)));
  ts_hashmap_set(req, ts_string_new("url"), ts_value_string(ts_string_new(path)));
  TSHashMap* headers = ts_hashmap_new();
  ts_hashmap_set(headers, ts_string_new("host"), ts_value_string(ts_string_new(hostHeader)));

  {
    char* line = strstr(buf, "\r\n");
    if (line) line += 2;
    while (line && *line && !(line[0] == '\r' && line[1] == '\n')) {
      char* next = strstr(line, "\r\n");
      char* colon = strchr(line, ':');
      if (colon && (!next || colon < next)) {
        char keybuf[128];
        char valbuf[512];
        int klen = (int)(colon - line);
        if (klen > 0 && klen < (int)sizeof(keybuf)) {
          memcpy(keybuf, line, (size_t)klen);
          keybuf[klen] = '\0';
          for (int i = 0; keybuf[i]; i++) {
            if (keybuf[i] >= 'A' && keybuf[i] <= 'Z') keybuf[i] = (char)(keybuf[i] + 32);
          }
          const char* vstart = colon + 1;
          while (*vstart == ' ' || *vstart == '\t') vstart++;
          int vlen = next ? (int)(next - vstart) : (int)strlen(vstart);
          while (vlen > 0 && (vstart[vlen - 1] == ' ' || vstart[vlen - 1] == '\t')) vlen--;
          if (vlen < 0) vlen = 0;
          if (vlen >= (int)sizeof(valbuf)) vlen = (int)sizeof(valbuf) - 1;
          memcpy(valbuf, vstart, (size_t)vlen);
          valbuf[vlen] = '\0';
          ts_hashmap_set(headers, ts_string_new(keybuf),
                         ts_value_string(ts_string_new(valbuf)));
        }
      }
      if (!next) break;
      line = next + 2;
    }
  }
  ts_hashmap_set(req, ts_string_new("headers"), ts_value_object(headers));
  return ts_value_object(req);
}

/* ---------- Send response for a parsed request ---------- */
static void http_handle_response(int client_fd, Value result) {
  const char* body = "";
  size_t body_len = 0;
  const char* ctype = "text/plain";
  int status = 200;
  FetchResponse* stream_fr = NULL;
  int is_stream = extract_response(result, &body, &body_len, &ctype, &status, &stream_fr);
  if (is_stream && stream_fr) {
    http_respond_chunked(client_fd, stream_fr);
  } else {
    http_respond(client_fd, status, ctype, body, body_len);
  }
}

/* ---------- Accept thread ---------- */
#ifdef _WIN32
static DWORD WINAPI http_accept_thread(LPVOID arg) {
  (void)arg;
#else
static void* http_accept_thread(void* arg) {
  (void)arg;
#endif
  for (;;) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = (int)accept(g_server->server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
#ifdef _WIN32
      Sleep(10);
#else
      usleep(10000);
#endif
      continue;
    }

    /* Force blocking mode for request read */
#ifdef _WIN32
    { u_long mode0 = 0; ioctlsocket(client_fd, FIONBIO, &mode0); }
#else
    { int fl = fcntl(client_fd, F_GETFL, 0); if (fl >= 0) fcntl(client_fd, F_SETFL, fl & ~O_NONBLOCK); }
#endif
    http_set_nodelay(client_fd);

    /* Read request — may need multiple recv calls for large headers */
    char buf[4096];
    int n = 0;
    for (int attempt = 0; attempt < 20; attempt++) {
      int r = recv(client_fd, buf + n, (int)(sizeof(buf) - 1 - n), 0);
      if (r > 0) {
        n += r;
        buf[n] = '\0';
        /* Check if we have complete headers (ends with \r\n\r\n) */
        if (strstr(buf, "\r\n\r\n")) break;
      } else if (r == 0) {
        break; /* client closed */
      } else {
        break; /* error */
      }
    }
    if (n <= 0) {
      CLOSE_SOCKET(client_fd);
      continue;
    }

    /* Parse and enqueue for main thread */
    Value req_obj = http_parse_request(buf, n);
    PendingRequest* pr = (PendingRequest*)malloc(sizeof(PendingRequest));
    pr->client_fd = client_fd;
    pr->req_obj = req_obj;
    memcpy(pr->raw_buf, buf, (size_t)n);
    pr->raw_buf[n] = '\0';
    pr->raw_len = n;
    pq_push(&g_pending, pr);
  }
  return 0;
}

/* ---------- Public API ---------- */

Value node_http_createServer(Value callback) {
  HttpServer* server = (HttpServer*)malloc(sizeof(HttpServer));
  server->type_tag = HTTP_SERVER_TAG;
  server->server_fd = -1;
  server->callback = callback;
  return ts_value_object(server);
}

Value node_http_server_listen(Value serverVal, Value portVal, Value callback) {
  HttpServer* server = (HttpServer*)serverVal.as.object;
  if (!server || server->type_tag != HTTP_SERVER_TAG) {
    TS_THROW(ts_value_string(ts_string_new("Invalid server object")));
    return ts_value_undefined();
  }

  int port = (int)ts_to_number(portVal);

#ifdef _WIN32
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

  server->server_fd = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (server->server_fd < 0) {
    TS_THROW(ts_value_string(ts_string_new("Failed to create socket")));
    return ts_value_undefined();
  }

  int opt = 1;
  setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons((uint16_t)port);

  if (bind(server->server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    CLOSE_SOCKET(server->server_fd);
    TS_THROW(ts_value_string(ts_string_new("Failed to bind")));
    return ts_value_undefined();
  }

  if (listen(server->server_fd, 128) < 0) {
    CLOSE_SOCKET(server->server_fd);
    TS_THROW(ts_value_string(ts_string_new("Failed to listen")));
    return ts_value_undefined();
  }

  printf("Server listening on port %d\n", port);

  /* Store server globally for accept thread */
  g_server = server;
  if (!g_server_inited) {
    pq_init(&g_pending);
    g_server_inited = 1;
  }

  /* Launch accept thread (non-blocking — returns immediately) */
#ifdef _WIN32
  g_accept_thread = CreateThread(NULL, 0, http_accept_thread, NULL, 0, NULL);
#else
  pthread_create(&g_accept_thread, NULL, http_accept_thread, NULL);
  g_accept_thread_created = 1;
#endif

  /* Run listen callback if provided */
  if (callback.tag == TAG_FUNCTION && callback.as.function) {
    HttpListenCallback listenCb = (HttpListenCallback)callback.as.function;
    listenCb();
  }

  return ts_value_undefined();
}

/* ---------- Event-loop polling (called from generated main.c) ---------- */

void node_http_server_poll(void) {
  if (!g_server || !g_server_inited) return;

  /* Drain all pending requests from the accept thread */
  for (;;) {
    PendingRequest* pr = pq_pop(&g_pending);
    if (!pr) break;

    /* Call the request handler */
    Value result;
    if (g_server->callback.tag == TAG_FUNCTION && g_server->callback.as.function) {
      HttpRequestHandler handler = (HttpRequestHandler)g_server->callback.as.function;
      fprintf(stderr, "[HTTP] Calling handler for fd=%d\n", pr->client_fd);
      result = handler(pr->req_obj);
      fprintf(stderr, "[HTTP] Handler returned: tag=%d\n", result.tag);
#if defined(TS_NEED_PROMISE)
      if (ts_value_is_promise(result)) {
        result = ts_await(result);
      }
#endif
    } else {
      result = ts_value_string(ts_string_new("Hello, World!"));
    }

    /* WebSocket upgrade check */
    if (result.tag == TAG_OBJECT && result.as.object &&
        *((int32_t*)result.as.object) == FETCH_RESPONSE_TAG) {
      FetchResponse* fr = (FetchResponse*)result.as.object;
      if (fr->stream && !fr->body_complete) {
        int32_t st = *((int32_t*)fr->stream);
        if (st == WEBSOCKET_SERVER_TAG || st == WEBSOCKET_TAG) {
          fprintf(stderr, "[HTTP] WebSocket upgrade: fd=%d raw_len=%d\n",
                  pr->client_fd, pr->raw_len);
          /* Print first 200 chars of raw request for debugging */
          {
            int show = pr->raw_len < 200 ? pr->raw_len : 200;
            fprintf(stderr, "[HTTP] raw_buf: %.*s\n", show, pr->raw_buf);
          }
          ts_websocket_http_upgrade(pr->client_fd, fr, pr->raw_buf, pr->raw_len);
          fprintf(stderr, "[HTTP] WebSocket upgrade done\n");
          ts_hashmap_free((TSHashMap*)pr->req_obj.as.object);
          free(pr);
          continue;
        }
      }
    }

    /* Send response on main thread */
    http_handle_response(pr->client_fd, result);
    CLOSE_SOCKET(pr->client_fd);
    ts_hashmap_free((TSHashMap*)pr->req_obj.as.object);
    free(pr);
  }

  /* Pump timers for deferred writers (SSE streaming, etc.) */
#if defined(TS_NEED_TIMERS)
  if (ts_timers_pending()) {
    ts_timers_poll();
  }
#endif

  /* Poll WebSocket connections for incoming messages */
  extern int ts_websocket_pending(void);
  extern void ts_websocket_poll(void);
  if (ts_websocket_pending()) {
    ts_websocket_poll();
  }
}

int node_http_server_active(void) {
  return g_server != NULL;
}

int node_http_server_pending(void) {
  return !pq_empty(&g_pending);
}

/* ---------- HTTP client ---------- */

Value node_http_request(Value options, Value callback) {
  if (options.tag != TAG_OBJECT) {
    TS_THROW(ts_value_string(ts_string_new("Options must be an object")));
    return ts_value_undefined();
  }
  TSHashMap* optionsMap = (TSHashMap*)options.as.object;
  TSString* hostname = ts_to_string(ts_hashmap_get(optionsMap, ts_string_new("hostname")));
  double port = ts_to_number(ts_hashmap_get(optionsMap, ts_string_new("port")));

  int sock = (int)socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (hostname && hostname->data) {
    inet_pton(AF_INET, hostname->data, &addr.sin_addr);
  }

  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    CLOSE_SOCKET(sock);
    TS_THROW(ts_value_string(ts_string_new("Connection failed")));
    return ts_value_undefined();
  }

  const char* req = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
  send(sock, req, (int)strlen(req), 0);

  char buf[4096];
  int n = recv(sock, buf, sizeof(buf) - 1, 0);
  CLOSE_SOCKET(sock);

  if (n > 0) {
    buf[n] = '\0';
    TSHashMap* res = ts_hashmap_new();
    ts_hashmap_set(res, ts_string_new("statusCode"), ts_value_number(200));
    ts_hashmap_set(res, ts_string_new("body"), ts_value_string(ts_string_new(buf)));
    return ts_value_object(res);
  }

  (void)callback;
  return ts_value_undefined();
}

Value node_http_get(Value url, Value callback) {
  (void)url;
  TSHashMap* options = ts_hashmap_new();
  ts_hashmap_set(options, ts_string_new("hostname"), ts_value_string(ts_string_new("127.0.0.1")));
  ts_hashmap_set(options, ts_string_new("port"), ts_value_number(80));
  return node_http_request(ts_value_object(options), callback);
}
