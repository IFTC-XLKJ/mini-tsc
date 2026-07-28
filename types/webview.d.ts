/** mini-tsc ambient types for the `webview` module (Edge WebView2 / WebKit). */
declare module "webview" {
  /**
   * Window / content options for `new WebView(options)`.
   * CSS `-webkit-app-region: drag | no-drag` is supported (Electron-compatible)
   * for custom title-bar drag regions on frameless windows.
   */
  interface WebViewOptions {
    /** Window width in CSS pixels (default 800). */
    width?: number;
    /** Window height in CSS pixels (default 600). */
    height?: number;
    /** Window title (default "WebView"). */
    title?: string;
    /** Path to window icon (.ico on Windows, image file on Linux). */
    icon?: string;
    /** Show native frame / title bar (default true). */
    frame?: boolean;
    /** Alias of `frame: false` — borderless window. */
    frameless?: boolean;
    /** Allow user resize (default true). */
    resizable?: boolean;
    /** Transparent window background (default false). */
    transparent?: boolean;
    /** Initial page URL. */
    url?: string;
    /** Initial HTML document (used when `url` is omitted). */
    html?: string;
    /** Window X position (screen coords). */
    x?: number;
    /** Window Y position (screen coords). */
    y?: number;
    /** Center on screen (default true when x/y omitted). */
    center?: boolean;
    /** Background color as CSS color string, e.g. "#ffffff" or "transparent". */
    backgroundColor?: string;
    /** Start hidden; call `show()` later (default false). */
    show?: boolean;
    /** DevTools enabled (default false). */
    devTools?: boolean;
  }

  interface WebViewEventMap {
    ready: [];
    load: [string];
    navigate: [string];
    close: [];
    message: [any];
    title: [string];
    resize: [number, number];
  }

  /**
   * Native WebView window.
   * Windows: Microsoft Edge WebView2. Linux: WebKitGTK.
   * Construction fails (throws / error dialog) when the platform WebView runtime is missing.
   */
  class WebView {
    constructor(options?: WebViewOptions);

    /** Navigate to a URL (http(s)://, file://, data:, about:blank). */
    loadURL(url: string): void;
    /** Alias of loadURL. */
    navigate(url: string): void;
    /** Load an HTML string as document. */
    loadHTML(html: string): void;
    /** Run JavaScript in the page (fire-and-forget). */
    evaluate(script: string): void;
    /** Execute script and ignore result (alias). */
    executeJavaScript(script: string): void;

    setTitle(title: string): void;
    setSize(width: number, height: number): void;
    setIcon(iconPath: string): void;
    setPosition(x: number, y: number): void;
    center(): void;

    show(): void;
    hide(): void;
    focus(): void;
    minimize(): void;
    maximize(): void;
    unmaximize(): void;
    close(): void;

    /** Whether the native WebView controller is ready. */
    readonly ready: boolean;
    /** Current document URL (best-effort). */
    readonly url: string;

    /**
     * Subscribe to events: "ready" | "load" | "navigate" | "close" | "message" | "title" | "resize".
     * Page → host messages use `window.chrome.webview.postMessage(...)` (Win) /
     * `window.webkit.messageHandlers.miniTsc.postMessage(...)` (Linux).
     */
    on(event: string, callback: (...args: any[]) => void): this;
    once(event: string, callback: (...args: any[]) => void): this;
    off(event: string, callback: (...args: any[]) => void): this;

    /**
     * Run the native UI message loop until the window closes (blocking).
     * Call after configuring loadURL / on handlers.
     */
    run(): void;
  }

  /** True when Edge WebView2 (Windows) or WebKitGTK (Linux) is available. */
  function isAvailable(): boolean;

  export { WebView, isAvailable, WebViewOptions, WebViewEventMap };
}
