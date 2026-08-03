#ifndef NODE_PROCESS_H
#define NODE_PROCESS_H

#include "runtime.h"

Value node_process_env(void);
/* Capture main(argc, argv) so process.argv works on Unix/Android (no __argc/__argv). */
void node_process_set_argv(int argc, char** argv);

#endif /* NODE_PROCESS_H */
