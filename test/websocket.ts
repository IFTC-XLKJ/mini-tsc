function main() {
    console.log("start");
    const ws = new WebSocket("ws://localhost:3000/test-websocket");
    ws.onopen = () => {
        console.log("Connected");
        ws.send("Hello, World");
    };
    ws.onmessage = (event) => {
        console.log("Received message:", event.data);
        ws.close();
    };
    ws.onerror = (error) => {
        console.error("Error:", error);
    };
    ws.onclose = () => {
        console.log("Closed");
    };
}
main();
