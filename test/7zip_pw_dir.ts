import * as sevenZip from "7zip";
import * as fs from "fs";

fs.mkdirSync("out_7zpwdir_src", true);
fs.mkdirSync("out_7zpwdir_src/sub", true);
fs.writeFileSync("out_7zpwdir_src/hello.txt", "hello world\n");
fs.writeFileSync("out_7zpwdir_src/sub/nested.txt", "nested data\n");

sevenZip.compress("out_7zpwdir_src", "out_7zpwdir_out/pw.7z", { password: "sekrit", level: 6 });
console.log("list:", JSON.stringify(sevenZip.list("out_7zpwdir_out/pw.7z")));
console.log("hello:", sevenZip.readFile("out_7zpwdir_out/pw.7z", "hello.txt", "sekrit"), "|", sevenZip.lastError());
console.log("nested:", sevenZip.readFile("out_7zpwdir_out/pw.7z", "sub/nested.txt", "sekrit"), "|", sevenZip.lastError());
