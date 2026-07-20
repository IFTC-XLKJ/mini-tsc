#ifndef NODE_NET_H
#define NODE_NET_H

#include "runtime.h"

Value node_net_createServer(Value callback);
Value node_net_createConnection(Value options, Value callback);

#endif /* NODE_NET_H */
