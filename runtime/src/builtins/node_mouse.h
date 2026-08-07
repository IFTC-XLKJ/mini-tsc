#ifndef NODE_MOUSE_H
#define NODE_MOUSE_H

#include "runtime.h"

/* Register a listener for mouse events */
Value node_mouse_on(Value event, Value callback);

/* Remove a previously registered listener */
Value node_mouse_off(Value event, Value callback);

/* Start listening for mouse events (installs a global mouse hook) */
Value node_mouse_start(void);

/* Stop listening for mouse events (removes the global mouse hook) */
Value node_mouse_stop(void);

/* Get the current mouse position as { x: number, y: number } */
Value node_mouse_getPosition(void);

#endif /* NODE_MOUSE_H */
