#ifndef NODE_UUID_H
#define NODE_UUID_H

#include "runtime.h"

Value node_uuid_v4(void);
Value node_uuid_validate(TSString* uuid);
Value node_uuid_v7(void);

#endif /* NODE_UUID_H */
