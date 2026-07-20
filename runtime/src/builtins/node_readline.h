#ifndef NODE_READLINE_H
#define NODE_READLINE_H

#include "runtime.h"

/* Module API */
Value node_readline_createInterface(Value options);
Value node_readline_question(Value rl, Value query, Value callback);
Value node_readline_close(Value rl);
Value node_readline_on(Value rl, Value event, Value callback);
Value node_readline_prompt(Value rl);
Value node_readline_setPrompt(Value rl, Value prompt);
Value node_readline_write(Value rl, Value data);
Value node_readline_pause(Value rl);
Value node_readline_resume(Value rl);
Value node_readline_getPrompt(Value rl);

/* Static helpers (optional Node API surface) */
void node_readline_clearLine(Value stream, Value dir);
void node_readline_cursorTo(Value stream, Value x, Value y);
void node_readline_moveCursor(Value stream, Value dx, Value dy);

#endif /* NODE_READLINE_H */
