import * as sevenZip from "7zip";
import * as fs from "fs";

function main(): void {
  fs.mkdirSync("out_7zpw_src", true);
  fs.writeFileSync("out_7zpw_src/hello.txt", "hello world\n");

  console.log("STEP 1: password compress");
  sevenZip.compress("out_7zpw_src", "out_7zpw_out/pw.7z", { password: "sekrit", level: 6 });
  console.log("STEP 2: isArchive:", sevenZip.isArchive("out_7zpw_out/pw.7z"), "|", sevenZip.lastError());
  console.log("STEP 3: no-password read:", sevenZip.readFile("out_7zpw_out/pw.7z", "hello.txt"));
  console.log("STEP 4: wrong-password read:", sevenZip.readFile("out_7zpw_out/pw.7z", "hello.txt", "wrong"));
  console.log("STEP 5: correct-password read:", sevenZip.readFile("out_7zpw_out/pw.7z", "hello.txt", "sekrit"));
  console.log("pw test done");
}

main();
