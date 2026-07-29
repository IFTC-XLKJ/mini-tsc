#include "node_webview.h"
#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>

#pragma comment(lib, "ws2_32.lib")

#define CINTERFACE
#define COBJMACROS
#include "WebView2.h"

#pragma comment(lib, "WebView2Loader.dll.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

/* WebView2 instance data */
typedef struct {
  HWND hwnd;
  ICoreWebView2Controller* controller;
  ICoreWebView2* webview;
  char* url;
  int width;
  int height;
  char* title;
  char* icon;
  int show;
  int center;
  int ready;
  int frame;        /* 1 = show window frame (default), 0 = frameless */
  int transparent;  /* 1 = transparent background, 0 = opaque (default) */
  int devTools;     /* 1 = open devtools (default), 0 = hide devtools */
  /* Event listeners: map eventName → array of functions */
  TSHashMap* listeners;
  /* COM event tokens for cleanup */
  EventRegistrationToken token_nav_completed;
  EventRegistrationToken token_source_changed;
  EventRegistrationToken token_web_message;
  EventRegistrationToken token_title_changed;
  /* JavaScript interfaces: map name → JS shim script string */
  TSHashMap* interfaces;
  /* Interface method callbacks: map "name.method" → callback Function */
  TSHashMap* interfaceMethods;
} WebViewInstance;

static const wchar_t* CLASS_NAME = L"MiniTscWebView";

/* Forward declarations */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* Convert char* to wchar_t* (caller must free) */
static wchar_t* to_wide(const char* str) {
  if (!str) return NULL;
  int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
  wchar_t* wstr = (wchar_t*)malloc(len * sizeof(wchar_t));
  if (wstr) MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, len);
  return wstr;
}

/* Register window class */
static void register_class(void) {
  static int registered = 0;
  if (registered) return;
  registered = 1;

  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandle(NULL);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = CLASS_NAME;
  wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
  RegisterClassExW(&wc);
}

/* Convert wchar_t* to char* (caller must free) */
static char* from_wide(const wchar_t* wstr) {
  if (!wstr) return NULL;
  int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
  char* str = (char*)malloc(len);
  if (str) WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL);
  return str;
}

static void webview_emit(WebViewInstance* inst, const char* event, Value* args, int argc) {
  if (!inst || !inst->listeners) return;
  Value arr = ts_hashmap_get(inst->listeners, ts_string_new(event));
  if (arr.tag == TAG_ARRAY && arr.as.array) {
    TSArray* a = arr.as.array;
    for (int i = 0; i < a->length; i++) {
      Value fn = ts_array_get(a, i);
      ts_value_call(fn, args, argc);
    }
  }
  fflush(stdout);
  fflush(stderr);
}

/* Window procedure */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  WebViewInstance* inst = (WebViewInstance*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

  /* Debug output for first few messages */
  static int msgDbgCount = 0;
  if (msgDbgCount < 10) {
    fprintf(stderr, "WndProc: msg=%u hwnd=%p inst=%p\n", msg, hwnd, inst);
    msgDbgCount++;
  }

  switch (msg) {
    case WM_CREATE: {
      CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
      SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
      return 0;
    }
    case WM_ERASEBKGND:
      /* Suppress background erase when WebView2 is active to avoid flashing over content */
      if (inst && inst->controller) return 1;
      break;
    case WM_SIZE:
      if (inst && inst->controller) {
        RECT bounds;
        GetClientRect(hwnd, &bounds);
        ICoreWebView2Controller_put_Bounds(inst->controller, bounds);
        if (inst) {
          Value w = ts_value_number((double)(bounds.right - bounds.left));
          Value h = ts_value_number((double)(bounds.bottom - bounds.top));
          Value args[2] = { w, h };
          webview_emit(inst, "resize", args, 2);
        }
      }
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_CLOSE:
      if (inst) {
        webview_emit(inst, "close", NULL, 0);
      }
      if (inst && inst->controller) {
        ICoreWebView2Controller_Close(inst->controller);
      }
      DestroyWindow(hwnd);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* Navigate to URL */
static void navigate_to_url(WebViewInstance* inst) {
  if (!inst->webview || !inst->url) {
    fprintf(stderr, "WebView: navigate_to_url skipped - webview=%p, url=%s\n",
            inst->webview, inst->url ? inst->url : "(null)");
    return;
  }
  wchar_t* wurl = to_wide(inst->url);
  if (wurl) {
    HRESULT hr = ICoreWebView2_Navigate(inst->webview, wurl);
    fprintf(stderr, "WebView: ICoreWebView2_Navigate result: 0x%08lx\n", hr);
    free(wurl);
  }
}

/* ==================== COM Event Handlers ==================== */

/* --- NavigationCompleted → "load" --- */
typedef struct {
  ICoreWebView2NavigationCompletedEventHandler handler;
  WebViewInstance* inst;
} NavCompletedHandler;

static HRESULT STDMETHODCALLTYPE NavCompleted_QueryInterface(
    ICoreWebView2NavigationCompletedEventHandler* This, REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2NavigationCompletedEventHandler)) {
    *ppv = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
  }
  return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE NavCompleted_AddRef(ICoreWebView2NavigationCompletedEventHandler* This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE NavCompleted_Release(ICoreWebView2NavigationCompletedEventHandler* This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE NavCompleted_Invoke(
    ICoreWebView2NavigationCompletedEventHandler* This,
    ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) {
  (void)sender; (void)args;
  NavCompletedHandler* h = (NavCompletedHandler*)This;
  /* Re-inject interface scripts for the newly loaded document */
  if (h->inst && h->inst->interfaces && h->inst->webview) {
    for (size_t i = 0; i < h->inst->interfaces->capacity; i++) {
      if (h->inst->interfaces->entries[i].occupied) {
        Value val = h->inst->interfaces->entries[i].value;
        if (val.tag == TAG_STRING && val.as.string && val.as.string->data) {
          wchar_t* wcode = to_wide(val.as.string->data);
          if (wcode) {
            ICoreWebView2_ExecuteScript(h->inst->webview, wcode, NULL);
            free(wcode);
          }
        }
      }
    }
  }
  webview_emit(h->inst, "load", NULL, 0);
  return S_OK;
}
static ICoreWebView2NavigationCompletedEventHandlerVtbl navCompletedVtbl = {
  NavCompleted_QueryInterface, NavCompleted_AddRef, NavCompleted_Release, NavCompleted_Invoke
};

/* --- SourceChanged → "navigate" --- */
typedef struct {
  ICoreWebView2SourceChangedEventHandler handler;
  WebViewInstance* inst;
} SourceChangedHandler;

static HRESULT STDMETHODCALLTYPE SourceChanged_QueryInterface(
    ICoreWebView2SourceChangedEventHandler* This, REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2SourceChangedEventHandler)) {
    *ppv = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
  }
  return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE SourceChanged_AddRef(ICoreWebView2SourceChangedEventHandler* This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE SourceChanged_Release(ICoreWebView2SourceChangedEventHandler* This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE SourceChanged_Invoke(
    ICoreWebView2SourceChangedEventHandler* This,
    ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs* args) {
  (void)args;
  SourceChangedHandler* h = (SourceChangedHandler*)This;
  char* url = NULL;
  if (sender) {
    LPWSTR urlW = NULL;
    ICoreWebView2_get_Source(sender, &urlW);
    if (urlW) {
      url = from_wide(urlW);
      CoTaskMemFree(urlW);
    }
  }
  Value arg = ts_value_string(ts_string_new(url ? url : ""));
  Value argsArr[1] = { arg };
  webview_emit(h->inst, "navigate", argsArr, 1);
  free(url);
  return S_OK;
}
static ICoreWebView2SourceChangedEventHandlerVtbl sourceChangedVtbl = {
  SourceChanged_QueryInterface, SourceChanged_AddRef, SourceChanged_Release, SourceChanged_Invoke
};

/* --- WebMessageReceived → "message" --- */
typedef struct {
  ICoreWebView2WebMessageReceivedEventHandler handler;
  WebViewInstance* inst;
} WebMessageReceivedHandler;

static HRESULT STDMETHODCALLTYPE WebMsg_QueryInterface(
    ICoreWebView2WebMessageReceivedEventHandler* This, REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2WebMessageReceivedEventHandler)) {
    *ppv = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
  }
  return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE WebMsg_AddRef(ICoreWebView2WebMessageReceivedEventHandler* This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE WebMsg_Release(ICoreWebView2WebMessageReceivedEventHandler* This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE WebMsg_Invoke(
    ICoreWebView2WebMessageReceivedEventHandler* This,
    ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) {
  (void)sender;
  WebMessageReceivedHandler* h = (WebMessageReceivedHandler*)This;
  char* msg = NULL;
  if (args) {
    LPWSTR msgW = NULL;
    ICoreWebView2WebMessageReceivedEventArgs_TryGetWebMessageAsString(args, &msgW);
    if (msgW) {
      msg = from_wide(msgW);
      CoTaskMemFree(msgW);
    }
  }
  int interfaceDispatched = 0;
  if (msg && h->inst && h->inst->interfaceMethods) {
    TSString* msgStr = ts_string_new(msg);
    Value parsed = ts_json_parse(msgStr);
    ts_string_free(msgStr);
    if (parsed.tag == TAG_OBJECT && parsed.as.object) {
      TSHashMap* map = (TSHashMap*)parsed.as.object;
      Value ifVal = ts_hashmap_get(map, ts_string_new("__if"));
      Value mVal = ts_hashmap_get(map, ts_string_new("__m"));
      Value aVal = ts_hashmap_get(map, ts_string_new("__a"));
      if (ifVal.tag == TAG_STRING && ifVal.as.string && mVal.tag == TAG_STRING && mVal.as.string) {
        size_t keyLen = strlen(ifVal.as.string->data) + 1 + strlen(mVal.as.string->data) + 1;
        char* compositeKey = (char*)malloc(keyLen);
        if (compositeKey) {
          snprintf(compositeKey, keyLen, "%s.%s", ifVal.as.string->data, mVal.as.string->data);
          Value cb = ts_hashmap_get(h->inst->interfaceMethods, ts_string_new(compositeKey));
          free(compositeKey);
          if ((cb.tag == TAG_FUNCTION && cb.as.function) || (cb.tag == TAG_OBJECT && cb.as.object && *(int32_t*)cb.as.object == BOUND_FN_TAG)) {
            TSArray* argsArr = (aVal.tag == TAG_ARRAY && aVal.as.array) ? aVal.as.array : NULL;
            int argc = argsArr ? argsArr->length : 0;
            static Value callArgs[16];
            for (int i = 0; i < argc && i < 16; i++) {
              callArgs[i] = ts_array_get(argsArr, i);
            }
            ts_value_call(cb, callArgs, argc);
            interfaceDispatched = 1;
            fflush(stdout);
            fflush(stderr);
          }
        }
      }
    }
  }
  if (!interfaceDispatched) {
    Value arg = ts_value_string(ts_string_new(msg ? msg : ""));
    Value argsArr[1] = { arg };
    webview_emit(h->inst, "message", argsArr, 1);
  }
  free(msg);
  return S_OK;
}
static ICoreWebView2WebMessageReceivedEventHandlerVtbl webMsgVtbl = {
  WebMsg_QueryInterface, WebMsg_AddRef, WebMsg_Release, WebMsg_Invoke
};

/* --- DocumentTitleChanged → "title" --- */
typedef struct {
  ICoreWebView2DocumentTitleChangedEventHandler handler;
  WebViewInstance* inst;
} TitleChangedHandler;

static HRESULT STDMETHODCALLTYPE TitleChanged_QueryInterface(
    ICoreWebView2DocumentTitleChangedEventHandler* This, REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2DocumentTitleChangedEventHandler)) {
    *ppv = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
  }
  return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE TitleChanged_AddRef(ICoreWebView2DocumentTitleChangedEventHandler* This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE TitleChanged_Release(ICoreWebView2DocumentTitleChangedEventHandler* This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE TitleChanged_Invoke(
    ICoreWebView2DocumentTitleChangedEventHandler* This,
    ICoreWebView2* sender, IUnknown* args) {
  (void)args;
  TitleChangedHandler* h = (TitleChangedHandler*)This;
  char* title = NULL;
  if (sender) {
    LPWSTR titleW = NULL;
    ICoreWebView2_get_DocumentTitle(sender, &titleW);
    if (titleW) {
      title = from_wide(titleW);
      CoTaskMemFree(titleW);
    }
  }
  Value arg = ts_value_string(ts_string_new(title ? title : ""));
  Value argsArr[1] = { arg };
  webview_emit(h->inst, "title", argsArr, 1);
  free(title);
  return S_OK;
}
static ICoreWebView2DocumentTitleChangedEventHandlerVtbl titleChangedVtbl = {
  TitleChanged_QueryInterface, TitleChanged_AddRef, TitleChanged_Release, TitleChanged_Invoke
};

/* Controller completion handler vtable - forward declarations */
static HRESULT STDMETHODCALLTYPE ControllerCreatedHandler_QueryInterface(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This,
    REFIID riid, void** ppvObject);
static ULONG STDMETHODCALLTYPE ControllerCreatedHandler_AddRef(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This);
static ULONG STDMETHODCALLTYPE ControllerCreatedHandler_Release(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This);
static HRESULT STDMETHODCALLTYPE ControllerCreatedHandler_Invoke(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This,
    HRESULT errorCode, ICoreWebView2Controller* result);

static ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl controllerVtbl = {
  ControllerCreatedHandler_QueryInterface,
  ControllerCreatedHandler_AddRef,
  ControllerCreatedHandler_Release,
  ControllerCreatedHandler_Invoke
};

typedef struct {
  ICoreWebView2CreateCoreWebView2ControllerCompletedHandler handler;
  WebViewInstance* inst;
} ControllerCompletedHandler;

/* Controller completion handler implementation */
static HRESULT STDMETHODCALLTYPE ControllerCreatedHandler_QueryInterface(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This,
    REFIID riid, void** ppvObject) {
  if (!ppvObject) return E_POINTER;
  *ppvObject = NULL;
  if (IsEqualIID(riid, &IID_IUnknown) ||
      IsEqualIID(riid, &IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
    *ppvObject = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
  }
  return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ControllerCreatedHandler_AddRef(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This) {
  (void)This;
  return 1;
}

static ULONG STDMETHODCALLTYPE ControllerCreatedHandler_Release(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This) {
  (void)This;
  return 1;
}

static HRESULT STDMETHODCALLTYPE ControllerCreatedHandler_Invoke(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This,
    HRESULT errorCode, ICoreWebView2Controller* result) {
  ControllerCompletedHandler* handler = (ControllerCompletedHandler*)This;
  WebViewInstance* inst = handler->inst;

  if (FAILED(errorCode) || !result) {
    fprintf(stderr, "WebView2: Failed to create controller: 0x%08lx\n", errorCode);
    return E_FAIL;
  }

  fprintf(stderr, "WebView2: Controller created successfully\n");

  inst->controller = result;
  ICoreWebView2Controller_AddRef(result);

  /* Get webview interface */
  ICoreWebView2Controller_get_CoreWebView2(result, &inst->webview);
  inst->ready = 1;

  /* Set bounds */
  RECT bounds;
  GetClientRect(inst->hwnd, &bounds);
  fprintf(stderr, "WebView: Setting bounds: %ldx%ld\n", bounds.right - bounds.left, bounds.bottom - bounds.top);
  ICoreWebView2Controller_put_Bounds(result, bounds);

  /* Force repaint */
  InvalidateRect(inst->hwnd, NULL, TRUE);

  /* Navigate to URL */
  if (inst->url) {
    fprintf(stderr, "WebView: Navigating to URL: %s\n", inst->url);
  }
  navigate_to_url(inst);

  /* Force repaint after navigation */
  InvalidateRect(inst->hwnd, NULL, TRUE);
  UpdateWindow(inst->hwnd);

  /* Show window before opening devtools */
  if (inst->show) {
    fprintf(stderr, "WebView: Showing window...\n");
    ShowWindow(inst->hwnd, SW_SHOW);
    UpdateWindow(inst->hwnd);
    /* Ensure the controller is visible after the parent window is shown */
    ICoreWebView2Controller_put_IsVisible(result, TRUE);
    /* Force full redraw */
    RedrawWindow(inst->hwnd, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN);
    fprintf(stderr, "WebView: Window shown\n");
  }

  /* Open devtools if requested */
  if (inst->devTools && inst->webview) {
    ICoreWebView2Settings* settings = NULL;
    HRESULT hrSettings = ICoreWebView2_get_Settings(inst->webview, &settings);
    if (SUCCEEDED(hrSettings) && settings) {
      ICoreWebView2Settings_put_AreDevToolsEnabled(settings, TRUE);
      ICoreWebView2Settings_put_IsWebMessageEnabled(settings, TRUE);
      ICoreWebView2Settings_Release(settings);
      fprintf(stderr, "WebView: DevTools enabled\n");
    }
    /* Open devtools window */
    HRESULT hrDevTools = ICoreWebView2_OpenDevToolsWindow(inst->webview);
    if (SUCCEEDED(hrDevTools)) {
      fprintf(stderr, "WebView: DevTools window opened\n");
    } else {
      fprintf(stderr, "WebView: Failed to open DevTools: 0x%08lx\n", hrDevTools);
    }
  }

  /* Register COM event handlers */
  {
    NavCompletedHandler* nc = (NavCompletedHandler*)malloc(sizeof(NavCompletedHandler));
    if (nc) {
      nc->handler.lpVtbl = &navCompletedVtbl;
      nc->inst = inst;
      ICoreWebView2_add_NavigationCompleted(inst->webview, &nc->handler, &inst->token_nav_completed);
    }

    SourceChangedHandler* sc = (SourceChangedHandler*)malloc(sizeof(SourceChangedHandler));
    if (sc) {
      sc->handler.lpVtbl = &sourceChangedVtbl;
      sc->inst = inst;
      ICoreWebView2_add_SourceChanged(inst->webview, &sc->handler, &inst->token_source_changed);
    }

    WebMessageReceivedHandler* wm = (WebMessageReceivedHandler*)malloc(sizeof(WebMessageReceivedHandler));
    if (wm) {
      wm->handler.lpVtbl = &webMsgVtbl;
      wm->inst = inst;
      ICoreWebView2_add_WebMessageReceived(inst->webview, &wm->handler, &inst->token_web_message);
    }

    TitleChangedHandler* tc = (TitleChangedHandler*)malloc(sizeof(TitleChangedHandler));
    if (tc) {
      tc->handler.lpVtbl = &titleChangedVtbl;
      tc->inst = inst;
      ICoreWebView2_add_DocumentTitleChanged(inst->webview, &tc->handler, &inst->token_title_changed);
    }
  }

  /* Re-inject any existing interface scripts into current page */
  if (inst->interfaces && inst->webview) {
    for (size_t i = 0; i < inst->interfaces->capacity; i++) {
      if (inst->interfaces->entries[i].occupied) {
        Value val = inst->interfaces->entries[i].value;
        if (val.tag == TAG_STRING && val.as.string && val.as.string->data) {
          wchar_t* wcode = to_wide(val.as.string->data);
          if (wcode) {
            ICoreWebView2_ExecuteScript(inst->webview, wcode, NULL);
            free(wcode);
          }
        }
      }
    }
  }

  /* Emit ready event */
  webview_emit(inst, "ready", NULL, 0);

  return S_OK;
}

/* Environment completion handler vtable - forward declarations */
static HRESULT STDMETHODCALLTYPE EnvCompletedHandler_QueryInterface(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This,
    REFIID riid, void** ppvObject);
static ULONG STDMETHODCALLTYPE EnvCompletedHandler_AddRef(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This);
static ULONG STDMETHODCALLTYPE EnvCompletedHandler_Release(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This);
static HRESULT STDMETHODCALLTYPE EnvCompletedHandler_Invoke(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This,
    HRESULT errorCode, ICoreWebView2Environment* environment);

static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl envVtbl = {
  EnvCompletedHandler_QueryInterface,
  EnvCompletedHandler_AddRef,
  EnvCompletedHandler_Release,
  EnvCompletedHandler_Invoke
};

typedef struct {
  ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler handler;
  WebViewInstance* inst;
} EnvCompletedHandler;

/* Environment completion handler implementation */
static HRESULT STDMETHODCALLTYPE EnvCompletedHandler_QueryInterface(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This,
    REFIID riid, void** ppvObject) {
  if (!ppvObject) return E_POINTER;
  *ppvObject = NULL;
  if (IsEqualIID(riid, &IID_IUnknown) ||
      IsEqualIID(riid, &IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
    *ppvObject = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
  }
  return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE EnvCompletedHandler_AddRef(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This) {
  (void)This;
  return 1;
}

static ULONG STDMETHODCALLTYPE EnvCompletedHandler_Release(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This) {
  (void)This;
  return 1;
}

static HRESULT STDMETHODCALLTYPE EnvCompletedHandler_Invoke(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This,
    HRESULT errorCode, ICoreWebView2Environment* environment) {
  EnvCompletedHandler* handler = (EnvCompletedHandler*)This;
  WebViewInstance* inst = handler->inst;

  if (FAILED(errorCode) || !environment) {
    fprintf(stderr, "WebView2: Failed to create environment: 0x%08lx\n", errorCode);
    free(handler);
    return E_FAIL;
  }

  fprintf(stderr, "WebView2: Environment created successfully\n");

  /* Allocate controller handler on heap so it survives until callback */
  ControllerCompletedHandler* controllerHandler = (ControllerCompletedHandler*)malloc(sizeof(ControllerCompletedHandler));
  if (!controllerHandler) {
    fprintf(stderr, "WebView: Failed to allocate controller handler\n");
    free(handler);
    return E_FAIL;
  }
  controllerHandler->handler.lpVtbl = &controllerVtbl;
  controllerHandler->inst = inst;

  /* Create controller */
  ICoreWebView2Environment_CreateCoreWebView2Controller(
    environment, inst->hwnd,
    &controllerHandler->handler);

  /* Free env handler (no longer needed after queuing controller creation) */
  free(handler);

  return S_OK;
}

Value node_webview_isAvailable(void) {
#ifdef _WIN32
  LPWSTR version = NULL;
  HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(NULL, &version);
  if (version) CoTaskMemFree(version);
  return ts_value_boolean(SUCCEEDED(hr));
#else
  return ts_value_boolean(0);
#endif
}

Value node_webview_WebView(Value options) {
  printf("WebView: node_webview_WebView called\n");
  fflush(stdout);
  fprintf(stderr, "WebView: node_webview_WebView called (stderr)\n");
  WebViewInstance* inst = (WebViewInstance*)calloc(1, sizeof(WebViewInstance));
  if (!inst) {
    fprintf(stderr, "WebView: Failed to allocate instance\n");
    return ts_value_undefined();
  }

  /* Defaults */
  inst->width = 800;
  inst->height = 600;
  inst->show = 1;
  inst->center = 1;
  inst->frame = 1;        /* default: show frame */
  inst->transparent = 0;  /* default: opaque */
  inst->devTools = 1;     /* default: open devtools */

  /* Parse options */
  if (options.tag == TAG_OBJECT && options.as.object) {
    TSHashMap* map = (TSHashMap*)options.as.object;
    Value v;

    v = ts_hashmap_get(map, ts_string_new("url"));
    if (v.tag == TAG_STRING && v.as.string && v.as.string->data) {
      inst->url = strdup(v.as.string->data);
    }

    v = ts_hashmap_get(map, ts_string_new("width"));
    if (v.tag == TAG_NUMBER) inst->width = (int)v.as.number;

    v = ts_hashmap_get(map, ts_string_new("height"));
    if (v.tag == TAG_NUMBER) inst->height = (int)v.as.number;

    v = ts_hashmap_get(map, ts_string_new("title"));
    if (v.tag == TAG_STRING && v.as.string && v.as.string->data) {
      inst->title = strdup(v.as.string->data);
    }

    v = ts_hashmap_get(map, ts_string_new("icon"));
    if (v.tag == TAG_STRING && v.as.string && v.as.string->data) {
      inst->icon = strdup(v.as.string->data);
    }

    v = ts_hashmap_get(map, ts_string_new("show"));
    inst->show = ts_to_boolean(v);

    v = ts_hashmap_get(map, ts_string_new("center"));
    inst->center = ts_to_boolean(v);

    v = ts_hashmap_get(map, ts_string_new("frame"));
    inst->frame = ts_to_boolean(v);

    v = ts_hashmap_get(map, ts_string_new("transparent"));
    inst->transparent = ts_to_boolean(v);

    v = ts_hashmap_get(map, ts_string_new("devTools"));
    inst->devTools = ts_to_boolean(v);
  }

  /* Register window class */
  fprintf(stderr, "WebView: Registering window class...\n");
  register_class();

  /* Create window */
  fprintf(stderr, "WebView: Creating window %dx%d, frame=%d, transparent=%d\n",
          inst->width, inst->height, inst->frame, inst->transparent);
  int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
  if (inst->center) {
    x = (GetSystemMetrics(SM_CXSCREEN) - inst->width) / 2;
    y = (GetSystemMetrics(SM_CYSCREEN) - inst->height) / 2;
  }

  /* Set window style based on frame option */
  DWORD dwStyle = (inst->frame ? WS_OVERLAPPEDWINDOW : WS_POPUP) | WS_CLIPCHILDREN;
  DWORD dwExStyle = 0;

  /* Note: transparent option is reserved for future use */

  wchar_t* wtitle = to_wide(inst->title ? inst->title : "WebView");

  inst->hwnd = CreateWindowExW(
    dwExStyle, CLASS_NAME, wtitle ? wtitle : L"WebView",
    dwStyle,
    x, y, inst->width, inst->height,
    NULL, NULL, GetModuleHandle(NULL), inst);

  if (wtitle) free(wtitle);

  if (!inst->hwnd) {
    fprintf(stderr, "WebView: Failed to create window\n");
    free(inst->url);
    free(inst->title);
    free(inst->icon);
    free(inst);
    return ts_value_undefined();
  }

  fprintf(stderr, "WebView: Window created successfully, hwnd=%p\n", inst->hwnd);

  /* Set window icon if provided */
  if (inst->icon) {
    wchar_t* wicon = to_wide(inst->icon);
    if (wicon) {
      HICON hIcon = (HICON)LoadImageW(NULL, wicon, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
      if (!hIcon) {
        /* Try relative to executable directory */
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        /* Find last backslash and replace with icon filename */
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) {
          *(lastSlash + 1) = L'\0';
          wcscat_s(exePath, MAX_PATH, wicon);
          hIcon = (HICON)LoadImageW(NULL, exePath, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
          if (hIcon) {
            fprintf(stderr, "WebView: Loaded icon from exe dir: %ls\n", exePath);
          }
        }
      }
      if (hIcon) {
        SendMessage(inst->hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessage(inst->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        fprintf(stderr, "WebView: Set window icon: %s\n", inst->icon);
      } else {
        fprintf(stderr, "WebView: Failed to load icon: %s\n", inst->icon);
      }
      free(wicon);
    }
  }

  /* Note: transparent option is reserved for future use with WebView2 background transparency */

  /* Initialize WebView2 */
  fprintf(stderr, "WebView: Initializing WebView2...\n");

  /* Initialize COM as Single-Threaded Apartment for UI */
  HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    fprintf(stderr, "WebView2: Failed to initialize COM: 0x%08lx\n", hr);
    DestroyWindow(inst->hwnd);
    free(inst->url);
    free(inst->title);
    free(inst->icon);
    free(inst);
    return ts_value_undefined();
  }

  /* Allocate environment handler on heap so it survives until callback */
  EnvCompletedHandler* envHandler = (EnvCompletedHandler*)malloc(sizeof(EnvCompletedHandler));
  if (!envHandler) {
    fprintf(stderr, "WebView: Failed to allocate env handler\n");
    DestroyWindow(inst->hwnd);
    free(inst->url);
    free(inst->title);
    free(inst->icon);
    free(inst);
    return ts_value_undefined();
  }
  envHandler->handler.lpVtbl = &envVtbl;
  envHandler->inst = inst;

  hr = CreateCoreWebView2EnvironmentWithOptions(
    NULL, NULL, NULL,
    &envHandler->handler);

  if (FAILED(hr)) {
    fprintf(stderr, "WebView2: Failed to create environment: 0x%08lx\n", hr);
    DestroyWindow(inst->hwnd);
    free(inst->url);
    free(inst->title);
    free(inst->icon);
    free(inst);
    return ts_value_undefined();
  }

  fprintf(stderr, "WebView2: Environment creation started\n");
  return ts_value_object((void*)inst);
}

Value node_webview_loadURL(Value self, Value url) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst) return ts_value_undefined();
  free(inst->url);
  inst->url = NULL;
  if (url.tag == TAG_STRING && url.as.string && url.as.string->data) {
    inst->url = strdup(url.as.string->data);
  }
  navigate_to_url(inst);
  return ts_value_undefined();
}

Value node_webview_navigate(Value self, Value url) {
  return node_webview_loadURL(self, url);
}

Value node_webview_loadHTML(Value self, Value html) {
  (void)self; (void)html;
  /* TODO: Implement NavigateToString */
  return ts_value_undefined();
}

Value node_webview_evaluate(Value self, Value script) {
  (void)self; (void)script;
  /* TODO: Implement ExecuteScript */
  return ts_value_undefined();
}

Value node_webview_executeJavaScript(Value self, Value script) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->webview) return ts_value_undefined();
  if (script.tag != TAG_STRING || !script.as.string || !script.as.string->data) {
    return ts_value_undefined();
  }
  wchar_t* wscript = to_wide(script.as.string->data);
  if (wscript) {
    ICoreWebView2_ExecuteScript(inst->webview, wscript, NULL);
    free(wscript);
  }
  return ts_value_undefined();
}

Value node_webview_setTitle(Value self, Value title) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  free(inst->title);
  inst->title = NULL;
  if (title.tag == TAG_STRING && title.as.string && title.as.string->data) {
    inst->title = strdup(title.as.string->data);
    wchar_t* wtitle = to_wide(inst->title);
    if (wtitle) {
      SetWindowTextW(inst->hwnd, wtitle);
      free(wtitle);
    }
  }
  return ts_value_undefined();
}

Value node_webview_setSize(Value self, Value width, Value height) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  int w = (width.tag == TAG_NUMBER) ? (int)width.as.number : inst->width;
  int h = (height.tag == TAG_NUMBER) ? (int)height.as.number : inst->height;
  inst->width = w;
  inst->height = h;
  SetWindowPos(inst->hwnd, NULL, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
  return ts_value_undefined();
}

Value node_webview_setIcon(Value self, Value iconPath) {
  (void)self; (void)iconPath;
  /* TODO: Implement icon setting */
  return ts_value_undefined();
}

Value node_webview_setPosition(Value self, Value x, Value y) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  int px = (x.tag == TAG_NUMBER) ? (int)x.as.number : 0;
  int py = (y.tag == TAG_NUMBER) ? (int)y.as.number : 0;
  SetWindowPos(inst->hwnd, NULL, px, py, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
  return ts_value_undefined();
}

Value node_webview_center(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  int x = (GetSystemMetrics(SM_CXSCREEN) - inst->width) / 2;
  int y = (GetSystemMetrics(SM_CYSCREEN) - inst->height) / 2;
  SetWindowPos(inst->hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
  return ts_value_undefined();
}

Value node_webview_show(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  ShowWindow(inst->hwnd, SW_SHOW);
  UpdateWindow(inst->hwnd);
  return ts_value_undefined();
}

Value node_webview_hide(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  ShowWindow(inst->hwnd, SW_HIDE);
  return ts_value_undefined();
}

Value node_webview_focus(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  SetForegroundWindow(inst->hwnd);
  SetFocus(inst->hwnd);
  return ts_value_undefined();
}

Value node_webview_minimize(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  ShowWindow(inst->hwnd, SW_MINIMIZE);
  return ts_value_undefined();
}

Value node_webview_maximize(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  ShowWindow(inst->hwnd, SW_MAXIMIZE);
  return ts_value_undefined();
}

Value node_webview_unmaximize(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  ShowWindow(inst->hwnd, SW_RESTORE);
  return ts_value_undefined();
}

Value node_webview_close(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();

  if (inst->webview) {
    ICoreWebView2_remove_NavigationCompleted(inst->webview, inst->token_nav_completed);
    ICoreWebView2_remove_SourceChanged(inst->webview, inst->token_source_changed);
    ICoreWebView2_remove_WebMessageReceived(inst->webview, inst->token_web_message);
    ICoreWebView2_remove_DocumentTitleChanged(inst->webview, inst->token_title_changed);
  }

  if (inst->controller) {
    ICoreWebView2Controller_Close(inst->controller);
  }
  DestroyWindow(inst->hwnd);

  /* Cleanup C strings (but leave inst itself — runtime/GC owns the object pointer) */
  free(inst->url);
  free(inst->title);
  free(inst->icon);
  return ts_value_undefined();
}

Value node_webview_run(Value self) {
  fprintf(stderr, "WebView: node_webview_run called, self.tag=%d, self.as.object=%p\n", self.tag, self.as.object);
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) {
    fprintf(stderr, "WebView: node_webview_run returning early, inst=%p, hwnd=%p\n", inst, inst ? inst->hwnd : NULL);
    return ts_value_undefined();
  }

  fprintf(stderr, "WebView: Starting message loop, hwnd=%p\n", inst->hwnd);

  MSG msg;
  int msgCount = 0;
  while (GetMessage(&msg, NULL, 0, 0)) {
    msgCount++;
    if (msgCount <= 5) {
      fprintf(stderr, "WebView: Message %d: msg=%u hwnd=%p\n", msgCount, msg.message, msg.hwnd);
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  fprintf(stderr, "WebView: Message loop ended, total messages: %d\n", msgCount);
  return ts_value_undefined();
}

Value node_webview_on(Value self, Value event, Value callback) {
  if (self.tag != TAG_OBJECT || !self.as.object) return self;
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst) return self;

  if (!inst->listeners) {
    inst->listeners = ts_hashmap_new();
  }

  TSString* evName = ts_to_string(event);
  if (!evName) return self;

  Value arr = ts_hashmap_get(inst->listeners, evName);
  if (arr.tag != TAG_ARRAY || !arr.as.array) {
    arr = ts_value_array(ts_array_new());
    ts_hashmap_set(inst->listeners, evName, arr);
  }
  ts_array_push(arr.as.array, callback);
  return self;
}

Value node_webview_once(Value self, Value event, Value callback) {
  /* For simplicity, same as on (full implementation would track once flag) */
  return node_webview_on(self, event, callback);
}

Value node_webview_off(Value self, Value event, Value callback) {
  if (self.tag != TAG_OBJECT || !self.as.object) return self;
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->listeners) return self;

  TSString* evName = ts_to_string(event);
  if (!evName) return self;

  Value arr = ts_hashmap_get(inst->listeners, evName);
  if (arr.tag != TAG_ARRAY || !arr.as.array) return self;

  TSArray* old = arr.as.array;
  TSArray* neu = ts_array_new();
  int removed = 0;
  for (int i = 0; i < old->length; i++) {
    Value fn = ts_array_get(old, i);
    if (!removed && fn.tag == callback.tag && fn.as.function == callback.as.function) {
      removed = 1;
      continue;
    }
    ts_array_push(neu, fn);
  }
  ts_hashmap_set(inst->listeners, evName, ts_value_array(neu));
  return self;
}

Value node_webview_get_ready(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst) return ts_value_boolean(0);
  return ts_value_boolean(inst->ready);
}

Value node_webview_get_url(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->url) return ts_value_string(ts_string_new(""));
  return ts_value_string(ts_string_new(inst->url));
}

/* helper: dynamic string builder */
typedef struct {
  char* buf;
  size_t cap;
  size_t len;
} StrBuilder;

static void sb_init(StrBuilder* sb) {
  sb->cap = 256;
  sb->buf = (char*)malloc(sb->cap);
  sb->len = 0;
  if (sb->buf) sb->buf[0] = '\0';
}

static void sb_append(StrBuilder* sb, const char* text) {
  if (!sb->buf) return;
  size_t tlen = strlen(text);
  while (sb->len + tlen + 1 > sb->cap) {
    sb->cap = sb->cap * 2 + tlen + 1;
    sb->buf = (char*)realloc(sb->buf, sb->cap);
  }
  memcpy(sb->buf + sb->len, text, tlen);
  sb->len += tlen;
  sb->buf[sb->len] = '\0';
}

static char* sb_take(StrBuilder* sb) {
  char* result = sb->buf;
  sb->buf = NULL;
  sb->cap = 0;
  sb->len = 0;
  return result;
}

/* Context passed to build_script_cb */
typedef struct {
  const char* name;
  StrBuilder sb;
} BuildScriptCtx;

static void build_script_cb(TSString* key, Value value, void* ctx) {
  (void)value;
  BuildScriptCtx* b = (BuildScriptCtx*)ctx;
  sb_append(&b->sb, "window.");
  sb_append(&b->sb, b->name);
  sb_append(&b->sb, " = window.");
  sb_append(&b->sb, b->name);
  sb_append(&b->sb, " || {};\nwindow.");
  sb_append(&b->sb, b->name);
  sb_append(&b->sb, "[\"");
  sb_append(&b->sb, key->data);
  sb_append(&b->sb, "\"] = function(...args) {\n"
                    "  if (typeof window.chrome !== 'undefined' && window.chrome.webview) {\n"
                    "    window.chrome.webview.postMessage(JSON.stringify({__if:\"");
  sb_append(&b->sb, b->name);
  sb_append(&b->sb, "\",__m:\"");
  sb_append(&b->sb, key->data);
  sb_append(&b->sb, "\",__a:args}));\n"
                    "  }\n"
                    "};\n");
}

static char* build_interface_script(const char* name, TSHashMap* methods) {
  BuildScriptCtx ctx = { .name = name };
  sb_init(&ctx.sb);
  if (!ctx.sb.buf) return NULL;
  ts_hashmap_for_each(methods, build_script_cb, &ctx);
  return sb_take(&ctx.sb);
}

Value node_webview_addJavaScriptInterface(Value self, Value name, Value methods) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst) return ts_value_undefined();
  if (name.tag != TAG_STRING || !name.as.string || !name.as.string->data) return ts_value_undefined();
  if (methods.tag != TAG_OBJECT || !methods.as.object) return ts_value_undefined();

  TSHashMap* methodsMap = (TSHashMap*)methods.as.object;
  const char* ifName = name.as.string->data;

  /* Store callbacks */
  if (!inst->interfaceMethods) {
    inst->interfaceMethods = ts_hashmap_new();
  }
  for (int32_t i = 0; i < methodsMap->capacity; i++) {
    if (!methodsMap->entries[i].occupied) continue;
    TSString* key = methodsMap->entries[i].key;
    Value fn = methodsMap->entries[i].value;
    if (fn.tag != TAG_FUNCTION && !(fn.tag == TAG_OBJECT && fn.as.object && *(int32_t*)fn.as.object == BOUND_FN_TAG)) continue;
    size_t keyLen = strlen(ifName) + 1 + strlen(key->data) + 1;
    char* compositeKey = (char*)malloc(keyLen);
    snprintf(compositeKey, keyLen, "%s.%s", ifName, key->data);
    ts_hashmap_set(inst->interfaceMethods, ts_string_new(compositeKey), fn);
    free(compositeKey);
  }

  /* Build JS shim and store for re-injection on every navigation */
  char* jsCode = build_interface_script(ifName, methodsMap);
  if (!jsCode) return ts_value_undefined();

  if (!inst->interfaces) {
    inst->interfaces = ts_hashmap_new();
  }
  TSString* keyStr = ts_string_new(ifName);
  /* Remove old script string if present */
  Value oldVal = ts_hashmap_get(inst->interfaces, keyStr);
  if (oldVal.tag == TAG_STRING && oldVal.as.string) {
    ts_string_free(oldVal.as.string);
  }
  Value scriptVal;
  scriptVal.tag = TAG_STRING;
  scriptVal.as.string = ts_string_new(jsCode);
  ts_hashmap_set(inst->interfaces, keyStr, scriptVal);
  ts_string_free(keyStr);

  /* If webview already ready, inject immediately into current page */
  if (inst->webview) {
    wchar_t* wcode = to_wide(jsCode);
    if (wcode) {
      ICoreWebView2_ExecuteScript(inst->webview, wcode, NULL);
      free(wcode);
    }
  }

  free(jsCode);
  return ts_value_undefined();
}

Value node_webview_removeJavaScriptInterface(Value self, Value name) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst) return ts_value_undefined();
  if (name.tag != TAG_STRING || !name.as.string || !name.as.string->data) return ts_value_undefined();

  /* Remove stored script string */
  if (inst->interfaces) {
    Value idVal = ts_hashmap_get(inst->interfaces, name.as.string);
    if (idVal.tag == TAG_STRING && idVal.as.string) {
      ts_string_free(idVal.as.string);
    }
    ts_hashmap_set(inst->interfaces, name.as.string, ts_value_undefined());
  }

  /* Remove callbacks */
  if (inst->interfaceMethods) {
    const char* prefix = name.as.string->data;
    size_t prefixLen = strlen(prefix);
    TSHashMap* newMap = ts_hashmap_new();
    for (int32_t i = 0; i < inst->interfaceMethods->capacity; i++) {
      if (!inst->interfaceMethods->entries[i].occupied) continue;
      TSString* key = inst->interfaceMethods->entries[i].key;
      if (strlen(key->data) <= prefixLen || strncmp(key->data, prefix, prefixLen) != 0 || key->data[prefixLen] != '.') {
        ts_hashmap_set(newMap, key, inst->interfaceMethods->entries[i].value);
      }
    }
    /* We intentionally leak the old hashmap and just swap pointer; GC will handle orphans */
    inst->interfaceMethods = newMap;
  }

  return ts_value_undefined();
}
