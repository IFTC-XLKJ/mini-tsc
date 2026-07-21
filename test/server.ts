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
                    "Content-Disposition": "attachment; filename=mini-tsc.zip",
                },
            });
        }
        if (url.pathname === "/test-stream") {
            const writableStream = new WritableStream();
            const { getWriter } = writableStream;
            const writer = getWriter();
            if (!writer) {
                return new Response("No body writer available", { status: 500 });
            }
            for (let i = 1; i <= 5; i++) {
                setTimeout(
                    () => {
                        writer.write("id: " + i + "\nevent: tick\ndata: chunk " + i + "\n\n");
                        if (i === 5) {
                            writer.close();
                        }
                    },
                    i * randomInt(1000, 5000),
                );
            }
            return new Response(writer, {
                headers: {
                    "Content-Type": "text/event-stream",
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
function randomInt(min: number, max: number): number {
    return Math.floor(Math.random() * (max - min + 1)) + min;
}
