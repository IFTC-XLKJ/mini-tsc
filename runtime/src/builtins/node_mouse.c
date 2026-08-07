#include "node_mouse.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "user32.lib")
#else
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#endif

/* ---- Cross-platform primitives ---- */

#ifdef _WIN32
#define MOUSE_MUTEX          CRITICAL_SECTION
#define MOUSE_MUTEX_INIT(m)  InitializeCriticalSection(&(m))
#define MOUSE_MUTEX_LOCK(m)  EnterCriticalSection(&(m))
#define MOUSE_MUTEX_UNLOCK(m) LeaveCriticalSection(&(m))
#define MOUSE_MUTEX_DESTROY(m) DeleteCriticalSection(&(m))
#define MOUSE_THREAD_T       HANDLE
#define MOUSE_THREAD_CREATE(t, fn, arg) \
    (t) = CreateThread(NULL, 0, (fn), (arg), 0, NULL)
#define MOUSE_THREAD_JOIN(t) \
    WaitForSingleObject((t), 2000); CloseHandle((t))
#define MOUSE_THREAD_SIGNAL  volatile long
#define MOUSE_SIGNAL_SET(dst, val) InterlockedExchange(&(dst), (val))
#define MOUSE_SIGNAL_GET(dst)     (dst)
#else
#define MOUSE_MUTEX          pthread_mutex_t
#define MOUSE_MUTEX_INIT(m)  pthread_mutex_init(&(m), NULL)
#define MOUSE_MUTEX_LOCK(m)  pthread_mutex_lock(&(m))
#define MOUSE_MUTEX_UNLOCK(m) pthread_mutex_unlock(&(m))
#define MOUSE_MUTEX_DESTROY(m) pthread_mutex_destroy(&(m))
#define MOUSE_THREAD_T       pthread_t
#define MOUSE_THREAD_CREATE(t, fn, arg) \
    pthread_create(&(t), NULL, (fn), (arg))
#define MOUSE_THREAD_JOIN(t) pthread_join((t), NULL)
#define MOUSE_THREAD_SIGNAL  volatile int
#define MOUSE_SIGNAL_SET(dst, val) __sync_lock_test_and_set(&(dst), (val))
#define MOUSE_SIGNAL_GET(dst)     (dst)
#endif

/* ---- Internal state ---- */

typedef struct {
    TSHashMap* listeners;
    volatile long pos_x;
    volatile long pos_y;
    volatile long btn_left;
    volatile long btn_right;
    volatile long btn_middle;
    MOUSE_THREAD_T thread;
    MOUSE_THREAD_SIGNAL running;
    MOUSE_MUTEX lock;
#ifdef _WIN32
    HHOOK hook;
#endif
#ifdef __linux__
    Display* x_display;
    int xi_opcode;
    int x_error;
#endif
} MouseState;

static MouseState g_mouse = {0};

/* Interned event name strings */
static TSString* EV_MOUSEMOVE;
static TSString* EV_MOUSEDOWN;
static TSString* EV_MOUSEUP;
static TSString* EV_CLICK;
static TSString* EV_WHEEL;
static int g_strings_initialized = 0;

static void ensure_strings(void) {
    if (!g_strings_initialized) {
        EV_MOUSEMOVE = ts_string_new("mousemove");
        EV_MOUSEDOWN = ts_string_new("mousedown");
        EV_MOUSEUP   = ts_string_new("mouseup");
        EV_CLICK     = ts_string_new("click");
        EV_WHEEL     = ts_string_new("wheel");
        g_strings_initialized = 1;
    }
}

/* ---- Listener management ---- */

static void ensure_listeners(void) {
    if (!g_mouse.listeners) {
        g_mouse.listeners = ts_hashmap_new();
        MOUSE_MUTEX_INIT(g_mouse.lock);
        ensure_strings();
    }
}

static void dispatch_event(TSString* event_name, Value* args, int argc) {
    if (!g_mouse.listeners) return;
    MOUSE_MUTEX_LOCK(g_mouse.lock);
    Value arr_val = ts_hashmap_get(g_mouse.listeners, event_name);
    if (arr_val.tag == TAG_ARRAY && arr_val.as.array) {
        TSArray* arr = arr_val.as.array;
        for (int i = 0; i < arr->length; i++) {
            Value fn = ts_array_get(arr, i);
            if (fn.tag == TAG_FUNCTION && fn.as.function) {
                ts_value_call(fn, args, argc);
            }
        }
    }
    MOUSE_MUTEX_UNLOCK(g_mouse.lock);
}

static void add_listener(TSString* event_name, Value callback) {
    ensure_listeners();
    if (callback.tag != TAG_FUNCTION) return;
    MOUSE_MUTEX_LOCK(g_mouse.lock);
    Value arr_val = ts_hashmap_get(g_mouse.listeners, event_name);
    TSArray* arr;
    if (arr_val.tag == TAG_ARRAY && arr_val.as.array) {
        arr = arr_val.as.array;
    } else {
        arr = ts_array_new();
        ts_hashmap_set(g_mouse.listeners, event_name, ts_value_array(arr));
    }
    ts_array_push(arr, callback);
    MOUSE_MUTEX_UNLOCK(g_mouse.lock);
}

static void remove_listener(TSString* event_name, Value callback) {
    if (!g_mouse.listeners) return;
    MOUSE_MUTEX_LOCK(g_mouse.lock);
    Value arr_val = ts_hashmap_get(g_mouse.listeners, event_name);
    if (arr_val.tag != TAG_ARRAY || !arr_val.as.array) {
        MOUSE_MUTEX_UNLOCK(g_mouse.lock);
        return;
    }
    TSArray* old = arr_val.as.array;
    TSArray* neu = ts_array_new();
    for (int i = 0; i < old->length; i++) {
        Value fn = ts_array_get(old, i);
        if (fn.tag != callback.tag || fn.as.function != callback.as.function) {
            ts_array_push(neu, fn);
        }
    }
    ts_hashmap_set(g_mouse.listeners, event_name, ts_value_array(neu));
    MOUSE_MUTEX_UNLOCK(g_mouse.lock);
}

/* ====================================================================== */
/*  Windows implementation                                                */
/* ====================================================================== */
#ifdef _WIN32

static LRESULT CALLBACK mouse_hook_proc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        MSLLHOOKSTRUCT* info = (MSLLHOOKSTRUCT*)lParam;

        InterlockedExchange(&g_mouse.pos_x, info->pt.x);
        InterlockedExchange(&g_mouse.pos_y, info->pt.y);

        TSString* ev = NULL;
        Value args[4];
        int argc = 0;

        switch (wParam) {
            case WM_MOUSEMOVE:
                ev = EV_MOUSEMOVE;
                args[0] = ts_value_number((double)info->pt.x);
                args[1] = ts_value_number((double)info->pt.y);
                argc = 2;
                break;

            case WM_LBUTTONDOWN:
                InterlockedExchange(&g_mouse.btn_left, 1);
                ev = EV_MOUSEDOWN;
                args[0] = ts_value_string(ts_string_new("left"));
                args[1] = ts_value_boolean(1);
                args[2] = ts_value_number((double)info->pt.x);
                args[3] = ts_value_number((double)info->pt.y);
                argc = 4;
                break;
            case WM_LBUTTONUP:
                InterlockedExchange(&g_mouse.btn_left, 0);
                ev = EV_MOUSEUP;
                args[0] = ts_value_string(ts_string_new("left"));
                args[1] = ts_value_boolean(0);
                args[2] = ts_value_number((double)info->pt.x);
                args[3] = ts_value_number((double)info->pt.y);
                argc = 4;
                break;

            case WM_RBUTTONDOWN:
                InterlockedExchange(&g_mouse.btn_right, 1);
                ev = EV_MOUSEDOWN;
                args[0] = ts_value_string(ts_string_new("right"));
                args[1] = ts_value_boolean(1);
                args[2] = ts_value_number((double)info->pt.x);
                args[3] = ts_value_number((double)info->pt.y);
                argc = 4;
                break;
            case WM_RBUTTONUP:
                InterlockedExchange(&g_mouse.btn_right, 0);
                ev = EV_MOUSEUP;
                args[0] = ts_value_string(ts_string_new("right"));
                args[1] = ts_value_boolean(0);
                args[2] = ts_value_number((double)info->pt.x);
                args[3] = ts_value_number((double)info->pt.y);
                argc = 4;
                break;

            case WM_MBUTTONDOWN:
                InterlockedExchange(&g_mouse.btn_middle, 1);
                ev = EV_MOUSEDOWN;
                args[0] = ts_value_string(ts_string_new("middle"));
                args[1] = ts_value_boolean(1);
                args[2] = ts_value_number((double)info->pt.x);
                args[3] = ts_value_number((double)info->pt.y);
                argc = 4;
                break;
            case WM_MBUTTONUP:
                InterlockedExchange(&g_mouse.btn_middle, 0);
                ev = EV_MOUSEUP;
                args[0] = ts_value_string(ts_string_new("middle"));
                args[1] = ts_value_boolean(0);
                args[2] = ts_value_number((double)info->pt.x);
                args[3] = ts_value_number((double)info->pt.y);
                argc = 4;
                break;

            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
                ev = EV_WHEEL;
                args[0] = ts_value_number((double)GET_WHEEL_DELTA_WPARAM(info->mouseData));
                argc = 1;
                break;
        }

        if (ev) dispatch_event(ev, args, argc);
    }
    return CallNextHookEx(g_mouse.hook, nCode, wParam, lParam);
}

static DWORD WINAPI mouse_thread_proc(LPVOID param) {
    (void)param;
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

Value node_mouse_start(void) {
    ensure_listeners();
    if (MOUSE_SIGNAL_GET(g_mouse.running)) return ts_value_boolean(1);

    g_mouse.hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook_proc, NULL, 0);
    if (!g_mouse.hook) {
        fprintf(stderr, "mouse: SetWindowsHookEx failed (error %lu)\n", GetLastError());
        return ts_value_boolean(0);
    }

    MOUSE_THREAD_CREATE(g_mouse.thread, mouse_thread_proc, NULL);
    MOUSE_SIGNAL_SET(g_mouse.running, 1);
    return ts_value_boolean(1);
}

Value node_mouse_stop(void) {
    if (!MOUSE_SIGNAL_GET(g_mouse.running)) return ts_value_boolean(1);

    if (g_mouse.hook) {
        UnhookWindowsHookEx(g_mouse.hook);
        g_mouse.hook = NULL;
    }
    if (g_mouse.thread) {
        PostThreadMessage(GetThreadId(g_mouse.thread), WM_QUIT, 0, 0);
        MOUSE_THREAD_JOIN(g_mouse.thread);
        g_mouse.thread = NULL;
    }
    MOUSE_SIGNAL_SET(g_mouse.running, 0);

    MOUSE_MUTEX_LOCK(g_mouse.lock);
    if (g_mouse.listeners) {
        ts_hashmap_free(g_mouse.listeners);
        g_mouse.listeners = NULL;
    }
    MOUSE_MUTEX_UNLOCK(g_mouse.lock);

    return ts_value_boolean(1);
}

Value node_mouse_getPosition(void) {
    POINT pt;
    TSHashMap* obj = ts_hashmap_new();
    if (GetCursorPos(&pt)) {
        ts_hashmap_set(obj, ts_string_new("x"), ts_value_number((double)pt.x));
        ts_hashmap_set(obj, ts_string_new("y"), ts_value_number((double)pt.y));
    } else {
        ts_hashmap_set(obj, ts_string_new("x"), ts_value_number(0));
        ts_hashmap_set(obj, ts_string_new("y"), ts_value_number(0));
    }
    return ts_value_object(obj);
}

/* ====================================================================== */
/*  Linux / X11 implementation                                             */
/* ====================================================================== */
#else

static int xi_error_handler(Display* dpy, XErrorEvent* err) {
    (void)dpy;
    g_mouse.x_error = err->error_code;
    return 0;
}

static int x_io_error_handler(Display* dpy) {
    (void)dpy;
    /* X connection lost — stop the loop */
    MOUSE_SIGNAL_SET(g_mouse.running, 0);
    return 0;
}

static int query_xi_opcode(Display* dpy) {
    int xi_major opcode, first_event, first_error;
    if (XQueryExtension(dpy, "XInputExtension", &opcode, &first_event, &first_error)) {
        int major = 2, minor = 0;
        if (XIQueryVersion(dpy, &major, &minor) == Success)
            return opcode;
    }
    return -1;
}

static void dispatch_xi_event(XIDeviceInfo* dev, int detail, int x, int y, int flags) {
    (void)dev;
    TSString* ev = NULL;
    Value args[4];
    int argc = 0;

    /* XI_RawMotion */
    if (detail == 6) {
        ev = EV_MOUSEMOVE;
        args[0] = ts_value_number((double)x);
        args[1] = ts_value_number((double)y);
        argc = 2;
    }
    /* XI_RawButtonPress */
    else if (detail == 3) {
        int btn = flags; /* button number */
        if (btn == 1) { MOUSE_SIGNAL_SET(g_mouse.btn_left, 1); }
        else if (btn == 2) { MOUSE_SIGNAL_SET(g_mouse.btn_middle, 1); }
        else if (btn == 3) { MOUSE_SIGNAL_SET(g_mouse.btn_right, 1); }
        const char* btn_name = (btn == 1) ? "left" : (btn == 2) ? "middle" : (btn == 3) ? "right" : "unknown";
        ev = EV_MOUSEDOWN;
        args[0] = ts_value_string(ts_string_new(btn_name));
        args[1] = ts_value_boolean(1);
        args[2] = ts_value_number((double)x);
        args[3] = ts_value_number((double)y);
        argc = 4;
    }
    /* XI_RawButtonRelease */
    else if (detail == 4) {
        int btn = flags;
        if (btn == 1) { MOUSE_SIGNAL_SET(g_mouse.btn_left, 0); }
        else if (btn == 2) { MOUSE_SIGNAL_SET(g_mouse.btn_middle, 0); }
        else if (btn == 3) { MOUSE_SIGNAL_SET(g_mouse.btn_right, 0); }
        const char* btn_name = (btn == 1) ? "left" : (btn == 2) ? "middle" : (btn == 3) ? "right" : "unknown";
        ev = EV_MOUSEUP;
        args[0] = ts_value_string(ts_string_new(btn_name));
        args[1] = ts_value_boolean(0);
        args[2] = ts_value_number((double)x);
        args[3] = ts_value_number((double)y);
        argc = 4;
    }
    /* XI_RawButtonPress on scroll (buttons 4/5 = vertical, 6/7 = horizontal) */
    else if (detail == 5) {
        int btn = flags;
        long delta = 0;
        if (btn == 4) delta = 120;       /* scroll up */
        else if (btn == 5) delta = -120;  /* scroll down */
        else if (btn == 6) delta = 120;   /* scroll right */
        else if (btn == 7) delta = -120;  /* scroll left */
        if (delta != 0) {
            ev = EV_WHEEL;
            args[0] = ts_value_number((double)delta);
            argc = 1;
        }
    }

    if (ev) dispatch_event(ev, args, argc);
}

static void* x11_mouse_thread(void* arg) {
    (void)arg;

    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "mouse: cannot open X11 display\n");
        MOUSE_SIGNAL_SET(g_mouse.running, 0);
        return NULL;
    }
    g_mouse.x_display = dpy;

    XSetErrorHandler(xi_error_handler);
    XSetIOErrorHandler(x_io_error_handler);

    g_mouse.xi_opcode = query_xi_opcode(dpy);
    if (g_mouse.xi_opcode < 0) {
        fprintf(stderr, "mouse: XInput2 not available\n");
        XCloseDisplay(dpy);
        g_mouse.x_display = NULL;
        MOUSE_SIGNAL_SET(g_mouse.running, 0);
        return NULL;
    }

    /* Select raw input events on root window */
    Window root = DefaultRootWindow(dpy);
    unsigned char mask[XIMaskLen(XI_LASTEVENT)] = {0};
    XISetMask(mask, XI_RawMotion);
    XISetMask(mask, XI_RawButtonPress);
    XISetMask(mask, XI_RawButtonRelease);

    XISelectEvents(dpy, root, &(XIEventMask){
        .deviceid = XIAllDevices,
        .mask_len = sizeof(mask),
        .mask = mask
    });
    XSync(dpy, False);

    /* Main event loop */
    while (MOUSE_SIGNAL_GET(g_mouse.running)) {
        /* Non-blocking: check for events with XPending, then flush */
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.xcookie.type != g_mouse.xi_opcode) continue;

            XGenericEvent* xge = (XGenericEvent*)&ev;
            if (xge->evtype == XI_RawMotion) {
                XIRawEvent* raw = (XIRawEvent*)xge;
                int x = (int)raw->raw_values[0];
                int y = (int)raw->raw_values[1];
                MOUSE_SIGNAL_SET(g_mouse.pos_x, x);
                MOUSE_SIGNAL_SET(g_mouse.pos_y, y);
                dispatch_xi_event(NULL, 6, x, y, 0);
            } else if (xge->evtype == XI_RawButtonPress) {
                XIRawEvent* raw = (XIRawEvent*)xge;
                int x = (int)raw->raw_values[0];
                int y = (int)raw->raw_values[1];
                MOUSE_SIGNAL_SET(g_mouse.pos_x, x);
                MOUSE_SIGNAL_SET(g_mouse.pos_y, y);
                dispatch_xi_event(NULL, 3, x, y, (int)raw->detail);
            } else if (xge->evtype == XI_RawButtonRelease) {
                XIRawEvent* raw = (XIRawEvent*)xge;
                int x = (int)raw->raw_values[0];
                int y = (int)raw->raw_values[1];
                MOUSE_SIGNAL_SET(g_mouse.pos_x, x);
                MOUSE_SIGNAL_SET(g_mouse.pos_y, y);
                dispatch_xi_event(NULL, 4, x, y, (int)raw->detail);
            }
        }
        usleep(1000); /* 1ms poll interval */
    }

    XCloseDisplay(dpy);
    g_mouse.x_display = NULL;
    return NULL;
}

Value node_mouse_start(void) {
    ensure_listeners();
    if (MOUSE_SIGNAL_GET(g_mouse.running)) return ts_value_boolean(1);

    MOUSE_SIGNAL_SET(g_mouse.running, 1);
    int rc = pthread_create(&g_mouse.thread, NULL, x11_mouse_thread, NULL);
    if (rc != 0) {
        fprintf(stderr, "mouse: pthread_create failed (%d)\n", rc);
        MOUSE_SIGNAL_SET(g_mouse.running, 0);
        return ts_value_boolean(0);
    }
    return ts_value_boolean(1);
}

Value node_mouse_stop(void) {
    if (!MOUSE_SIGNAL_GET(g_mouse.running)) return ts_value_boolean(1);

    MOUSE_SIGNAL_SET(g_mouse.running, 0);
    MOUSE_THREAD_JOIN(g_mouse.thread);

    MOUSE_MUTEX_LOCK(g_mouse.lock);
    if (g_mouse.listeners) {
        ts_hashmap_free(g_mouse.listeners);
        g_mouse.listeners = NULL;
    }
    MOUSE_MUTEX_UNLOCK(g_mouse.lock);

    return ts_value_boolean(1);
}

Value node_mouse_getPosition(void) {
    TSHashMap* obj = ts_hashmap_new();
    if (g_mouse.x_display) {
        /* Use cached position from hook events */
        ts_hashmap_set(obj, ts_string_new("x"), ts_value_number((double)MOUSE_SIGNAL_GET(g_mouse.pos_x)));
        ts_hashmap_set(obj, ts_string_new("y"), ts_value_number((double)MOUSE_SIGNAL_GET(g_mouse.pos_y)));
    } else {
        /* Fallback: query X11 directly */
        Display* dpy = XOpenDisplay(NULL);
        if (dpy) {
            Window root_ret, child_ret;
            int rx, ry, wx, wy;
            unsigned int mask;
            if (XQueryPointer(dpy, DefaultRootWindow(dpy), &root_ret, &child_ret, &rx, &ry, &wx, &wy, &mask)) {
                ts_hashmap_set(obj, ts_string_new("x"), ts_value_number((double)rx));
                ts_hashmap_set(obj, ts_string_new("y"), ts_value_number((double)ry));
            }
            XCloseDisplay(dpy);
        } else {
            ts_hashmap_set(obj, ts_string_new("x"), ts_value_number(0));
            ts_hashmap_set(obj, ts_string_new("y"), ts_value_number(0));
        }
    }
    return ts_value_object(obj);
}

#endif /* _WIN32 */

/* ====================================================================== */
/*  Platform-independent public API                                       */
/* ====================================================================== */

Value node_mouse_on(Value event, Value callback) {
    ensure_listeners();
    TSString* evName = ts_to_string(event);
    if (!evName) return ts_value_undefined();
    add_listener(evName, callback);
    return ts_value_undefined();
}

Value node_mouse_once(Value event, Value callback) {
    ensure_listeners();
    TSString* evName = ts_to_string(event);
    if (!evName) return ts_value_undefined();
    add_listener(evName, callback);
    return ts_value_undefined();
}

Value node_mouse_off(Value event, Value callback) {
    TSString* evName = ts_to_string(event);
    if (!evName) return ts_value_undefined();
    remove_listener(evName, callback);
    return ts_value_undefined();
}

Value node_mouse_isButtonDown(Value button) {
    TSString* btn = ts_to_string(button);
    if (!btn || !btn->data) return ts_value_boolean(0);
    if (strcmp(btn->data, "left") == 0)
        return ts_value_boolean(MOUSE_SIGNAL_GET(g_mouse.btn_left));
    if (strcmp(btn->data, "right") == 0)
        return ts_value_boolean(MOUSE_SIGNAL_GET(g_mouse.btn_right));
    if (strcmp(btn->data, "middle") == 0)
        return ts_value_boolean(MOUSE_SIGNAL_GET(g_mouse.btn_middle));
    return ts_value_boolean(0);
}

Value node_mouse_listenerCount(Value event) {
    if (!g_mouse.listeners) return ts_value_number(0);
    TSString* evName = ts_to_string(event);
    if (!evName) return ts_value_number(0);
    MOUSE_MUTEX_LOCK(g_mouse.lock);
    Value arr_val = ts_hashmap_get(g_mouse.listeners, evName);
    double count = 0;
    if (arr_val.tag == TAG_ARRAY && arr_val.as.array)
        count = (double)arr_val.as.array->length;
    MOUSE_MUTEX_UNLOCK(g_mouse.lock);
    return ts_value_number(count);
}
