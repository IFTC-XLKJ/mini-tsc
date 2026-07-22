#ifndef NODE_HTTP_H
#define NODE_HTTP_H

#include "runtime.h"

Value node_http_createServer(Value callback);
Value node_http_server_listen(Value serverVal, Value portVal, Value callback);

#endif /* NODE_HTTP_H */
