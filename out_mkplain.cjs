// Build a minimal plain (unencrypted) Copy-only .7z containing one file.
const fs = require("fs");

function wn(n) {
  if (n < 0x80) return Buffer.from([n]);
  const extra = [];
  let v = n;
  while (v > 0) { extra.push(v & 0xff); v = Math.floor(v / 256); }
  const ec = extra.length;
  const mask = (0xff << (8 - ec)) & 0xff;
  return Buffer.from([mask, ...extra.reverse()]);
}
function u16(s) { return Buffer.concat([Buffer.from(s, "utf16le"), Buffer.from([0, 0])]); }
function crc32(buf) {
  const t = [];
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  let crc = 0xffffffff;
  for (const b of buf) crc = t[(crc ^ b) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

const data = Buffer.from("hello copy 7z\n", "utf8");
const name = "plain.txt";

// Folder: 1 coder Copy (id 0x00), no props, implicit numBonds=0, 1 pack stream.
const folder = Buffer.concat([wn(1), Buffer.from([0x01, 0x00])]);

const streams = Buffer.concat([
  Buffer.from([0x06]), wn(0), wn(1), Buffer.from([0x09]), wn(data.length), Buffer.from([0x00]),
  Buffer.from([0x07]), Buffer.from([0x0b]), wn(1), Buffer.from([0x00]), folder,
  Buffer.from([0x0c]), wn(data.length), Buffer.from([0x00]), // ends UnpackInfo
  Buffer.from([0x00]),                                        // ends StreamsInfo
]);

// FilesInfo: 0x05, numFiles, Name(0x11), END
const nb = u16(name);
const files = Buffer.concat([
  Buffer.from([0x05]), wn(1),
  Buffer.from([0x11]), wn(nb.length + 1), Buffer.from([0x00]), nb,
  Buffer.from([0x00]),
]);

const header = Buffer.concat([Buffer.from([0x01]), Buffer.from([0x04]), streams, files, Buffer.from([0x00])]);
const hcrc = crc32(header);
const shb = Buffer.alloc(20);
shb.writeBigUInt64LE(BigInt(data.length), 0);
shb.writeBigUInt64LE(BigInt(header.length), 8);
shb.writeUInt32LE(hcrc, 16);
const shcrc = crc32(shb);
const sh = Buffer.concat([
  Buffer.from([0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c]),
  Buffer.from([0x00, 0x04]),
  (() => { const b = Buffer.alloc(4); b.writeUInt32LE(shcrc, 0); return b; })(),
  shb,
]);

fs.writeFileSync("test/fixtures/plain_copy.7z", Buffer.concat([sh, data, header]));
console.log("wrote plain_copy.7z", sh.length + data.length + header.length, "bytes");