import * as sevenZip from "7zip";
import * as fs from "fs";

function main(): void {
  fs.mkdirSync("out_7zvol_src", true);
  fs.mkdirSync("out_7zvol_out", true);
  const big = Buffer.alloc(1000);
  for (let i = 0; i < 1000; i++) big[i] = i;
  fs.writeFileSync("out_7zvol_src/big.bin", big);
  console.log("wrote big.bin, size:", fs.statSync("out_7zvol_src/big.bin").size);
  sevenZip.compress("out_7zvol_src/big.bin", "out_7zvol_out/vol.7z", { volumeSize: 64, level: 1 });
  console.log("vol.7z exists:", fs.existsSync("out_7zvol_out/vol.7z"));
  console.log("vol.7z.001 exists:", fs.existsSync("out_7zvol_out/vol.7z.001"));
  console.log("vol.7z.002 exists:", fs.existsSync("out_7zvol_out/vol.7z.002"));
  console.log("vol.7z.003 exists:", fs.existsSync("out_7zvol_out/vol.7z.003"));
  console.log("lastError:", sevenZip.lastError());
}

main();
