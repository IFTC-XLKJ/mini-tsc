import * as process from "process";
function main(): void {
    console.log(process.stdin);
    process.stdin.on("data", (data) => {
        console.log(data.toString());
    });
}
main();
