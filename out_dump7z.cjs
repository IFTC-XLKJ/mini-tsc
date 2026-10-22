// Correct 7z NUMBER decoder + header dumper, used to reverse-engineer a valid
// header layout from test/fixtures/sample.7z.
const fs = require("fs");

const b = fs.readFileSync(process.argv[2] || "test/fixtures/sample.7z");
const nextOff = Number(b.readBigUInt64LE(12));
const nextSize = Number(b.readBigUInt64LE(20));
const h = b.slice(32 + nextOff, 32 + nextOff + nextSize);

let p = 0;
function rdN() {
  // 7z NUMBER: count leading 1-bits of first byte = number of extra bytes.
  const first = h[p++];
  let mask = 0x80, extra = 0;
  while (mask & first) { extra++; mask >>= 1; }
  if (extra === 0) return first;
  // high bits are the low (8-extra-1) bits of the first byte
  const highBits = first & ((1 << (7 - extra)) - 1);
  let y = 0;
  for (let i = 0; i < extra; i++) y |= h[p++] << (8 * i);
  return highBits * Math.pow(2, 8 * extra) + y;
}
function byte() { return h[p++]; }
function hex1() { return h[p].toString(16).padStart(2, "0"); }

function dump(indent, msg) { console.log(indent + msg); }

dump("", "header hex: " + h.toString("hex"));
dump("", "ID " + hex1()); p++;
// StreamsInfo inline (encoded header path)
dump("", "ID " + hex1()); p++; // PackInfo 06
dump("", "  numPackStreams=" + rdN());
dump("", "ID " + hex1()); p++; // Size 09
dump("", "  packSize=" + rdN());
while (h[p] !== 0x00) { dump("", "  pack extra ID " + hex1()); p++; const n = rdN(); dump("", "   skip " + n); p += n; }
dump("", "ID " + hex1()); p++; // END
dump("", "ID " + hex1()); p++; // UnpackInfo 07
dump("", "ID " + hex1()); p++; // Folder 0b
const numFolders = rdN(); dump("", "  numFolders=" + numFolders);
for (let f = 0; f < numFolders; f++) {
  dump("", "  folder external=" + hex1()); p++;
  const numCoders = rdN(); dump("", "    numCoders=" + numCoders);
  for (let c = 0; c < numCoders; c++) {
    const flag = byte();
    const idSize = flag & 0xf;
    const id = h.slice(p, p + idSize); p += idSize;
    let props = Buffer.alloc(0);
    if (flag & 0x20) { const ps = rdN(); props = h.slice(p, p + ps); p += ps; }
    dump("", "    coder id=" + id.toString("hex") + " props=" + props.toString("hex"));
    if (flag & 0x10) { dump("", "      inStreams=" + rdN()); dump("", "      outStreams=" + rdN()); }
  }
}
dump("", "ID " + hex1()); p++; // CodersUnpackSize 0c
for (let f = 0; f < numFolders; f++) dump("", "  unpackSize=" + rdN());
dump("", "next ID " + hex1()); p++;
dump("", "rest: " + h.slice(p - 1).toString("hex"));