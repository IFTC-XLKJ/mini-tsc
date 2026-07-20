import * as child_process from "child_process";

function main(): void {
    child_process.exec('echo "hello world"', (error, stdout, stderr) => {
        if (error) {
            console.error(`执行错误: ${error}`);
            return;
        }
        console.log(`标准输出:\n${stdout}`);
        if (stderr) {
            console.error(`标准错误输出:\n${stderr}`);
        }
    });

    child_process.execFile("node", ["--version"], (error, stdout, stderr) => {
        if (error) {
            throw error;
        }
        console.log(stdout);
    });

    const dir = child_process.spawn("dir");
    dir.stdout.on("data", (data) => {
        console.log(data.toString());
    });
    dir.stderr.on("data", (data) => {
        console.error(data.toString());
    });
    dir.on("close", (code, signal) => {
        console.log(`子进程退出，退出码: ${code} signal: ${signal}`);
    });

    const forked = child_process.fork("./test/child_process_fork.ts");
    forked.on("message", (msg) => {
        console.log(msg);
    });
    forked.send({ hello: "world" });
}
main();
