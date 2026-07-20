function main(): void {
    console.log(process.stdout);
    console.log(process.stdout.rows, process.stdout.columns);
    process.stdout.write("Hello, World!\n");
    process.stdout.cursorTo(0, 0);
    process.stdout.moveCursor(1, 1);
    process.stdout.clearScreenDown();
    process.stdout.clearLine(0);
}
main();
