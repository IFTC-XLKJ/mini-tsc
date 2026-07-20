#ifndef NODE_EVENTS_H
#define NODE_EVENTS_H

#include "runtime.h"

/* Constructor: new events.EventEmitter() / events.EventEmitter() */
Value node_events_EventEmitter(void);

/* Instance methods (first arg = emitter Value) */
Value node_events_on(Value ee, Value event, Value listener);
Value node_events_addListener(Value ee, Value event, Value listener);
Value node_events_once(Value ee, Value event, Value listener);
Value node_events_off(Value ee, Value event, Value listener);
Value node_events_removeListener(Value ee, Value event, Value listener);
Value node_events_prependListener(Value ee, Value event, Value listener);
Value node_events_prependOnceListener(Value ee, Value event, Value listener);
Value node_events_emit(Value ee, Value event, Value* args, int argc);
Value node_events_removeAllListeners(Value ee, Value event);
Value node_events_listenerCount(Value ee, Value event);
Value node_events_listeners(Value ee, Value event);
Value node_events_rawListeners(Value ee, Value event);
Value node_events_eventNames(Value ee);
Value node_events_setMaxListeners(Value ee, Value n);
Value node_events_getMaxListeners(Value ee);

/* Module-level helpers */
Value node_events_getEventListeners(Value ee, Value event);
Value node_events_defaultMaxListeners(void);
Value node_events_setDefaultMaxListeners(Value n);

#endif /* NODE_EVENTS_H */
