import * as sevenZip from "7zip";
import * as fs from "fs";

function eq(a: string, b: string): boolean {
  return a === b;
}

function main(): void {
  fs.mkdirSync("out_7zcomp_src", true);
  fs.writeFileSync("out_7zcomp_src/hello.txt", "hello world\n");
  fs.writeFileSync("out_7zcomp_src/sub/nested.txt", "nested data\n");
  fs.writeFileSync("out_7zcomp_src/data.json", '{"a":1,"b":2,"c":[1,2,3]}');

  // 1. single file, default options
  sevenZip.compress("out_7zcomp_src/hello.txt", "out_7zcomp_out/one.7z");
  console.log("one.7z isArchive:", sevenZip.isArchive("out_7zcomp_out/one.7z"), "|", sevenZip.lastError());
  console.log("one.7z hello.txt:", sevenZip.readFile("out_7zcomp_out/one.7z", "hello.txt"));

  // 2. directory, lzma2 level 9 dict 1MB threads 2
  sevenZip.compress("out_7zcomp_src", "out_7zcomp_out/dir.7z", {
    format: "7z", method: "lzma2", level: 9, dictSize: 1048576,
    wordSize: 3, threads: 2, solidSize: 0,
  });
  console.log("dir.7z isArchive:", sevenZip.isArchive("out_7zcomp_out/dir.7z"), "|", sevenZip.lastError());
  console.log("dir.7z hello.txt:", sevenZip.readFile("out_7zcomp_out/dir.7z", "hello.txt"));
  console.log("dir.7z sub/nested.txt:", sevenZip.readFile("out_7zcomp_out/dir.7z", "sub/nested.txt"));
  console.log("dir.7z data.json:", sevenZip.readFile("out_7zcomp_out/dir.7z", "data.json"));

  // 3. extract the directory archive
  console.log("extracted count:", sevenZip.extractToDir("out_7zcomp_out/dir.7z", "out_7zcomp_extracted"), "|", sevenZip.lastError());
  console.log("extracted hello:", fs.readFileSync("out_7zcomp_extracted/hello.txt", "utf8"));
  console.log("extracted nested:", fs.readFileSync("out_7zcomp_extracted/sub/nested.txt", "utf8"));

  // 4. copy method
  sevenZip.compress("out_7zcomp_src", "out_7zcomp_out/copy.7z", { method: "copy" });
  console.log("copy.7z data.json:", sevenZip.readFile("out_7zcomp_out/copy.7z", "data.json"), "|", sevenZip.lastError());

  // 5. password (7zAES)
  sevenZip.compress("out_7zcomp_src", "out_7zcomp_out/pw.7z", { password: "sekrit", level: 6 });
  console.log("pw.7z isArchive:", sevenZip.isArchive("out_7zcomp_out/pw.7z"), "|", sevenZip.lastError());
  console.log("pw.7z no-password read:", sevenZip.readFile("out_7zcomp_out/pw.7z", "hello.txt"));
  console.log("pw.7z wrong-password read:", sevenZip.readFile("out_7zcomp_out/pw.7z", "hello.txt", "wrong"));
  console.log("pw.7z correct-password read:", sevenZip.readFile("out_7zcomp_out/pw.7z", "hello.txt", "sekrit"));

  // 6. volume split (produces .001/.002...; volume 1 = base .7z)
  sevenZip.compress("out_7zcomp_src", "out_7zcomp_out/vol.7z", { volumeSize: 64, level: 1 });
  console.log("vol.7z base exists:", fs.existsSync("out_7zcomp_out/vol.7z"));
  console.log("vol.7z.001 exists:", fs.existsSync("out_7zcomp_out/vol.7z.001"));

  // 7. level/dict matrix (smoke)
  sevenZip.compress("out_7zcomp_src", "out_7zcomp_out/l0.7z", { level: 0, dictSize: 262144 });
  console.log("level 0 roundtrip:", eq(sevenZip.readFile("out_7zcomp_out/l0.7z", "hello.txt"), "hello world\n"), "|", sevenZip.lastError());
  sevenZip.compress("out_7zcomp_src", "out_7zcomp_out/l3.7z", { level: 3, dictSize: 262144 });
  console.log("level 3 roundtrip:", eq(sevenZip.readFile("out_7zcomp_out/l3.7z", "hello.txt"), "hello world\n"), "|", sevenZip.lastError());
  sevenZip.compress("out_7zcomp_src", "out_7zcomp_out/l5.7z", { level: 5, dictSize: 262144 });
  console.log("level 5 roundtrip:", eq(sevenZip.readFile("out_7zcomp_out/l5.7z", "hello.txt"), "hello world\n"), "|", sevenZip.lastError());

  // 8. threads=4 solid=64KB
  sevenZip.compress("out_7zcomp_src", "out_7zcomp_out/mt.7z", { level: 6, threads: 4, solidSize: 65536 });
  console.log("mt solid=64K threads=4 roundtrip:", eq(sevenZip.readFile("out_7zcomp_out/mt.7z", "hello.txt"), "hello world\n"), "|", sevenZip.lastError());

  console.log("7zip compress test done");
}
