#ifndef NODE_WEBVIEW_H
#define NODE_WEBVIEW_H

#include "runtime.h"

/* Module helpers */
Value node_webview_isAvailable(void);

/* Constructor: new WebView(options?) */
Value node_webview_WebView(Value options);

/* Instance methods (first arg = self) */
Value node_webview_loadURL(Value self, Value url);
Value node_webview_executeJavaScript(Value self, Value script);
Value node_webview_show(Value self);
Value node_webview_close(Value self);
Value node_webview_run(Value self);
Value node_webview_on(Value self, Value event, Value callback);

#endif /* NODE_WEBVIEW_H */
