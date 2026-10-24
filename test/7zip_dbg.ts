import * as sevenZip from "7zip";
import * as fs from "fs";

function main(): void {
  console.log("STEP 0: starting");
  fs.mkdirSync("out_7zdbg_src", true);
  console.log("STEP 1: dir made");
  fs.writeFileSync("out_7zdbg_src/hello.txt", "hello world\n");
  console.log("STEP 2: file written");

  sevenZip.compress("out_7zdbg_src/hello.txt", "out_7zdbg_out/one.7z");
  console.log("STEP 3: single-file compress done |", sevenZip.isArchive("out_7zdbg_out/one.7z"), "|", sevenZip.lastError());
  console.log("STEP 4: read:", sevenZip.readFile("out_7zdbg_out/one.7z", "hello.txt"));

  console.log("STEP 5: dir compress default opts");
  sevenZip.compress("out_7zdbg_src", "out_7zdbg_out/dir.7z");
  console.log("STEP 6: dir.7z read:", sevenZip.readFile("out_7zdbg_out/dir.7z", "hello.txt"), "|", sevenZip.lastError());

  console.log("STEP 7: dir compress with opts threads=2");
  sevenZip.compress("out_7zdbg_src", "out_7zdbg_out/dir2.7z", { level: 6, threads: 2 });
  console.log("STEP 8: dir2.7z read:", sevenZip.readFile("out_7zdbg_out/dir2.7z", "hello.txt"), "|", sevenZip.lastError());

  console.log("7zip debug done");
}

main();
