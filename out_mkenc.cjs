// Build a minimal 7zAES-encrypted .7z containing one file, using Copy+AES.
// Test fixture generator for the mini-tsc 7zip module password path.
const crypto = require("crypto");
const fs = require("fs");

function writeNumber(n) {
  if (n < 0x80) return Buffer.from([n]);
  const extra = [];
  let v = n;
  while (v > 0) {
    extra.push(v & 0xff);
    v = Math.floor(v / 256);
  }
  const ec = extra.length;
  const firstByteMask = (0xff << (8 - ec)) & 0xff;
  return Buffer.from([firstByteMask, ...extra.reverse()]);
}

function utf16leName(s) {
  return Buffer.concat([Buffer.from(s, "utf16le"), Buffer.from([0, 0])]);
}

function crc32(buf) {
  const table = [];
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  let crc = 0xffffffff;
  for (const b of buf) crc = table[(crc ^ b) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

const PASSWORD = "s3cret-pw";
const FILE_NAME = "secret.txt";
const FILE_DATA = Buffer.from("top secret payload\n", "utf8");

const cycles = 19;
const salt = Buffer.alloc(0);
const iv = crypto.randomBytes(16);

function deriveKey(password, cycles, salt) {
  const pwd = Buffer.from(password, "utf16le");
  if (cycles === 0x3f) {
    return Buffer.concat([salt, pwd, Buffer.alloc(32)]).slice(0, 32);
  }
  const rounds = 1n << BigInt(cycles);
  const h = crypto.createHash("sha256");
  const rbuf = Buffer.alloc(8);
  for (let i = 0n; i < rounds; i++) {
    h.update(salt);
    h.update(pwd);
    rbuf.writeBigUInt64LE(i);
    h.update(rbuf);
  }
  return h.digest().slice(0, 32);
}

const key = deriveKey(PASSWORD, cycles, salt);

function pad16(buf) {
  const rem = buf.length % 16;
  return rem === 0 ? buf : Buffer.concat([buf, Buffer.alloc(16 - rem)]);
}
const cipher = crypto.createCipheriv("aes-256-cbc", key, iv);
cipher.setAutoPadding(false);
const packedData = Buffer.concat([cipher.update(pad16(FILE_DATA)), cipher.final()]);

const aesProps = Buffer.concat([
  Buffer.from([(cycles & 0x3f) | 0x40]),
  Buffer.from([(iv.length - 1) & 0x0f]),
  iv,
]);

function coderEntry(methodIdBytes, props) {
  const flag = methodIdBytes.length | (props.length > 0 ? 0x20 : 0);
  const parts = [Buffer.from([flag]), methodIdBytes];
  if (props.length > 0) {
    parts.push(writeNumber(props.length));
    parts.push(props);
  }
  return Buffer.concat(parts);
}

function folderBytes() {
  const parts = [];
  parts.push(writeNumber(2));
  // Coder[0] MUST be the pack-stream consumer. For 7zAES the packed bytes are
  // encrypted, so AES is coder[0] and the main decoder (Copy) is coder[1]:
  //   packed -> AES (coder0) -> Copy (coder1) -> output.
  parts.push(coderEntry(Buffer.from([0x06, 0xf1, 0x07, 0x01]), aesProps)); // AES
  parts.push(coderEntry(Buffer.from([0x00]), Buffer.alloc(0)));            // Copy
  // numBonds = numCoders - 1 is implicit (not stored).
  parts.push(writeNumber(1));   // InIndex = 1 (Copy input <- coder0 output)
  parts.push(writeNumber(0));   // OutIndex = 0 (AES output feeds it)
  // numPackStreams = numInStreams - numBindPairs = 1, so no explicit packed
  // stream index is written (the decoder infers the single unused input).
  return Buffer.concat(parts);
}

function streamsInfo() {
  const parts = [];
  parts.push(Buffer.from([0x06]));       // PackInfo
  parts.push(writeNumber(0));            // packPos (dataOffset) = 0
  parts.push(writeNumber(1));            // numPackStreams
  parts.push(Buffer.from([0x09]));       // Size
  parts.push(writeNumber(packedData.length));
  parts.push(Buffer.from([0x00]));       // END

  parts.push(Buffer.from([0x07]));       // UnpackInfo
  parts.push(Buffer.from([0x0b]));       // Folder
  parts.push(writeNumber(1));            // numFolders
  parts.push(Buffer.from([0x00]));       // external=0
  parts.push(folderBytes());
  parts.push(Buffer.from([0x0c]));       // CodersUnpackSize
  // One entry per coder OUTPUT (numCodersOutStreams = numCoders = 2 here):
  //   coder0 = AES   -> output = plaintext size
  //   coder1 = Copy  -> output = plaintext size
  parts.push(writeNumber(FILE_DATA.length)); // AES output
  parts.push(writeNumber(FILE_DATA.length)); // Copy output
  parts.push(Buffer.from([0x00]));       // END of UnpackInfo

  // SubStreamsInfo (0x08) with a CRC (0x0a) of the plaintext. This is what
  // lets the decoder detect a wrong password: AES-CBC has no integrity check,
  // so a bad key yields same-length garbage that only the CRC catches.
  const fileCrc = crc32(FILE_DATA);
  const crcBuf = Buffer.alloc(4);
  crcBuf.writeUInt32LE(fileCrc, 0);
  parts.push(Buffer.from([0x08]));       // SubStreamsInfo
  parts.push(Buffer.from([0x0a]));       // CRC
  parts.push(Buffer.from([0x01]));       // AllAreDefined
  parts.push(crcBuf);                    // CRC value (plaintext)
  parts.push(Buffer.from([0x00]));       // END of SubStreamsInfo

  parts.push(Buffer.from([0x00]));       // END of StreamsInfo
  return Buffer.concat(parts);
}

function filesInfo() {
  const parts = [];
  parts.push(Buffer.from([0x05]));       // FilesInfo
  parts.push(writeNumber(1));            // numFiles

  // Name (0x11): Size, External(0x00), UTF-16LE names
  const nameBuf = utf16leName(FILE_NAME);
  parts.push(Buffer.from([0x11]));
  parts.push(writeNumber(nameBuf.length + 1)); // size includes External byte
  parts.push(Buffer.from([0x00]));       // not external
  parts.push(nameBuf);

  // Attributes (0x15). Writer order (7zOut.cpp WriteAlignedBools):
  //   type, Size, BoolVector_numDefined(Defs), External(0), UINT32[per defined]
  // Size = numDefined*4 + bvSize + 2, where bvSize = 0 when all files defined.
  const numDefined = 1;
  const attrDataSize = numDefined * 4 + 0 + 2;
  const attrPayload = Buffer.concat([
    Buffer.from([0x01]),                 // numDefined==numFiles -> single byte 1
    Buffer.from([0x00]),                 // External: 0 (inline)
    Buffer.from([0x20, 0x00, 0x00, 0x00])// FILE_ATTRIBUTE_ARCHIVE
  ]);
  parts.push(Buffer.from([0x15]));
  parts.push(writeNumber(attrDataSize));
  parts.push(attrPayload);

  parts.push(Buffer.from([0x00]));       // END
  return Buffer.concat(parts);
}

const header = Buffer.concat([
  Buffer.from([0x01]),                   // Header
  Buffer.from([0x04]),                   // MainStreamsInfo wrapper
  streamsInfo(),
  filesInfo(),
  Buffer.from([0x00]),                   // END
]);

const headerCrcVal = crc32(header);
const startHeaderBlock = Buffer.alloc(20);
startHeaderBlock.writeBigUInt64LE(BigInt(packedData.length), 0);
startHeaderBlock.writeBigUInt64LE(BigInt(header.length), 8);
startHeaderBlock.writeUInt32LE(headerCrcVal, 16);
const startHeaderCrc = crc32(startHeaderBlock);

const startHeader = Buffer.concat([
  Buffer.from([0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c]),
  Buffer.from([0x00, 0x04]),
  (() => { const b = Buffer.alloc(4); b.writeUInt32LE(startHeaderCrc, 0); return b; })(),
  startHeaderBlock,
]);

const archive = Buffer.concat([startHeader, packedData, header]);
fs.writeFileSync("test/fixtures/encrypted.7z", archive);
console.log("wrote test/fixtures/encrypted.7z", archive.length, "bytes (packed", packedData.length, "header", header.length + ")");