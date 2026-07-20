async function main(): Promise<void> {
    const url = new URL("https://iftc.koyeb.app/api");
    console.log("href:", url.href);
    console.log("protocol:", url.protocol);
    console.log("host:", url.host);
    console.log("hostname:", url.hostname);
    console.log("port:", url.port);
    console.log("pathname:", url.pathname);
    console.log("search:", url.search);
    console.log("hash:", url.hash);
    console.log("toString:", url.toString());
}
main();
