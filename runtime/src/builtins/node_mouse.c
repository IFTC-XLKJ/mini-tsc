#include "node_mouse.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

#pragma comment(lib, "user32.lib")

/* ---- Internal state ---- */

typedef struct {
    TSHashMap* listeners;
    volatile long pos_x;
    volatile long pos_y;
    volatile long btn_left;
    volatile long btn_right;
    volatile long btn_middle;
    HHOOK hook;
    HANDLE thread;
    int running;
    CRITICAL_SECTION lock;
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
        InitializeCriticalSection(&g_mouse.lock);
        ensure_strings();
    }
}

static void dispatch_event(TSString* event_name, Value* args, int argc) {
    if (!g_mouse.listeners) return;
    EnterCriticalSection(&g_mouse.lock);
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
    LeaveCriticalSection(&g_mouse.lock);
}

static void add_listener(TSString* event_name, Value callback) {
    ensure_listeners();
    if (callback.tag != TAG_FUNCTION) return;
    EnterCriticalSection(&g_mouse.lock);
    Value arr_val = ts_hashmap_get(g_mouse.listeners, event_name);
    TSArray* arr;
    if (arr_val.tag == TAG_ARRAY && arr_val.as.array) {
        arr = arr_val.as.array;
    } else {
        arr = ts_array_new();
        ts_hashmap_set(g_mouse.listeners, event_name, ts_value_array(arr));
    }
    ts_array_push(arr, callback);
    LeaveCriticalSection(&g_mouse.lock);
}

static void remove_listener(TSString* event_name, Value callback) {
    if (!g_mouse.listeners) return;
    EnterCriticalSection(&g_mouse.lock);
    Value arr_val = ts_hashmap_get(g_mouse.listeners, event_name);
    if (arr_val.tag != TAG_ARRAY || !arr_val.as.array) {
        LeaveCriticalSection(&g_mouse.lock);
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
    LeaveCriticalSection(&g_mouse.lock);
}

/* ---- Hook callback ---- */

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

/* ---- Background thread ---- */

static DWORD WINAPI mouse_thread_proc(LPVOID param) {
    (void)param;
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

/* ---- Public API ---- */

Value node_mouse_start(void) {
    ensure_listeners();
    if (g_mouse.running) return ts_value_boolean(1);

    g_mouse.hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook_proc, NULL, 0);
    if (!g_mouse.hook) {
        fprintf(stderr, "mouse: SetWindowsHookEx failed (error %lu)\n", GetLastError());
        return ts_value_boolean(0);
    }

    g_mouse.thread = CreateThread(NULL, 0, mouse_thread_proc, NULL, 0, NULL);
    g_mouse.running = 1;
    return ts_value_boolean(1);
}

Value node_mouse_stop(void) {
    if (!g_mouse.running) return ts_value_boolean(1);

    if (g_mouse.hook) {
        UnhookWindowsHookEx(g_mouse.hook);
        g_mouse.hook = NULL;
    }
    if (g_mouse.thread) {
        PostThreadMessage(GetThreadId(g_mouse.thread), WM_QUIT, 0, 0);
        WaitForSingleObject(g_mouse.thread, 1000);
        CloseHandle(g_mouse.thread);
        g_mouse.thread = NULL;
    }
    g_mouse.running = 0;

    EnterCriticalSection(&g_mouse.lock);
    if (g_mouse.listeners) {
        ts_hashmap_free(g_mouse.listeners);
        g_mouse.listeners = NULL;
    }
    LeaveCriticalSection(&g_mouse.lock);

    return ts_value_boolean(1);
}

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

Value node_mouse_isButtonDown(Value button) {
    TSString* btn = ts_to_string(button);
    if (!btn || !btn->data) return ts_value_boolean(0);
    if (strcmp(btn->data, "left") == 0)
        return ts_value_boolean(g_mouse.btn_left);
    if (strcmp(btn->data, "right") == 0)
        return ts_value_boolean(g_mouse.btn_right);
    if (strcmp(btn->data, "middle") == 0)
        return ts_value_boolean(g_mouse.btn_middle);
    return ts_value_boolean(0);
}

Value node_mouse_listenerCount(Value event) {
    if (!g_mouse.listeners) return ts_value_number(0);
    TSString* evName = ts_to_string(event);
    if (!evName) return ts_value_number(0);
    EnterCriticalSection(&g_mouse.lock);
    Value arr_val = ts_hashmap_get(g_mouse.listeners, evName);
    double count = 0;
    if (arr_val.tag == TAG_ARRAY && arr_val.as.array)
        count = (double)arr_val.as.array->length;
    LeaveCriticalSection(&g_mouse.lock);
    return ts_value_number(count);
}
