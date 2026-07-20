import * as process from "process";
import * as commander from "commander"; // 导入 commander 模块 (与 npm 中 commander 库用法一致)
function main(): void {
    const program = commander.program;
    program.version("1.0.0").name("command").description("A TypeScript transpiler for Node.js");
    program
        .command("test <testArgument>")
        .description("The test argument")
        .action((testArgument: string) => {
            console.log(testArgument);
        });
    program.option("-t, --test <testArgument>", "The test argument");
    program.parse(process.argv);
    const options = program.opts();
    console.log(options);
}
main();
