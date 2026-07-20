async function main(): Promise<void> {
    // GET request
    console.log("=== GET Request ===");
    const getRes = await fetch("https://api.github.com");
    console.log("Status:", getRes.status);

    // POST request with body
    console.log("\n=== POST Request ===");
    const postRes = await fetch("https://httpbin.org/post", {
        method: "POST",
        headers: {
            "Content-Type": "application/json",
        },
        body: JSON.stringify({ name: "test", value: 123 }),
    });
    console.log("Status:", postRes.status);

    // PUT request
    console.log("\n=== PUT Request ===");
    const putRes = await fetch("https://httpbin.org/put", {
        method: "PUT",
        body: JSON.stringify({ update: true }),
    });
    console.log("Status:", putRes.status);

    // DELETE request
    console.log("\n=== DELETE Request ===");
    const deleteRes = await fetch("https://httpbin.org/delete", {
        method: "DELETE",
    });
    console.log("Status:", deleteRes.status);

    // HEAD request
    console.log("\n=== HEAD Request ===");
    const headRes = await fetch("https://httpbin.org/get", {
        method: "HEAD",
    });
    console.log("Status:", headRes.status);

    // Stream response body
    console.log("\n=== Stream Response Body ===");
    const streamRes = await fetch("https://iftc.koyeb.app/stream-test", {
        headers: {
            "user-agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36 Edg/150.0.0.0",
            accept: "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7",
            "accept-language": "zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6",
            "cache-control": "max-age=0",
            priority: "u=0, i",
            "sec-ch-ua": '"Not;A=Brand";v="8", "Chromium";v="150", "Microsoft Edge";v="150"',
            "sec-ch-ua-mobile": "?0",
            "sec-ch-ua-platform": '"Windows"',
            "sec-fetch-dest": "document",
            "sec-fetch-mode": "navigate",
            "sec-fetch-site": "none",
            "sec-fetch-user": "?1",
            "upgrade-insecure-requests": "1",
        },
        body: null,
        method: "GET",
        mode: "cors",
        credentials: "omit",
    });
    console.log("Stream status:", streamRes.status);
    const reader = streamRes.body?.getReader()
    while (true) {
        const chunk = (await reader?.read()) || { done: true, value: "" };
        if (chunk.done) {
            break;
        }
        process.stdout.write(chunk.value);
    }
    process.stdout.write("\n");
}
main();
