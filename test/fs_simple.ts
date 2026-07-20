import * as fs from "fs";

async function main(): Promise<void> {
    console.log("Starting...");
    await fs.writeFile("test_output.txt", "Hello!");
    console.log("File written!");
    const content = await fs.readFile("test_output.txt");
    console.log("Content:", content);
    await fs.unlink("test_output.txt");
    console.log("File deleted!");
    console.log("Done!");
}

main();
