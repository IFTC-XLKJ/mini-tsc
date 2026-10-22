import * as sevenZip from "7zip";
import * as fs from "fs";

function main(): void {
  const archive = "test/fixtures/sample.7z";

  // isArchive on a real 7z and a non-archive
  console.log("isArchive(7z):", sevenZip.isArchive(archive));
  console.log("isArchive(ts):", sevenZip.isArchive("test/7zip.ts"));

  // list entries
  const entries: any = sevenZip.list(archive);
  console.log("count:", entries.length);
  for (let i = 0; i < entries.length; i++) {
    const e = entries[i];
    console.log("  entry:", e.name, "size:", e.size, "isDir:", e.isDir);
  }

  // read entries into memory
  console.log("hello:", sevenZip.readFile(archive, "hello.txt"));
  console.log("data:", sevenZip.readFile(archive, "data.json"));
  console.log("nested:", sevenZip.readFile(archive, "sub/nested.txt"));

  // missing entry → null (no crash); reason via lastError()
  const missing: any = sevenZip.readFile(archive, "does-not-exist.txt");
  console.log("missing is null:", missing === null);
  console.log("lastError:", sevenZip.lastError());

  // handle API
  const handle = sevenZip.open(archive);
  console.log("handle count:", sevenZip.handleCount(handle));
  console.log("has hello.txt:", sevenZip.handleHas(handle, "hello.txt"));
  console.log("has missing.txt:", sevenZip.handleHas(handle, "missing.txt"));
  console.log("handle read:", sevenZip.handleRead(handle, "hello.txt"));
  sevenZip.close(handle);

  // entry-level helpers
  console.log("hasEntry(hello):", sevenZip.hasEntry(archive, "hello.txt"));
  console.log("hasEntry(nope):", sevenZip.hasEntry(archive, "nope.txt"));
  console.log("size(hello):", sevenZip.size(archive, "hello.txt"));
  console.log("size(nope):", sevenZip.size(archive, "nope.txt"));
  const one: any = sevenZip.getEntry(archive, "data.json");
  console.log("getEntry size:", one.size, "isDir:", one.isDir, "attrib:", one.attrib);
  const names: any = sevenZip.entryNames(archive);
  console.log("names count:", names.length, "first:", names[0]);
  const info: any = sevenZip.info(archive);
  console.log("info count:", info.count, "files:", info.fileCount, "dirs:", info.dirCount, "total:", info.totalSize);

  // binary-safe buffer read
  const buf: any = sevenZip.readFileBuffer(archive, "hello.txt");
  console.log("buffer length:", buf.length);

  // handle info + isOpen
  const h2 = sevenZip.open(archive);
  console.log("h2 isOpen:", sevenZip.handleIsOpen(h2));
  const h2names: any = sevenZip.handleNames(h2);
  console.log("h2 names:", h2names.length);
  console.log("h2 size(hello):", sevenZip.handleSize(h2, "hello.txt"));
  const hi: any = sevenZip.handleInfo(h2);
  console.log("h2 info files:", hi.fileCount);
  const h2buf: any = sevenZip.handleReadBuffer(h2, "hello.txt");
  console.log("h2 readBuffer len:", h2buf.length);
  sevenZip.close(h2);
  console.log("h2 isOpen after close:", sevenZip.handleIsOpen(h2));
  // double close must be a safe no-op; use-after-close degrades gracefully
  sevenZip.close(h2);
  console.log("h2 count after close:", sevenZip.handleCount(h2));
  console.log("h2 lastError:", sevenZip.lastError());

  // extract everything into a directory (creates sub/)
  const extracted: any = sevenZip.extractToDir(archive, "out_7zip_test");
  console.log("extracted file count:", extracted);
  console.log("extracted hello:", fs.readFileSync("out_7zip_test/hello.txt", "utf8"));
  console.log("extracted nested:", fs.readFileSync("out_7zip_test/sub/nested.txt", "utf8"));

  // extract only the "sub" subtree
  const subCount: any = sevenZip.extractMatching(archive, "sub", "out_7zip_test_sub");
  console.log("subtree extracted count:", subCount);
  console.log("subtree nested:", fs.readFileSync("out_7zip_test_sub/sub/nested.txt", "utf8"));

  // extract a single file
  sevenZip.extractFile(archive, "data.json", "out_7zip_test/one.json");
  console.log("one.json:", fs.readFileSync("out_7zip_test/one.json", "utf8"));

  // extractMatching via an open handle
  const h3 = sevenZip.open(archive);
  const h3count: any = sevenZip.handleExtractMatching(h3, "sub", "out_7zip_test_sub2");
  console.log("handle subtree extracted count:", h3count);
  sevenZip.close(h3);

  // prefix that matches nothing → 0 files, no error
  const none: any = sevenZip.extractMatching(archive, "nope", "out_7zip_test_none");
  console.log("non-matching prefix count:", none);

  // extract into a freshly-created nested destination
  const deep: any = sevenZip.extractToDir(archive, "out_7zip_test_deep/a/b");
  console.log("deep dir count:", deep);
  console.log("deep hello:", fs.readFileSync("out_7zip_test_deep/a/b/hello.txt", "utf8"));

  // error surface: missing archive path → null + lastError
  const bad: any = sevenZip.extractToDir("test/fixtures/does-not-exist.7z", "out_7zip_test_bad");
  console.log("bad archive is null:", bad === null, "|", sevenZip.lastError());

  console.log("7zip smoke test done");
}