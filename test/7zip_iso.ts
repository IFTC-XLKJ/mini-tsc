import * as sevenZip from "7zip";
import * as fs from "fs";

function main(): void {
  fs.mkdirSync("out_7ziso_src", true);
  fs.writeFileSync("out_7ziso_src/a.txt", "AAAA");
  fs.writeFileSync("out_7ziso_src/b.txt", "BBBB");

  // copy method: no compression, CRC should be straightforward
  sevenZip.compress("out_7ziso_src/a.txt", "out_7ziso_out/copy_a.7z", { method: "copy" });
  console.log("copy a.7z isArchive:", sevenZip.isArchive("out_7ziso_out/copy_a.7z"), "|", sevenZip.lastError());
  console.log("copy a read:", sevenZip.readFile("out_7ziso_out/copy_a.7z", "a.txt"), "|", sevenZip.lastError());

  sevenZip.compress("out_7ziso_src", "out_7ziso_out/copy_dir.7z", { method: "copy" });
  console.log("copy dir isArchive:", sevenZip.isArchive("out_7ziso_out/copy_dir.7z"), "|", sevenZip.lastError());
  console.log("copy dir a:", sevenZip.readFile("out_7ziso_out/copy_dir.7z", "a.txt"), "|", sevenZip.lastError());
  console.log("copy dir b:", sevenZip.readFile("out_7ziso_out/copy_dir.7z", "b.txt"), "|", sevenZip.lastError());

  // lzma2 single file
  sevenZip.compress("out_7ziso_src/a.txt", "out_7ziso_out/lz_a.7z");
  console.log("lz a read:", sevenZip.readFile("out_7ziso_out/lz_a.7z", "a.txt"), "|", sevenZip.lastError());

  console.log("iso done");
}

main();
