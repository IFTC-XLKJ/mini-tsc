#ifndef NODE_MOUSE_H
#define NODE_MOUSE_H

#include "runtime.h"

/* Lifecycle */
Value node_mouse_start(void);
Value node_mouse_stop(void);

/* Event registration */
Value node_mouse_on(Value event, Value callback);
Value node_mouse_once(Value event, Value callback);
Value node_mouse_off(Value event, Value callback);

/* State queries */
Value node_mouse_getPosition(void);
Value node_mouse_isButtonDown(Value button);

/* Listener management */
Value node_mouse_listenerCount(Value event);

#endif /* NODE_MOUSE_H */
