import * as fs from "fs";

function main(): void {
    console.log("Starting...");
    fs.writeFileSync("test_output.txt", "Hello!");
    console.log("File written!");
    const content = fs.readFileSync("test_output.txt");
    console.log("Content:", content);
    fs.unlinkSync("test_output.txt");
    console.log("File deleted!");
    console.log("Done!");
}

main();
