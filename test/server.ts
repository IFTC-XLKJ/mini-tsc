import * as http from "http";
import * as fs from "fs/promises";
import * as path from "path";

function main(): void {
    console.log(__dirname, __filename);
    console.log("Starting server...");
    const server = http.createServer(async (req: Request) => {
        console.log(req.url);
        const host = req.headers.get("host") || "localhost";
        const url = new URL(req.url || "/", "http://" + host);
        console.log(url.pathname);
        if (url.pathname === "/test-file") {
            const file = await fs.readFile(path.join(__dirname, "../mini-tsc.zip"));
            console.log(file, file.length);
            return new Response(file, {
                headers: {
                    "Content-Type": "application/zip",
                    "Content-Length": file.length.toString(),
                    "Content-Disposition": "attachment; filename=mini-tsc.zip",
                },
            });
        }
        return new Response("Hello, World!");
    });
    server.listen(3000, () => {
        console.log("Server is running on port 3000");
    });
}
main();
