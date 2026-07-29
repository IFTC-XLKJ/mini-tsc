// my_app - mini-tsc project
import { WebView } from "webview";

function main(): void {
    console.log("Hello from my_app!");
    const webView = new WebView({
        url: "https://iftc.koyeb.app/mini-tsc",
        width: 800,
        height: 600,
        title: "My App",
        icon: "icon.ico",
        show: true,
        center: true,
        frame: false,
        transparent: true,
        devTools: true,
    });
    webView.run();
    webView.on("ready", () => {
        console.log("WebView ready");
        webView.executeJavaScript("console.log('Hello from my_app!');");
    });
    webView.on("message", (message) => {
        console.log("WebView message:", message);
    });
    webView.on("load", () => {
        console.log("WebView loaded");
    });
    webView.on("navigate", () => {
        console.log("WebView navigate");
    });
    webView.on("resize", () => {
        console.log("WebView resized");
    });
    webView.on("title", (title) => {
        console.log("WebView title:", title);
    });
    webView.on("error", (error) => {
        console.error("WebView error:", error);
    });
    webView.on("close", () => {
        console.log("WebView closed");
    });
    const webView2 = new WebView({
        url: "https://iftc.koyeb.app/mini-tsc",
        width: 800,
        height: 600,
        title: "My App",
        icon: "icon.ico",
        show: true,
        center: true,
        frame: false,
        transparent: true,
        devTools: true,
    });
    webView2.run();
    webView2.executeJavaScript("console.log('Hello from my_app!');");
}

main();
