async function main(): Promise<void> {
    console.log("start");

    setTimeout(() => {
        console.log("timeout 50");
    }, 50);

    let n = 0;
    const id = setInterval(() => {
        n = n + 1;
        console.log("tick");
        console.log(n);
        if (n >= 3) {
            clearInterval(id);
            console.log("cleared");
        }
    }, 30);

    setTimeout(
        (msg: any) => {
            console.log(msg);
        },
        10,
        "hello-arg",
    );
    await wait(100);
    console.log("wait 100ms");
}
main();

function wait(ms: number): Promise<void> {
    return new Promise((resolve) => {
        setTimeout(resolve, ms);
    });
}
