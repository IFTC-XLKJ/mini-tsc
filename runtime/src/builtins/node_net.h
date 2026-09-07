#ifndef NODE_NET_H
#define NODE_NET_H

#include "runtime.h"

/* Top-level module functions */
Value node_net_createServer(Value callback);
Value node_net_createConnection(Value options, Value callback);
Value node_net_isIP(Value input);
Value node_net_isIPv4(Value input);
Value node_net_isIPv6(Value input);

/* Server instance methods */
Value node_net_server_listen(Value serverVal, Value portVal, Value callback);
Value node_net_server_on(Value serverVal, Value event, Value callback);
Value node_net_server_once(Value serverVal, Value event, Value callback);
Value node_net_server_off(Value serverVal, Value event, Value callback);
Value node_net_server_close(Value serverVal, Value callback);
Value node_net_server_address(Value serverVal);
Value node_net_server_getConnections(Value serverVal, Value callback);
Value node_net_server_ref(Value serverVal);
Value node_net_server_unref(Value serverVal);

/* Socket instance methods */
Value node_net_socket_write(Value self, Value data);
Value node_net_socket_end(Value self, Value data);
Value node_net_socket_destroy(Value self);
Value node_net_socket_on(Value self, Value event, Value callback);
Value node_net_socket_once(Value self, Value event, Value callback);
Value node_net_socket_off(Value self, Value event, Value callback);
Value node_net_socket_pause(Value self);
Value node_net_socket_resume(Value self);
Value node_net_socket_setEncoding(Value self, Value enc);
Value node_net_socket_setTimeout(Value self, Value ms, Value cb);
Value node_net_socket_setNoDelay(Value self, Value flag);
Value node_net_socket_setKeepAlive(Value self, Value enable, Value delay);
Value node_net_socket_ref(Value self);
Value node_net_socket_unref(Value self);
Value node_net_socket_address(Value self);

/* Event-loop integration (called from generated main.c ) */
void node_net_server_poll(void);
int  node_net_server_active(void);
int  node_net_server_pending(void);

#endif /* NODE_NET_H */
