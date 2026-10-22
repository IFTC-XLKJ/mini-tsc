import * as sevenZip from "7zip";

function main(): void {
  const archive = "test/fixtures/encrypted.7z";
  const password = "s3cret-pw";

  console.log("isArchive:", sevenZip.isArchive(archive));

  const entries: any = sevenZip.list(archive, password);
  if (entries === null) {
    console.log("list failed |", sevenZip.lastError());
  } else {
    console.log("list count:", entries.length, "name:", entries[0].name);
  }

  // Encrypted content cannot be read without the password.
  const noPw: any = sevenZip.readFile(archive, "secret.txt");
  console.log("no-password read is null:", noPw === null, "|", sevenZip.lastError());
  const wrong: any = sevenZip.readFile(archive, "secret.txt", "wrong-pw");
  console.log("wrong-password read is null:", wrong === null, "|", sevenZip.lastError());

  // Correct password decrypts.
  const plain = sevenZip.readFile(archive, "secret.txt", password);
  console.log("decrypted:", plain);

  // Password via the handle API.
  const h = sevenZip.open(archive, password);
  console.log("handle count:", sevenZip.handleCount(h));
  console.log("handle read:", sevenZip.handleRead(h, "secret.txt"));
  sevenZip.close(h);

  // extractToDir with password.
  const n: any = sevenZip.extractToDir(archive, "out_7zip_enc", password);
  console.log("extracted count:", n);

  console.log("7zip password test done");
}

main();