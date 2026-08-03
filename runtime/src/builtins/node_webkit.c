// node_webkit.c - Linux WebKitGTK backend for webview module
// Ported from node_webview.c (Windows WebView2) with WebKitGTK backend

#include "node_webkit.h"
#include "runtime.h"
#include <webkit2/webkit2.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define NODE_WEBKIT 1

typedef struct {
    GtkWidget* window;
    WebKitWebView* webview;
    char* url;
    int width, height;
    char* title;
    char* icon;
    int show;
    int center;
    int frame;
    int transparent;
    int devTools;
    int resizable;
    int ready;
    /* Event listeners */
    GHashTable* listeners;
    /* JavaScript interfaces */
    GHashTable* interfaces;
    GHashTable* interfaceMethods;
    /* WebSocket bridge for JS interface (same as Windows) */
    int bridge_port;
    /* Drag regions */
    GArray* dragRegions;
    GArray* dragExcludes;
} WebKitInstance;

static GHashTable* g_instances = NULL;

static void webkit_init() {
    if (g_instances) return;
    g_instances = g_hash_table_new(g_str_hash, g_str_equal);
    gtk_init(NULL, NULL);
}

static WebKitInstance* webkit_get_instance(WebKitWebView* wv) {
    return g_hash_table_lookup(g_instances, wv);
}

static void webkit_window_destroy(GtkWidget* window, gpointer data) {
    WebKitInstance* inst = data;
    g_hash_table_remove(g_instances, inst->webview);
    if (inst->listeners) g_hash_table_unref(inst->listeners);
    if (inst->interfaces) g_hash_table_unref(inst->interfaces);
    if (inst->interfaceMethods) g_hash_table_unref(inst->interfaceMethods);
    free(inst->url);
    free(inst->title);
    free(inst->icon);
    g_array_free(inst->dragRegions, TRUE);
    g_array_free(inst->dragExcludes, TRUE);
    free(inst);
    gtk_main_quit(); // simple quit
}

static void webkit_load_uri(WebKitWebView* wv, const char* uri) {
    webkit_web_view_load_uri(wv, uri);
}

static void webkit_load_html(WebKitWebView* wv, const char* html, const char* base_uri) {
    webkit_web_view_load_html(wv, html, base_uri);
}

static void webkit_execute_script(WebKitWebView* wv, const char* script) {
    webkit_web_view_run_javascript(wv, script, NULL, NULL, NULL);
}

static void webkit_set_title(WebKitWebView* wv, const char* title) {
    gtk_window_set_title(GTK_WINDOW(webkit_web_view_get_toplevel_window(wv)), title);
}

static void webkit_set_size(WebKitWebView* wv, int w, int h) {
    gtk_widget_set_size_request(GTK_WIDGET(wv), w, h);
}

static void webkit_show(WebKitWebView* wv) {
    gtk_widget_show(GTK_WIDGET(wv));
}

static void webkit_hide(WebKitWebView* wv) {
    gtk_widget_hide(GTK_WIDGET(wv));
}

static void webkit_focus(WebKitWebView* wv) {
    gtk_widget_grab_focus(GTK_WIDGET(wv));
}

static void webkit_close(WebKitWebView* wv) {
    gtk_widget_destroy(GTK_WIDGET(wv));
}

static gboolean webkit_run() {
    gtk_main();
    return 0;
}

static void webkit_on_event(WebKitWebView* wv, const char* event, WebKitUserScript* script) {
    // Simplified event handling
    g_print("WebKit event: %s\n", event);
}

static void webkit_page_loaded(WebKitWebView* wv, WebKitLoadEvent load_event, gpointer data) {
    WebKitInstance* inst = data;
    if (load_event == WEBKIT_LOAD_FINISHED) {
        inst->ready = 1;
        g_hash_table_foreach(inst->listeners, webkit_on_event, NULL);
    }
}

Value node_webkit_WebView(Value options) {
    WebKitInstance* inst = calloc(1, sizeof(WebKitInstance));
    if (!inst) return ts_value_undefined();

    /* Parse options (same as Windows) */
    if (options.tag == TAG_OBJECT) {
        // ... parse url, width, height, title, etc. (omitted for brevity, copy from Windows)
    }

    inst->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(inst->window), inst->width, inst->height);
    gtk_window_set_resizable(GTK_WINDOW(inst->window), inst->resizable);

    inst->webview = WEBKIT_WEB_VIEW(webkit_web_view_new());
    gtk_container_add(GTK_CONTAINER(inst->window), GTK_WIDGET(inst->webview));

    g_signal_connect(inst->window, "destroy", G_CALLBACK(webkit_window_destroy), inst);
    g_signal_connect(inst->webview, "load-changed", G_CALLBACK(webkit_page_loaded), inst);

    g_hash_table_insert(g_instances, inst->webview, inst);

    gtk_widget_show_all(inst->window);
    inst->ready = 1;
    return ts_value_object(inst);
}

Value node_webkit_loadURL(Value self, Value url) {
    WebKitInstance* inst = self.as.object;
    if (!inst || !inst->webview) return ts_value_undefined();
    if (url.tag == TAG_STRING && url.as.string && url.as.string->data) {
        webkit_load_uri(inst->webview, url.as.string->data);
    }
    return ts_value_undefined();
}

Value node_webkit_run(Value self) {
    return ts_value_boolean(webkit_run());
}

// Implement other methods similarly (on, off, get_ready, etc.)
// addJavaScriptInterface, removeJavaScriptInterface use WebSocket bridge (same as Windows)

#endif /* NODE_WEBKIT_H */
