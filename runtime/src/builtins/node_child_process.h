#ifndef NODE_CHILD_PROCESS_H
#define NODE_CHILD_PROCESS_H

#include "runtime.h"

Value node_child_process_execSync(Value command, Value options);
Value node_child_process_spawn(Value command, Value args, Value options);
Value node_child_process_exec(Value command, Value options, Value callback);
Value node_child_process_execFile(Value file, Value args, Value options, Value callback);
Value node_child_process_fork(Value modulePath, Value args, Value options);
Value node_child_process_on(Value child, Value event, Value callback);
Value node_child_process_stream_on(Value child, Value streamName, Value event, Value callback);
Value node_child_process_send(Value child, Value message);

#endif /* NODE_CHILD_PROCESS_H */
