#include "node_webview.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <commctrl.h>

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
      }
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_CLOSE:
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
  if (inst->controller) {
    ICoreWebView2Controller_Close(inst->controller);
  }
  DestroyWindow(inst->hwnd);
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
  (void)self; (void)event; (void)callback;
  /* TODO: Implement event handling */
  return ts_value_undefined();
}

Value node_webview_once(Value self, Value event, Value callback) {
  return node_webview_on(self, event, callback);
}

Value node_webview_off(Value self, Value event, Value callback) {
  (void)self; (void)event; (void)callback;
  /* TODO: Implement event removal */
  return ts_value_undefined();
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
