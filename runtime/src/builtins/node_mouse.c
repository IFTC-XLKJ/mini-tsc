#include "node_mouse.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "user32.lib")

/* Global state */
static HHOOK g_mouse_hook = NULL;
static HWND g_message_hwnd = NULL;
static HANDLE g_hook_thread = NULL;
static DWORD g_hook_thread_id = 0;
static int g_running = 0;

/* Event queue (ring buffer) */
#define MOUSE_EVENT_QUEUE_SIZE 256

typedef struct {
  int x;
  int y;
  int button;       /* 0=left, 1=right, 2=middle, -1=none */
  int eventType;    /* 0=move, 1=click, 2=press, 3=release, 4=scroll */
  int delta;        /* scroll delta */
} MouseRawEvent;

typedef struct {
  MouseRawEvent events[MOUSE_EVENT_QUEUE_SIZE];
  int head;
  int tail;
  int count;
} MouseEventQueue;

static MouseEventQueue g_event_queue = {0};

/* Listeners: up to 32 event+callback pairs */
#define MOUSE_MAX_LISTENERS 32

typedef struct {
  Value callback;
  char eventFilter[32]; /* "any", "move", "click", "press", "release", "scroll", "left_click", ... */
  int active;
} MouseListener;

static MouseListener g_listeners[MOUSE_MAX_LISTENERS];
static int g_listener_count = 0;

/* Last known position */
static int g_last_x = 0;
static int g_last_y = 0;

/* Helper: push raw event into queue */
static void queue_push(MouseRawEvent* evt) {
  if (g_event_queue.count >= MOUSE_EVENT_QUEUE_SIZE) return; /* drop if full */
  int idx = (g_event_queue.head + 1) % MOUSE_EVENT_QUEUE_SIZE;
  g_event_queue.events[idx] = *evt;
  g_event_queue.head = idx;
  g_event_queue.count++;
}

/* Helper: pop raw event from queue (returns 0 if empty) */
static int queue_pop(MouseRawEvent* evt) {
  if (g_event_queue.count <= 0) return 0;
  int idx = (g_event_queue.tail + 1) % MOUSE_EVENT_QUEUE_SIZE;
  *evt = g_event_queue.events[idx];
  g_event_queue.tail = idx;
  g_event_queue.count--;
  return 1;
}

/* Map Windows wParam to our event types */
static const char* event_type_name(int type) {
  switch (type) {
    case 0: return "move";
    case 1: return "click";
    case 2: return "press";
    case 3: return "release";
    case 4: return "scroll";
    default: return "unknown";
  }
}

static const char* button_event_name(int button, int type) {
  const char* prefix = "";
  switch (button) {
    case 0: prefix = "left"; break;
    case 1: prefix = "right"; break;
    case 2: prefix = "middle"; break;
    default: prefix = ""; break;
  }
  if (button < 0) return event_type_name(type);
  switch (type) {
    case 1: /* click = press + release */
      {
        static char buf[32];
        snprintf(buf, sizeof(buf), "%s_click", prefix);
        return buf;
      }
    case 2:
      {
        static char buf[32];
        snprintf(buf, sizeof(buf), "%s_press", prefix);
        return buf;
      }
    case 3:
      {
        static char buf[32];
        snprintf(buf, sizeof(buf), "%s_release", prefix);
        return buf;
      }
    default: return event_type_name(type);
  }
}

/* Check if listener matches event */
static int listener_matches(MouseListener* l, int button, int type) {
  if (strcmp(l->eventFilter, "any") == 0) return 1;
  if (strcmp(l->eventFilter, "move") == 0 && type == 0) return 1;
  if (strcmp(l->eventFilter, "scroll") == 0 && type == 4) return 1;
  if (strcmp(l->eventFilter, "press") == 0 && type == 2) return 1;
  if (strcmp(l->eventFilter, "release") == 0 && type == 3) return 1;
  if (strcmp(l->eventFilter, "click") == 0 && type == 1) return 1;
  /* Specific button events */
  const char* name = button_event_name(button, type);
  return strcmp(l->eventFilter, name) == 0;
}

/* Build a MouseEvent hashmap and call matching listeners */
static void dispatch_event(MouseRawEvent* evt) {
  for (int i = 0; i < g_listener_count; i++) {
    MouseListener* l = &g_listeners[i];
    if (!l->active) continue;
    if (!listener_matches(l, evt->button, evt->eventType)) continue;

    /* Build event object */
    TSHashMap* obj = ts_hashmap_new();
    ts_hashmap_set(obj, ts_string_new("x"), ts_value_number((double)evt->x));
    ts_hashmap_set(obj, ts_string_new("y"), ts_value_number((double)evt->y));
    ts_hashmap_set(obj, ts_string_new("button"), ts_value_number((double)evt->button));
    ts_hashmap_set(obj, ts_string_new("eventType"), ts_value_string(ts_string_new(event_type_name(evt->eventType))));
    ts_hashmap_set(obj, ts_string_new("delta"), ts_value_number((double)evt->delta));

    Value eventVal = ts_value_object(obj);

    /* Call listener: callback(event) */
    if (l->callback.tag == TAG_FUNCTION && l->callback.as.function) {
      /* Use closure call if available, otherwise direct function pointer */
      Value args[1] = { eventVal };
      ts_value_call(l->callback, args, 1);
    }
  }
}

/* Low-level mouse hook procedure */
static LRESULT CALLBACK mouse_hook_proc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode >= 0) {
    MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lParam;
    MouseRawEvent evt = {0};
    evt.x = ms->pt.x;
    evt.y = ms->pt.y;
    g_last_x = ms->pt.x;
    g_last_y = ms->pt.y;

    switch (wParam) {
      case WM_MOUSEMOVE:
        evt.eventType = 0; /* move */
        evt.button = -1;
        queue_push(&evt);
        break;
      case WM_LBUTTONDOWN:
        evt.eventType = 2; /* press */
        evt.button = 0;    /* left */
        queue_push(&evt);
        break;
      case WM_LBUTTONUP:
        evt.eventType = 3; /* release */
        evt.button = 0;
        queue_push(&evt);
        break;
      case WM_LBUTTONDBLCLK:
        evt.eventType = 1; /* click (double) */
        evt.button = 0;
        queue_push(&evt);
        break;
      case WM_RBUTTONDOWN:
        evt.eventType = 2;
        evt.button = 1; /* right */
        queue_push(&evt);
        break;
      case WM_RBUTTONUP:
        evt.eventType = 3;
        evt.button = 1;
        queue_push(&evt);
        break;
      case WM_RBUTTONDBLCLK:
        evt.eventType = 1;
        evt.button = 1;
        queue_push(&evt);
        break;
      case WM_MBUTTONDOWN:
        evt.eventType = 2;
        evt.button = 2; /* middle */
        queue_push(&evt);
        break;
      case WM_MBUTTONUP:
        evt.eventType = 3;
        evt.button = 2;
        queue_push(&evt);
        break;
      case WM_MBUTTONDBLCLK:
        evt.eventType = 1;
        evt.button = 2;
        queue_push(&evt);
        break;
      case WM_MOUSEWHEEL:
        evt.eventType = 4; /* scroll */
        evt.button = -1;
        evt.delta = GET_WHEEL_DELTA_WPARAM(ms->mouseData) / WHEEL_DELTA;
        queue_push(&evt);
        break;
      case WM_MOUSEHWHEEL:
        evt.eventType = 4;
        evt.button = -1;
        evt.delta = GET_WHEEL_DELTA_WPARAM(ms->mouseData) / WHEEL_DELTA;
        queue_push(&evt);
        break;
    }
  }
  return CallNextHookEx(g_mouse_hook, nCode, wParam, lParam);
}

/* Background thread: install hook + run message loop */
static DWORD WINAPI hook_thread_proc(LPVOID param) {
  (void)param;
  g_mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook_proc, NULL, 0);
  if (!g_mouse_hook) {
    fprintf(stderr, "mouse: SetWindowsHookEx failed\n");
    return 1;
  }

  /* Message loop (required for low-level hooks) */
  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  UnhookWindowsHookEx(g_mouse_hook);
  g_mouse_hook = NULL;
  return 0;
}

/* --- Exported C API --- */

Value node_mouse_on(Value event, Value callback) {
  if (g_listener_count >= MOUSE_MAX_LISTENERS) {
    return ts_value_undefined();
  }

  /* Extract event filter string */
  const char* filter = "any";
  if (event.tag == TAG_STRING && event.as.string && event.as.string->data) {
    filter = event.as.string->data;
  }

  MouseListener* l = &g_listeners[g_listener_count++];
  l->callback = callback;
  strncpy(l->eventFilter, filter, sizeof(l->eventFilter) - 1);
  l->eventFilter[sizeof(l->eventFilter) - 1] = '\0';
  l->active = 1;

  return ts_value_undefined();
}

Value node_mouse_off(Value event, Value callback) {
  const char* filter = "any";
  if (event.tag == TAG_STRING && event.as.string && event.as.string->data) {
    filter = event.as.string->data;
  }

  for (int i = 0; i < g_listener_count; i++) {
    MouseListener* l = &g_listeners[i];
    if (!l->active) continue;
    if (strcmp(l->eventFilter, filter) != 0) continue;
    /* Compare callback — for simplicity, deactivate all matching filter+callback */
    /* Since we can't compare closures easily, just deactivate by filter */
    l->active = 0;
  }
  return ts_value_undefined();
}

Value node_mouse_start(void) {
  if (g_running) return ts_value_undefined();
  if (g_hook_thread) return ts_value_undefined();

  g_running = 1;
  g_hook_thread = CreateThread(NULL, 0, hook_thread_proc, NULL, 0, &g_hook_thread_id);
  if (!g_hook_thread) {
    g_running = 0;
    return ts_value_boolean(0);
  }
  return ts_value_boolean(1);
}

Value node_mouse_stop(void) {
  if (!g_running) return ts_value_undefined();
  g_running = 0;

  if (g_hook_thread) {
    /* Post WM_QUIT to exit the message loop */
    PostThreadMessage(g_hook_thread_id, WM_QUIT, 0, 0);
    WaitForSingleObject(g_hook_thread, 2000);
    CloseHandle(g_hook_thread);
    g_hook_thread = NULL;
    g_hook_thread_id = 0;
  }
  return ts_value_undefined();
}

Value node_mouse_getPosition(void) {
  TSHashMap* obj = ts_hashmap_new();
  ts_hashmap_set(obj, ts_string_new("x"), ts_value_number((double)g_last_x));
  ts_hashmap_set(obj, ts_string_new("y"), ts_value_number((double)g_last_y));
  return ts_value_object(obj);
}

#else /* POSIX stubs */

Value node_mouse_on(Value event, Value callback) {
  (void)event; (void)callback;
  return ts_value_undefined();
}

Value node_mouse_off(Value event, Value callback) {
  (void)event; (void)callback;
  return ts_value_undefined();
}

Value node_mouse_start(void) {
  return ts_value_boolean(0);
}

Value node_mouse_stop(void) {
  return ts_value_undefined();
}

Value node_mouse_getPosition(void) {
  TSHashMap* obj = ts_hashmap_new();
  ts_hashmap_set(obj, ts_string_new("x"), ts_value_number(0));
  ts_hashmap_set(obj, ts_string_new("y"), ts_value_number(0));
  return ts_value_object(obj);
}

#endif /* _WIN32 */
