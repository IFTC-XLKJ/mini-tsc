#ifndef NODE_FS_H
#define NODE_FS_H

#include "runtime.h"

/* Synchronous functions */
Value node_fs_readFileSync(Value path, Value options);

/* Asynchronous functions (return Value which can be awaited) */
Value node_fs_readFile(Value path, Value options);

#endif /* NODE_FS_H */
