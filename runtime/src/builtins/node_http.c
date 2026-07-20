#define _CRT_SECURE_NO_WARNINGS
#include "node_http.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#define CLOSE_SOCKET closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#define CLOSE_SOCKET close
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Server struct */
typedef struct {
  int type_tag;  /* 0x53525620 = 'SRV ' */
  int server_fd;
  Value callback;
} HttpServer;

#define HTTP_SERVER_TAG 0x53525620

typedef Value (*HttpRequestHandler)(Value req);
typedef Value (*HttpListenCallback)(void);

static void http_send_all(int client_fd, const char* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    int n = send(client_fd, data + sent, (int)(len - sent), 0);
    if (n <= 0) break;
    sent += (size_t)n;
  }
}

static void http_respond(int client_fd, int status, const char* content_type,
                         const char* body, size_t body_len) {
  char header[1024];
  const char* statusText = "OK";
  if (status == 404) statusText = "Not Found";
  else if (status == 500) statusText = "Internal Server Error";
  else if (status == 201) statusText = "Created";
  else if (status == 204) statusText = "No Content";
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

/* Extract body bytes + content-type from handler return value.
 * Supports: string, Buffer, FetchResponse, { body, headers }. */
static void extract_response(Value result, const char** out_body, size_t* out_len,
                             const char** out_ctype) {
  *out_body = "";
  *out_len = 0;
  *out_ctype = "text/plain";

  if (result.tag == TAG_STRING && result.as.string) {
    *out_body = result.as.string->data ? result.as.string->data : "";
    *out_len = (size_t)result.as.string->length;
    return;
  }

  if (result.tag == TAG_OBJECT && result.as.object) {
    int32_t tag = *((int32_t*)result.as.object);

    if (tag == BUFFER_TAG) {
      Buffer* b = (Buffer*)result.as.object;
      *out_body = b->data ? (const char*)b->data : "";
      *out_len = (size_t)(b->length > 0 ? b->length : 0);
      *out_ctype = "application/octet-stream";
      return;
    }

    if (tag == FETCH_RESPONSE_TAG) {
      FetchResponse* fr = (FetchResponse*)result.as.object;
      if (fr->body) {
        *out_body = fr->body->data ? fr->body->data : "";
        *out_len = (size_t)fr->body->length;
      }
      if (fr->headers) {
        Value ct = ts_hashmap_get(fr->headers, ts_string_new("Content-Type"));
        if (ct.tag != TAG_STRING) ct = ts_hashmap_get(fr->headers, ts_string_new("content-type"));
        if (ct.tag == TAG_STRING && ct.as.string && ct.as.string->data) {
          *out_ctype = ct.as.string->data;
        }
      }
      return;
    }

    /* HashMap with body key */
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
    return;
  }

  if (result.tag == TAG_NULL) {
    return;
  }

  TSString* s = ts_to_string(result);
  *out_body = s && s->data ? s->data : "";
  *out_len = s ? (size_t)s->length : 0;
}

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

  if (listen(server->server_fd, 10) < 0) {
    CLOSE_SOCKET(server->server_fd);
    TS_THROW(ts_value_string(ts_string_new("Failed to listen")));
    return ts_value_undefined();
  }

  printf("Server listening on port %d\n", port);

  /* Run listen callback if provided */
  if (callback.tag == TAG_FUNCTION && callback.as.function) {
    HttpListenCallback listenCb = (HttpListenCallback)callback.as.function;
    listenCb();
  }

  /* Accept loop */
  while (1) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = (int)accept(server->server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) continue;

    /* Read request */
    char buf[4096];
    int n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
      CLOSE_SOCKET(client_fd);
      continue;
    }
    buf[n] = '\0';

    /* Parse request line */
    char method[16] = {0};
    char path[256] = {0};
    sscanf(buf, "%15s %255s", method, path);

    /* Parse Host header */
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

    /* Create request object */
    TSHashMap* req = ts_hashmap_new();
    ts_hashmap_set(req, ts_string_new("method"), ts_value_string(ts_string_new(method)));
    ts_hashmap_set(req, ts_string_new("url"), ts_value_string(ts_string_new(path)));
    TSHashMap* headers = ts_hashmap_new();
    ts_hashmap_set(headers, ts_string_new("host"), ts_value_string(ts_string_new(hostHeader)));
    ts_hashmap_set(req, ts_string_new("headers"), ts_value_object(headers));

    /* Call request handler */
    if (server->callback.tag == TAG_FUNCTION && server->callback.as.function) {
      HttpRequestHandler handler = (HttpRequestHandler)server->callback.as.function;
      Value result = handler(ts_value_object(req));
      const char* body = "";
      size_t body_len = 0;
      const char* ctype = "text/plain";
      extract_response(result, &body, &body_len, &ctype);
      http_respond(client_fd, 200, ctype, body, body_len);
    } else {
      const char* fallback = "Hello, World!";
      http_respond(client_fd, 200, "text/plain", fallback, strlen(fallback));
    }

    CLOSE_SOCKET(client_fd);
    ts_hashmap_free(req);
  }

  return ts_value_undefined();
}

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
