function main(): void {
    process.on("message", (message: any) => {
        console.log(message);
        process.send("hello from child process");
    });
}
main();
