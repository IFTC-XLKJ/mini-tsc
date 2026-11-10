import * as sevenZip from "7zip";
import * as fs from "fs";

function main(): void {
  fs.mkdirSync("out_7zpw_src", true);
  fs.writeFileSync("out_7zpw_src/hello.txt", "hello world\n");

  console.log("STEP 1: compress with password");
  sevenZip.compress("out_7zpw_src", "out_7zpw_out/pw.7z", { password: "sekrit", level: 6 });
  console.log("STEP 2: isArchive:", sevenZip.isArchive("out_7zpw_out/pw.7z"), "|", sevenZip.lastError());
  console.log("STEP 3: list:", JSON.stringify(sevenZip.list("out_7zpw_out/pw.7z")), "|", sevenZip.lastError());
  console.log("STEP 4: correct-password read:", sevenZip.readFile("out_7zpw_out/pw.7z", "hello.txt", "sekrit"), "|", sevenZip.lastError());
  console.log("pw3 done");
}

main();
