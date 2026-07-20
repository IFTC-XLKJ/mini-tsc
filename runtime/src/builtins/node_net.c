#include "node_net.h"

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

Value node_net_createServer(Value callback) {
  TSHashMap* server = ts_hashmap_new();
  ts_hashmap_set(server, ts_string_new("_callback"), callback);

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    TS_THROW(ts_value_string(ts_string_new("Failed to create socket")));
    return ts_value_undefined();
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

  ts_hashmap_set(server, ts_string_new("_fd"), ts_value_number((double)server_fd));
  return ts_value_object(server);
}

Value node_net_createConnection(Value options, Value callback) {
  TSHashMap* optionsMap = (TSHashMap*)options.as.object;
  TSString* host = ts_to_string(ts_hashmap_get(optionsMap, ts_string_new("host")));
  double port = ts_to_number(ts_hashmap_get(optionsMap, ts_string_new("port")));

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    TS_THROW(ts_value_string(ts_string_new("Failed to create socket")));
    return ts_value_undefined();
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  inet_pton(AF_INET, host->data, &addr.sin_addr);

  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    CLOSE_SOCKET(sock);
    TS_THROW(ts_value_string(ts_string_new("Connection failed")));
    return ts_value_undefined();
  }

  TSHashMap* socket = ts_hashmap_new();
  ts_hashmap_set(socket, ts_string_new("_fd"), ts_value_number((double)sock));
  return ts_value_object(socket);
}
