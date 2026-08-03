#ifndef NODE_WEBKIT_H
#define NODE_WEBKIT_H

/* Linux WebKitGTK backend for the `webview` module.
 * Exports the same node_webview_* C API as Windows (node_webview.h). */

#include "runtime.h"

/* Module helpers */
Value node_webview_isAvailable(void);

/* Constructor: new WebView(options?) */
Value node_webview_WebView(Value options);

/* Instance methods (first arg = self) */
Value node_webview_loadURL(Value self, Value url);
Value node_webview_navigate(Value self, Value url);
Value node_webview_loadHTML(Value self, Value html);
Value node_webview_evaluate(Value self, Value script);
Value node_webview_executeJavaScript(Value self, Value script);
Value node_webview_setTitle(Value self, Value title);
Value node_webview_setSize(Value self, Value width, Value height);
Value node_webview_setIcon(Value self, Value iconPath);
Value node_webview_setPosition(Value self, Value x, Value y);
Value node_webview_center(Value self);
Value node_webview_show(Value self);
Value node_webview_hide(Value self);
Value node_webview_focus(Value self);
Value node_webview_minimize(Value self);
Value node_webview_maximize(Value self);
Value node_webview_unmaximize(Value self);
Value node_webview_close(Value self);
Value node_webview_restart(Value self);
Value node_webview_run(Value self);
Value node_webview_on(Value self, Value event, Value callback);
Value node_webview_once(Value self, Value event, Value callback);
Value node_webview_off(Value self, Value event, Value callback);
Value node_webview_get_ready(Value self);
Value node_webview_get_url(Value self);

/* JavaScript interface injection */
Value node_webview_addJavaScriptInterface(Value self, Value name, Value code);
Value node_webview_removeJavaScriptInterface(Value self, Value name);

#endif /* NODE_WEBKIT_H */
