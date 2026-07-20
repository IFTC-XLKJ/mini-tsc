#ifndef NODE_PROCESS_H
#define NODE_PROCESS_H

#include "runtime.h"

Value node_process_env(void);
Value node_process_argv(void);
/* Capture main(argc, argv) so process.argv works on Unix/Android (no __argc/__argv). */
void node_process_set_argv(int argc, char** argv);
Value node_process_cwd(void);
int node_process_chdir(Value dir);
void node_process_exit(Value code);
int node_process_pid(void);
Value node_process_stdin(void);
Value node_process_stdout(void);
Value node_process_stderr(void);
Value node_process_stdin_on(Value event, Value callback);
Value node_process_stdout_on(Value event, Value callback);
Value node_process_stderr_on(Value event, Value callback);
Value node_process_stream_toString(Value val);
Value node_process_stdout_write(Value data);
Value node_process_stderr_write(Value data);
Value node_process_stdout_cursorTo(Value x, Value y);
Value node_process_stderr_cursorTo(Value x, Value y);
Value node_process_stdout_moveCursor(Value dx, Value dy);
Value node_process_stderr_moveCursor(Value dx, Value dy);
Value node_process_stdout_clearScreenDown(void);
Value node_process_stderr_clearScreenDown(void);
Value node_process_stdout_clearLine(Value dir);
Value node_process_stderr_clearLine(Value dir);

#endif /* NODE_PROCESS_H */
