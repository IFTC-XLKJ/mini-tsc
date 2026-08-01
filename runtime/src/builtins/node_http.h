#ifndef NODE_HTTP_H
#define NODE_HTTP_H

#include "runtime.h"

Value node_http_createServer(Value callback);
Value node_http_server_listen(Value serverVal, Value portVal, Value callback);
Value node_http_server_on(Value serverVal, Value event, Value callback);
Value node_http_server_close(Value serverVal, Value callback);
Value node_http_request(Value options, Value callback);
Value node_http_get(Value url, Value callback);

/* Non-blocking poll API for GUI event-loop integration */
int node_http_server_active(void);
int node_http_server_pending(void);
void node_http_server_poll(void);

#endif /* NODE_HTTP_H */
