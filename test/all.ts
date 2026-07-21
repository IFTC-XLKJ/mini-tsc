import * as process from "process";
import * as os from "os";

async function main(): Promise<void> {
    console.log(
        String,
        Number,
        Boolean,
        Object,
        Array,
        Function,
        RegExp,
        Date,
        Error,
        JSON,
        Request,
        Response,
        Headers,
        Blob,
        Promise,
        URL,
        ArrayBuffer,
        Uint8Array,
        ReadableStream,
        WritableStream,
        TransformStream,
        Crypto,
        CryptoKey,
        SubtleCrypto,
        TextEncoder,
        TextDecoder,
        AbortController,
        AbortSignal,
        EventTarget,
        Event,
    );
    console.log(process.argv);
    console.log(process.pid);
    console.log(process.cwd());
    console.log(process.env);
    console.log("Hello, World!");
    console.log(0.1 + 0.2);
    console.log(0.1 - 0.2);
    console.log(0.1 * 0.2);
    console.log(0.1 / 0.2);
    console.log(0.1 % 0.2);
    console.log(0.1 ** 0.2);
    console.log(0.1 < 0.2);
    console.log(0.1 > 0.2);
    console.log(0.1 <= 0.2);
    console.log(0.1 >= 0.2);
    const n1: number = 0.1;
    const n2: number = 0.2;
    console.log(n1 == n2);
    console.log(n1 !== n2);
    console.log(typeof 0.1);
    for (let i = 0; i < 10; i++) {
        console.log(i);
    }
    console.log(Date.now());
    console.log(Date.parse("2026-07-14"));
    console.log(JSON.stringify({ a: 1, b: 2, c: [1, 2, { a: 1, b: 2 }] }, null, 2));
    console.log(JSON.parse(JSON.stringify({ a: 1, b: 2, c: [1, 2, { a: 1, b: 2 }] }, null, 2)));
    console.time("test");
    // for (let i = 0; i < 2 ** 31; i++) {
    // console.log(i);
    // }
    console.timeEnd("test");
    console.warn("Warning!");
    console.error("Error!");
    console.info("Info!");
    // const r = await fetch("https://iftc.koyeb.app/api", {
    //     headers: {
    //         "User-Agent": "IFTC Bot",
    //     },
    // });
    // const rc = await r.clone();
    // console.log(r);
    // console.log(await r.blob());
    // console.log(await r.text());
    // console.log(await rc.json());
    const b = new Blob(["Hello, World!"], { type: "text/plain" });
    console.log(b);
    console.log(b.size);
    console.log(b.type);
    console.log(await b.arrayBuffer());
    const url = new URL("https://iftc.koyeb.app/api");
    console.log(url);
    console.log(os.platform());
    console.log(os.arch());
    console.log(os.hostname());
    console.log(os.totalmem());
    console.log(os.freemem());
    console.log(os.cpus());
    console.log(os.userInfo());
    console.log(os.type());
    console.log(os.release());
    console.log(os.uptime());
    console.log(os.loadavg());
    console.log(os.homedir());
    console.log(os.tmpdir());
    console.log(os.version());
    console.log(os.machine());
    console.log(os.EOL);
    console.log(os.devNull);
    console.log(process.stdin);
    console.log(process.stdout);
    console.log(process.stderr);
}
main();
