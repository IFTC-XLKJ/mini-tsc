import * as process from "process";
async function main() {
    const args = process.argv.slice(1);
    for (const s of args[0]) {
        process.stdout.write(s);
        await wait(100);
    }
}
main();

function wait(ms: number): Promise<void> {
    return new Promise(resolve => setTimeout(resolve, ms));
}