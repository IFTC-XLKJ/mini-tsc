import * as sevenZip from "7zip";

function main(): void {
  const a = "test/fixtures/plain_copy.7z";
  console.log("isArchive:", sevenZip.isArchive(a));
  const entries: any = sevenZip.list(a);
  if (entries === null) {
    console.log("list: null |", sevenZip.lastError());
  } else {
    console.log("list count:", entries.length);
    console.log("name:", entries[0].name, "size:", entries[0].size);
  }
  console.log("read:", sevenZip.readFile(a, "plain.txt"), "|", sevenZip.lastError());
}

main();