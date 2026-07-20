import * as fs from "fs";

async function main(): Promise<void> {
    console.log("=== Testing Async FS Functions ===");

    // Test writeFile
    console.log("\n--- writeFile ---");
    const writeResult = await fs.writeFile("test_output.txt", "Hello from async writeFile!");
    console.log("writeFile result:", writeResult);

    // Test readFile
    console.log("\n--- readFile ---");
    const content = await fs.readFile("test_output.txt");
    console.log("readFile content:", content);

    // Test stat
    console.log("\n--- stat ---");
    const stat = await fs.stat("test_output.txt");
    console.log("stat:", stat);

    // Test access (exists)
    console.log("\n--- access (exists) ---");
    const exists = await fs.access("test_output.txt");
    console.log("file exists:", exists);

    // Test readdir
    console.log("\n--- readdir ---");
    const files = await fs.readdir(".");
    console.log("files:", files);

    // Test unlink
    console.log("\n--- unlink ---");
    const unlinkResult = await fs.unlink("test_output.txt");
    console.log("unlink result:", unlinkResult);

    // Test mkdir
    console.log("\n--- mkdir ---");
    const mkdirResult = await fs.mkdir("test_dir");
    console.log("mkdir result:", mkdirResult);

    // Test rmdir
    console.log("\n--- rmdir ---");
    const rmdirResult = await fs.rmdir("test_dir");
    console.log("rmdir result:", rmdirResult);

    console.log("\n=== All tests passed! ===");
}

main();
