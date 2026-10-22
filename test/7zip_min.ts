import * as sevenZip from "7zip";
function main(): void {
  const entries: any = sevenZip.list("test/fixtures/sample.7z");
  for (let i = 0; i < entries.length; i++) {
    console.log(i, "[" + entries[i].name + "]", "dir:", entries[i].isDir, "size:", entries[i].size);
  }
  console.log("lastError:", sevenZip.lastError());
}
