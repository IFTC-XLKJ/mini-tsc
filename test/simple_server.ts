import * as http from "http";

function main(): void {
    console.log("Starting server...");
    const server = http.createServer((req: Request) => {
        console.log("Got request:", req.url);
        return new Response("Hello, World!");
    });
    server.listen(3001, () => {
        console.log("Server is running on port 3001");
    });
}
main();
