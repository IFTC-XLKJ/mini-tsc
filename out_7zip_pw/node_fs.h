#ifndef NODE_FS_H
#define NODE_FS_H

#include "runtime.h"

/* Synchronous functions */
Value node_fs_readFileSync(Value path, Value options);

/* Asynchronous functions (return Value which can be awaited) */

#endif /* NODE_FS_H */
