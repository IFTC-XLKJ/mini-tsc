#include "node_events.h"
#include <string.h>
#include <stdio.h>

/* Listener entry stored as object: { fn: Function, once: boolean } */
/* Emitter: { _type, _maxListeners, _events: map eventName → array of entries } */

#define DEFAULT_MAX_LISTENERS 10

static int g_default_max_listeners = DEFAULT_MAX_LISTENERS;

/* Call listener with up to 4 args (pad with undefined). Matches typical TS callbacks. */
typedef Value (*ListenerFn)(Value a0, Value a1, Value a2, Value a3);

static TSHashMap* as_map(Value v) {
  if (v.tag != TAG_OBJECT || !v.as.object) return NULL;
  return (TSHashMap*)v.as.object;
}

static int is_emitter(Value ee) {
  TSHashMap* m = as_map(ee);
  if (!m) return 0;
  Value t = ts_hashmap_get(m, ts_string_new("_type"));
  if (t.tag == TAG_STRING && t.as.string && t.as.string->data) {
    return strcmp(t.as.string->data, "EventEmitter") == 0;
  }
  return 0;
}

static TSHashMap* ensure_events_map(TSHashMap* ee) {
  Value ev = ts_hashmap_get(ee, ts_string_new("_events"));
  if (ev.tag == TAG_OBJECT && ev.as.object) {
    return (TSHashMap*)ev.as.object;
  }
  TSHashMap* events = ts_hashmap_new();
  ts_hashmap_set(ee, ts_string_new("_events"), ts_value_object(events));
  return events;
}

static TSArray* ensure_listener_array(TSHashMap* events, TSString* eventName) {
  Value arrVal = ts_hashmap_get(events, eventName);
  if (arrVal.tag == TAG_ARRAY && arrVal.as.array) {
    return arrVal.as.array;
  }
  TSArray* arr = ts_array_new();
  ts_hashmap_set(events, eventName, ts_value_array(arr));
  return arr;
}

static Value make_listener_entry(Value fn, int once) {
  TSHashMap* entry = ts_hashmap_new();
  ts_hashmap_set(entry, ts_string_new("fn"), fn);
  ts_hashmap_set(entry, ts_string_new("once"), ts_value_boolean(once));
  return ts_value_object(entry);
}

static Value get_entry_fn(Value entry) {
  TSHashMap* m = as_map(entry);
  if (!m) return ts_value_undefined();
  return ts_hashmap_get(m, ts_string_new("fn"));
}

static int entry_is_once(Value entry) {
  TSHashMap* m = as_map(entry);
  if (!m) return 0;
  Value o = ts_hashmap_get(m, ts_string_new("once"));
  return ts_to_boolean(o);
}

static void warn_max_listeners(TSString* eventName, int count, int max) {
  if (max <= 0) return;
  if (count <= max) return;
  const char* name = (eventName && eventName->data) ? eventName->data : "?";
  fprintf(stderr,
    "MaxListenersExceededWarning: Possible EventEmitter memory leak detected. "
    "%d %s listeners added. Use emitter.setMaxListeners() to increase limit\n",
    count, name);
}

static Value add_listener(Value ee, Value event, Value listener, int once, int prepend) {
  if (!is_emitter(ee)) return ee;
  if (listener.tag != TAG_FUNCTION || !listener.as.function) return ee;

  TSHashMap* map = as_map(ee);
  TSHashMap* events = ensure_events_map(map);
  TSString* evName = ts_to_string(event);
  if (!evName) evName = ts_string_new("");

  TSArray* arr = ensure_listener_array(events, evName);
  Value entry = make_listener_entry(listener, once);

  if (prepend) {
    /* unshift: rebuild array with entry first */
    TSArray* neu = ts_array_new();
    ts_array_push(neu, entry);
    for (int i = 0; i < arr->length; i++) {
      ts_array_push(neu, ts_array_get(arr, i));
    }
    ts_hashmap_set(events, evName, ts_value_array(neu));
    arr = neu;
  } else {
    ts_array_push(arr, entry);
  }

  Value maxV = ts_hashmap_get(map, ts_string_new("_maxListeners"));
  int max = (maxV.tag == TAG_NUMBER) ? (int)maxV.as.number : g_default_max_listeners;
  warn_max_listeners(evName, arr->length, max);

  return ee;
}

static int same_fn(Value a, Value b) {
  if (a.tag != TAG_FUNCTION || b.tag != TAG_FUNCTION) return 0;
  return a.as.function == b.as.function;
}

static Value remove_listener(Value ee, Value event, Value listener) {
  if (!is_emitter(ee)) return ee;
  TSHashMap* map = as_map(ee);
  Value evMapV = ts_hashmap_get(map, ts_string_new("_events"));
  if (evMapV.tag != TAG_OBJECT || !evMapV.as.object) return ee;
  TSHashMap* events = (TSHashMap*)evMapV.as.object;
  TSString* evName = ts_to_string(event);
  Value arrVal = ts_hashmap_get(events, evName);
  if (arrVal.tag != TAG_ARRAY || !arrVal.as.array) return ee;

  TSArray* arr = arrVal.as.array;
  TSArray* neu = ts_array_new();
  int removed = 0;
  for (int i = 0; i < arr->length; i++) {
    Value entry = ts_array_get(arr, i);
    Value fn = get_entry_fn(entry);
    if (!removed && same_fn(fn, listener)) {
      removed = 1;
      continue; /* remove first match only (Node semantics) */
    }
    ts_array_push(neu, entry);
  }
  ts_hashmap_set(events, evName, ts_value_array(neu));
  return ee;
}

static void call_listener(Value fnVal, Value* args, int argc) {
  if (fnVal.tag != TAG_FUNCTION || !fnVal.as.function) return;
  Value a0 = (argc > 0) ? args[0] : ts_value_undefined();
  Value a1 = (argc > 1) ? args[1] : ts_value_undefined();
  Value a2 = (argc > 2) ? args[2] : ts_value_undefined();
  Value a3 = (argc > 3) ? args[3] : ts_value_undefined();
  ListenerFn fn = (ListenerFn)fnVal.as.function;
  fn(a0, a1, a2, a3);
}

/* ---------- public API ---------- */

Value node_events_EventEmitter(void) {
  TSHashMap* ee = ts_hashmap_new();
  ts_hashmap_set(ee, ts_string_new("_type"), ts_value_string(ts_string_new("EventEmitter")));
  ts_hashmap_set(ee, ts_string_new("_maxListeners"), ts_value_number((double)g_default_max_listeners));
  ts_hashmap_set(ee, ts_string_new("_events"), ts_value_object(ts_hashmap_new()));
  return ts_value_object(ee);
}

Value node_events_on(Value ee, Value event, Value listener) {
  return add_listener(ee, event, listener, 0, 0);
}

Value node_events_addListener(Value ee, Value event, Value listener) {
  return add_listener(ee, event, listener, 0, 0);
}

Value node_events_once(Value ee, Value event, Value listener) {
  return add_listener(ee, event, listener, 1, 0);
}

Value node_events_prependListener(Value ee, Value event, Value listener) {
  return add_listener(ee, event, listener, 0, 1);
}

Value node_events_prependOnceListener(Value ee, Value event, Value listener) {
  return add_listener(ee, event, listener, 1, 1);
}

Value node_events_off(Value ee, Value event, Value listener) {
  return remove_listener(ee, event, listener);
}

Value node_events_removeListener(Value ee, Value event, Value listener) {
  return remove_listener(ee, event, listener);
}

Value node_events_emit(Value ee, Value event, Value* args, int argc) {
  if (!is_emitter(ee)) return ts_value_boolean(0);
  TSHashMap* map = as_map(ee);
  Value evMapV = ts_hashmap_get(map, ts_string_new("_events"));
  if (evMapV.tag != TAG_OBJECT || !evMapV.as.object) return ts_value_boolean(0);
  TSHashMap* events = (TSHashMap*)evMapV.as.object;
  TSString* evName = ts_to_string(event);
  Value arrVal = ts_hashmap_get(events, evName);
  if (arrVal.tag != TAG_ARRAY || !arrVal.as.array || arrVal.as.array->length == 0) {
    /* Special: 'error' with no listeners — throw (Node behavior simplified) */
    if (evName && evName->data && strcmp(evName->data, "error") == 0) {
      Value err = (argc > 0) ? args[0] : ts_value_string(ts_string_new("Unhandled error event"));
      TS_THROW(err);
    }
    return ts_value_boolean(0);
  }

  TSArray* arr = arrVal.as.array;
  /* Snapshot listeners so once-removal / add during emit is safe */
  int n = arr->length;
  Value* snap = (Value*)malloc(sizeof(Value) * (size_t)n);
  if (!snap) return ts_value_boolean(0);
  for (int i = 0; i < n; i++) snap[i] = ts_array_get(arr, i);

  TSArray* remaining = ts_array_new();
  for (int i = 0; i < n; i++) {
    Value entry = snap[i];
    Value fn = get_entry_fn(entry);
    int once = entry_is_once(entry);
    call_listener(fn, args, argc);
    if (!once) {
      ts_array_push(remaining, entry);
    }
  }
  free(snap);
  ts_hashmap_set(events, evName, ts_value_array(remaining));
  return ts_value_boolean(1);
}

Value node_events_removeAllListeners(Value ee, Value event) {
  if (!is_emitter(ee)) return ee;
  TSHashMap* map = as_map(ee);
  /* null/undefined (both TAG_NULL in this runtime) → clear every event */
  if (event.tag == TAG_NULL) {
    ts_hashmap_set(map, ts_string_new("_events"), ts_value_object(ts_hashmap_new()));
    return ee;
  }

  Value evMapV = ts_hashmap_get(map, ts_string_new("_events"));
  if (evMapV.tag != TAG_OBJECT || !evMapV.as.object) return ee;
  TSHashMap* events = (TSHashMap*)evMapV.as.object;
  TSString* evName = ts_to_string(event);
  ts_hashmap_set(events, evName, ts_value_array(ts_array_new()));
  return ee;
}

Value node_events_listenerCount(Value ee, Value event) {
  if (!is_emitter(ee)) return ts_value_number(0);
  TSHashMap* map = as_map(ee);
  Value evMapV = ts_hashmap_get(map, ts_string_new("_events"));
  if (evMapV.tag != TAG_OBJECT || !evMapV.as.object) return ts_value_number(0);
  TSHashMap* events = (TSHashMap*)evMapV.as.object;
  Value arrVal = ts_hashmap_get(events, ts_to_string(event));
  if (arrVal.tag == TAG_ARRAY && arrVal.as.array) {
    return ts_value_number((double)arrVal.as.array->length);
  }
  return ts_value_number(0);
}

Value node_events_listeners(Value ee, Value event) {
  TSArray* out = ts_array_new();
  if (!is_emitter(ee)) return ts_value_array(out);
  TSHashMap* map = as_map(ee);
  Value evMapV = ts_hashmap_get(map, ts_string_new("_events"));
  if (evMapV.tag != TAG_OBJECT || !evMapV.as.object) return ts_value_array(out);
  TSHashMap* events = (TSHashMap*)evMapV.as.object;
  Value arrVal = ts_hashmap_get(events, ts_to_string(event));
  if (arrVal.tag == TAG_ARRAY && arrVal.as.array) {
    for (int i = 0; i < arrVal.as.array->length; i++) {
      Value fn = get_entry_fn(ts_array_get(arrVal.as.array, i));
      ts_array_push(out, fn);
    }
  }
  return ts_value_array(out);
}

Value node_events_rawListeners(Value ee, Value event) {
  /* For v1, same as listeners (no wrapper distinction beyond once flag) */
  return node_events_listeners(ee, event);
}

typedef struct {
  TSArray* names;
} EventNamesCtx;

static void collect_event_name(TSString* key, Value value, void* ctx) {
  (void)value;
  EventNamesCtx* c = (EventNamesCtx*)ctx;
  if (key) ts_array_push(c->names, ts_value_string(key));
}

Value node_events_eventNames(Value ee) {
  TSArray* names = ts_array_new();
  if (!is_emitter(ee)) return ts_value_array(names);
  TSHashMap* map = as_map(ee);
  Value evMapV = ts_hashmap_get(map, ts_string_new("_events"));
  if (evMapV.tag != TAG_OBJECT || !evMapV.as.object) return ts_value_array(names);
  EventNamesCtx ctx = { .names = names };
  ts_hashmap_for_each((TSHashMap*)evMapV.as.object, collect_event_name, &ctx);
  return ts_value_array(names);
}

Value node_events_setMaxListeners(Value ee, Value n) {
  if (!is_emitter(ee)) return ee;
  TSHashMap* map = as_map(ee);
  ts_hashmap_set(map, ts_string_new("_maxListeners"), ts_value_number(ts_to_number(n)));
  return ee;
}

Value node_events_getMaxListeners(Value ee) {
  if (!is_emitter(ee)) return ts_value_number((double)g_default_max_listeners);
  TSHashMap* map = as_map(ee);
  Value maxV = ts_hashmap_get(map, ts_string_new("_maxListeners"));
  if (maxV.tag == TAG_NUMBER) return maxV;
  return ts_value_number((double)g_default_max_listeners);
}

Value node_events_getEventListeners(Value ee, Value event) {
  return node_events_listeners(ee, event);
}

Value node_events_defaultMaxListeners(void) {
  return ts_value_number((double)g_default_max_listeners);
}

Value node_events_setDefaultMaxListeners(Value n) {
  g_default_max_listeners = (int)ts_to_number(n);
  return ts_value_number((double)g_default_max_listeners);
}
