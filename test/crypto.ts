import * as crypto from "crypto";

function main(): void {
  // Test md5
  const md5Hash = crypto.md5("hello world");
  console.log("MD5:", md5Hash);

  // Test sha256
  const sha256Hash = crypto.sha256("hello world");
  console.log("SHA256:", sha256Hash);

  // Test sha512
  const sha512Hash = crypto.sha512("hello world");
  console.log("SHA512:", sha512Hash);

  // Test sha1
  const sha1Hash = crypto.sha1("hello world");
  console.log("SHA1:", sha1Hash);

  // Test randomBytes
  const randomHex = crypto.randomBytes(16);
  console.log("Random:", randomHex);

  // Test randomUUID
  const uuid = crypto.randomUUID();
  console.log("UUID:", uuid);

  // Test createHash
  const hash = crypto.createHash("sha256");
  hash.update("hello ");
  hash.update("world");
  const digest = hash.digest("hex");
  console.log("createHash:", digest);

  // Test hmac_sha256
  const hmac = crypto.hmac_sha256("secret", "message");
  console.log("HMAC-SHA256:", hmac);

  // Test pbkdf2Sync
  const key = crypto.pbkdf2Sync("password", "salt", 1000, 32);
  console.log("PBKDF2:", key);

  console.log("All crypto tests passed!");
}
main();
