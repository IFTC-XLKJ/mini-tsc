/* 7zEnc.c -- 7z archive writer for the vendored 7-Zip subset.
 *
 * Pairs with the vendored decoder (7zArcIn.c): writes archives in the same
 * on-disk format that 7-Zip / p7zip / py7zr — and the decoder in this tree —
 * can read. One folder holds the whole archive (a single LZMA2 or Copy
 * stream); with a password the packed stream is wrapped in a 7zAES coder so
 * the folder chain is [AES, main] — exactly the shape the decoder's 7zAES
 * path already handles (7zDec.c: AES as coder[0], packed stream
 * AES-decrypted into memory, inner coder run over it).
 *
 * On-disk 7z layout produced:
 *
 *   volume 1 (single-volume or the first of N):
 *     0..31   32-byte start header:
 *               sig '7z\xBC\xAF\x27\x1C' (6) + major(1) + minor(1)
 *               NextHeadCRC (4, LE32)  bytes 8..11
 *               NextHeadPos   (8, LE64) bytes 12..19
 *               StartHeadSize (2, LE16) bytes 20..21   = 0 (uncompressed)
 *     32..    packed stream (all pack streams concatenated)
 *     end     NextHeader block (Files ... StreamsInfo ... End) and its
 *             4-byte CRC is embedded in the start header at bytes 8..11.
 *
 *   volume 2..N (split archives): a bare packed-stream slice (no start
 *   header, no NextHeader); each volume is exactly `volumeSize` bytes
 *   except the last. The caller (node_7zip.c) re-invokes z7enc_compress7z
 *   once per volume, writing volumes 2..N to `in->archiveOut + ".NNN"`.
 *
 * The public interface is in 7zEnc.h; directory walking + the Value-level
 * API live in node_7zip.c.
 */

#include "Precomp.h"
#include "7zEnc.h"

#include "7zTypes.h"
#include "7zAlloc.h"
#include "7zCrc.h"
#include "7zFile.h"
#include "Lzma2Enc.h"
#include "Ppmd7.h"
#include "7zAesEnc.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const ISzAlloc g_EncAlloc = { SzAlloc, SzFree };

/* ------------------------------ byte writer ------------------------------ */

typedef struct {
  Byte* d;
  size_t n, cap;
} WB;

static int wb_reserve(WB* b, size_t need) {
  if (b->n + need <= b->cap) return 0;
  size_t c2 = b->cap ? b->cap : 128;
  while (c2 < b->n + need) c2 *= 2;
  Byte* nd = (Byte*)realloc(b->d, c2);
  if (!nd) return -1;
  b->d = nd;
  b->cap = c2;
  return 0;
}
static int wb_byte(WB* b, Byte c) {
  if (wb_reserve(b, 1)) return -1;
  b->d[b->n++] = c;
  return 0;
}
static int wb_bytes(WB* b, const Byte* p, size_t n) {
  if (n == 0) return 0;
  if (wb_reserve(b, n)) return -1;
  memcpy(b->d + b->n, p, n);
  b->n += n;
  return 0;
}
static int wb_le32(WB* b, UInt32 v) {
  if (wb_reserve(b, 4)) return -1;
  b->d[b->n++] = (Byte)(v & 0xFF);
  b->d[b->n++] = (Byte)((v >> 8) & 0xFF);
  b->d[b->n++] = (Byte)((v >> 16) & 0xFF);
  b->d[b->n++] = (Byte)((v >> 24) & 0xFF);
  return 0;
}

/* GILP (variable-length int per the 7z format, "size" variant).
 * 0 → 0x00; otherwise 7-bit groups, low group first, 0x80 continuation
 * marker on every non-final group. */
static int wb_gilp(WB* b, UInt64 v) {
  if (v == 0)
    return wb_byte(b, 0);
  do {
    Byte c = (Byte)(v & 0x7F);
    v >>= 7;
    if (v) c |= 0x80;
    if (wb_byte(b, c)) return -1;
  } while (v);
  return 0;
}

/* 7z "byte array with bits": `count` bits, MSB-first (0x80 = bit 0). */
static int wb_bitvector(WB* b, UInt32 count, const Byte* flags) {
  for (UInt32 i = 0; i < (count + 7) / 8; i++) {
    Byte byte = 0;
    for (unsigned bit = 0; bit < 8 && i * 8 + bit < count; bit++)
      if (flags[i * 8 + bit])
        byte |= (Byte)(0x80 >> bit);
    if (wb_byte(b, byte)) return -1;
  }
  return 0;
}

/* Append scratch `s` to `b`, freeing s either way. Returns 0 on success. */
static int wb_splice(WB* b, WB* s) {
  int r = wb_bytes(b, s->d, s->n);
  free(s->d);
  s->d = NULL;
  s->n = s->cap = 0;
  return r;
}

/* --------------------------- options resolve ----------------------------- */

void Z7EncOptions_Default(Z7EncOptions* o) {
  if (!o) return;
  memset(o, 0, sizeof(*o));
  o->format = Z7ENC_FMT_7Z;
  o->method = Z7ENC_METHOD_LZMA2;
}

/* Clamp `memory` (bytes) to a usable dict size. With 0 the level default
 * applies. Larger values merely cap the dictionary the encoder will take;
 * the encoder itself still allocates as needed. Returns the effective dict. */
static UInt32 z7enc_resolve_memory(const Z7EncOptions* o, UInt32 dictFromLevel) {
  UInt64 m = o ? o->memory : 0;
  if (m <= 0) return dictFromLevel;
  if (m < ((UInt64)1 << 12)) m = (UInt64)1 << 12;          /* LZMA minimum dict */
  if (m > ((UInt64)1 << 32)) m = (UInt64)1 << 32;          /* fit in UInt32 */
  return (m < (UInt64)dictFromLevel) ? (UInt32)m : dictFromLevel;
}

int Z7EncOptions_Resolve(const Z7EncOptions* in, UInt32* outDict,
                         unsigned* outThreads, unsigned* outLevel) {
  int level = in ? in->level : 0;
  UInt32 dict;
  unsigned threads;
  if (level < 0) level = 0;
  if (level > 9) level = 9;
  switch (level) {
    case 0: dict = 1u << 16; break;
    case 1: dict = 1u << 18; break;
    case 2: dict = 1u << 20; break;
    case 3: dict = 1u << 21; break;
    case 4: dict = 1u << 22; break;
    case 5: dict = 1u << 23; break;
    case 6: dict = 1u << 24; break;
    case 7: dict = 1u << 25; break;
    case 8: dict = 1u << 26; break;
    case 9: dict = 1u << 27; break;
    default: dict = 1u << 22; break;
  }
  if (in && in->dictSize) dict = in->dictSize;
  /* `memory` caps the dict: a caller asking for less memory than the level
   * default gets the smaller dict. */
  dict = z7enc_resolve_memory(in, dict);
  if (dict < (1u << 12)) dict = 1u << 12;
  if (dict > (3u << 29)) dict = 3u << 29;
  threads = (in && in->threads) ? in->threads : 1;
  if (outDict) *outDict = dict;
  if (outThreads) *outThreads = threads;
  if (outLevel) *outLevel = (unsigned)level;
  return 0;
}

/* Apply `dict` (bytes), `wordSize` (LZMA lc, 0..8), `solidSize` (bytes) and
 * `threads` to a fresh CLzma2EncProps. `reduceSize` (expected total source
 * bytes; 0 = unknown) lets the encoder shrink its dict to fit the data. */
static void z7enc_fill_lzma2_props(CLzma2EncProps* p, UInt32 dict,
                                   unsigned wordSize, UInt64 solidSize,
                                   unsigned threads, UInt64 reduceSize) {
  Lzma2EncProps_Init(p);
  p->lzmaProps.dictSize = dict;
  /* wordSize maps to the LZMA "literal context bits" (lc) — the 7-Zip GUI
   * labels this "Word size". 0..8; 0 = leave at default (3). */
  if (wordSize > 0) p->lzmaProps.lc = (int)wordSize;
  p->lzmaProps.numThreads = (threads > 1) ? 2 : 1;
  p->lzmaProps.reduceSize = reduceSize ? reduceSize : (UInt64)(Int64)-1;
  if (solidSize > 0) p->blockSize = solidSize;
  else p->blockSize = LZMA2_ENC_PROPS_BLOCK_SIZE_SOLID;
  int mtThreads = (int)(threads > 1 ? threads : 2);
#ifdef Z7ENC_MT_THREADS
  if (mtThreads > Z7ENC_MT_THREADS) mtThreads = Z7ENC_MT_THREADS;
#endif
  p->numBlockThreads_Max = mtThreads;
  p->numBlockThreads_Reduced = p->numBlockThreads_Max;
  Lzma2EncProps_Normalize(p);
}

static Byte z7enc_lzma2_prop(UInt32 dict) {
  CLzma2EncProps p;
  Byte r = 0;
  z7enc_fill_lzma2_props(&p, dict, 0, 0, 1, 0);
  {
    CLzma2EncHandle h = Lzma2Enc_Create(&g_EncAlloc, &g_EncAlloc);
    if (h) {
      Lzma2Enc_SetProps(h, &p);
      r = Lzma2Enc_WriteProperties(h);
      Lzma2Enc_Destroy(h);
    }
  }
  return r;
}

/* --------------------------- PPMd (PPMdH) encode -------------------------- */
/* 7-Zip PPMd coder id (PPMdH with 7z range coder): 0x030401.
 * Props (5 bytes): [order, memSize LE32]. */

#define Z7ENC_PPMD_METHOD_ID   0x030401u
#define Z7ENC_PPMD_PROPS_SIZE  5u

/* Simple growable memory IByteOut for Ppmd7's range encoder. */
typedef struct {
  IByteOut vt;
  Byte* d;
  size_t n, cap;
} Z7MemOut;

static int z7_memout_grow(Z7MemOut* m, size_t need) {
  (void)need;
  if (m->n + 1 <= m->cap) return 0;
  size_t c2 = m->cap ? m->cap : 256;
  while (c2 < m->n + 1) c2 *= 2;
  Byte* nd = (Byte*)realloc(m->d, c2);
  if (!nd) return -1;
  m->d = nd;
  m->cap = c2;
  return 0;
}

static void z7_memout_write(IByteOutPtr p, Byte b) {
  Z7MemOut* m = (Z7MemOut*)p;
  if (z7_memout_grow(m, 0) != 0) return;
  m->d[m->n++] = b;
}

/* Map compression level 0..9 to PPMd7 max order (2..32). */
static unsigned z7enc_ppmd_order(int level) {
  static const unsigned orders[10] = { 2, 4, 6, 8, 10, 12, 16, 20, 24, 32 };
  if (level < 0) level = 0;
  if (level > 9) level = 9;
  return orders[level];
}

/* Build the 5-byte PPMd coder properties. `ppmdMem` is the PPMd memory
 * budget in bytes (0 = auto). */
static void z7enc_ppmd_props(Byte out[5], unsigned order, UInt32 ppmdMem) {
  out[0] = (Byte)order;
  if (ppmdMem < (UInt32)PPMD7_MIN_MEM_SIZE) ppmdMem = 1u << 18; /* 256 KB */
  if (ppmdMem > (UInt32)(512u << 20)) ppmdMem = 512u << 20;
  out[1] = (Byte)(ppmdMem & 0xFF);
  out[2] = (Byte)((ppmdMem >> 8) & 0xFF);
  out[3] = (Byte)((ppmdMem >> 16) & 0xFF);
  out[4] = (Byte)((ppmdMem >> 24) & 0xFF);
}

/* Compress `src` (totalUnpack bytes) with Ppmd7 into `pack` buffer.
 * Returns 0 on success, -1 on error. `pack->n` gets the compressed size. */
static int z7enc_ppmd_encode(const Byte* src, size_t totalUnpack,
                              int level, UInt32 ppmdMem,
                              WB* pack) {
  CPpmd7 p;
  Z7MemOut mo = {0};
  unsigned order = z7enc_ppmd_order(level);
  UInt32 mem = ppmdMem ? ppmdMem : (UInt32)(totalUnpack * 64 + 1024);

  Ppmd7_Construct(&p);
  if (!Ppmd7_Alloc(&p, mem, &g_EncAlloc)) {
    Ppmd7_Free(&p, &g_EncAlloc);
    return -1;
  }
  Ppmd7_Init(&p, order);

  mo.vt.Write = z7_memout_write;
  p.rc.enc.Stream = &mo.vt;
  Ppmd7z_Init_RangeEnc(&p);

  if (totalUnpack > 0) {
    Ppmd7z_EncodeSymbols(&p, src, src + totalUnpack);
  }
  Ppmd7z_Flush_RangeEnc(&p);
  Ppmd7_Free(&p, &g_EncAlloc);

  pack->d = mo.d;
  pack->n = mo.n;
  pack->cap = mo.cap;
  return 0;
}

void Z7EncResult_Clear(Z7EncResult* r) {
  if (!r) return;
  memset(r, 0, sizeof(*r));
}

/* ------------------------------ entry read -------------------------------- */

static int z7enc_read_entry(const Z7EncInput* in, size_t idx, Byte** outData, size_t* outSize) {
  size_t want = in->entries[idx].size;
  if (!in->readFile || want == 0) {
    *outData = NULL;
    *outSize = 0;
    return 0;
  }
  Byte* data = (Byte*)malloc(want);
  if (!data) return -1;
  size_t got = in->readFile(in, idx, data, want, in->ctx);
  if (got != want) {
    free(data);
    return -1;
  }
  *outData = data;
  *outSize = want;
  return 0;
}

static Byte* z7enc_entry_data(const Z7EncInput* in, size_t idx) {
  Byte* data = NULL;
  size_t len = 0;
  if (z7enc_read_entry(in, idx, &data, &len) != 0) return NULL;
  return data;
}

/* ------------------------------ coder encoding --------------------------- */

/* Write one coder item into a Coder (0x06) property block, matching the
 * encoding SzGetNextFolderItem expects:
 *
 *   mainByte: 0b00 | (twoInOne ? 0x10 : 0) | (hasProps ? 0x20 : 0) | idSize
 *   idSize bytes of the method id, MSB-first (id = UInt32, truncated to the
 *            shortest representation with no leading-zero bytes)
 *   if twoInOne: NumInStreams (Gilp), NumOutStreams (Gilp)  -- both 1 here
 *   if hasProps:  PropsSize (Gilp, < 0x80) + the property bytes
 *
 * LZMA2's method id 0x21 has a single non-zero byte → idSize 1.
 * 7zAES's 0x06F10701 has 4 significant bytes → idSize 4.
 * Copy's method id 0 has no bytes → idSize 0, no props. */
static int wb_coder_item(WB* b, UInt32 method, int twoInOne,
                        const Byte* props, size_t propsSize) {
  int idSize = 0;
  unsigned i;
  if (method & 0xFF000000u) idSize = 4;
  else if (method & 0x00FF0000u) idSize = 3;
  else if (method & 0x0000FF00u) idSize = 2;
  else if (method & 0x000000FFu) idSize = 1;

  {
    Byte mainByte = (Byte)((twoInOne ? 0x10 : 0) |
                           (propsSize > 0 ? 0x20 : 0) |
                           (Byte)idSize);
    if (wb_byte(b, mainByte)) return -1;
    /* id bytes MSB-first: for idSize=4, bytes 3,2,1,0 of the 32-bit id */
    for (i = (unsigned)idSize; i > 0; i--)
      if (wb_byte(b, (Byte)((method >> (8 * (i - 1))) & 0xFF)))
        return -1;
  }
  if (twoInOne) {
    if (wb_byte(b, 1) || wb_byte(b, 1)) return -1; /* in=1, out=1 */
  }
  if (propsSize > 0) {
    if (wb_byte(b, (Byte)propsSize)) return -1;
    if (wb_bytes(b, props, propsSize)) return -1;
  }
  return 0;
}

/* ------------------------------ 7z writer ---------------------------------- */

/* Build the NextHeader block per the 7z format that SzReadHeader2 expects:
 *
 *   k7zIdHeader (0x01)
 *     k7zIdMainStreamsInfo (0x04)
 *       k7zIdPackInfo (0x06)
 *         dataOffset (GILP = 0)
 *         numPackStreams (GILP = 1)
 *         k7zIdSize (0x09) + packSize (GILP)
 *         k7zIdEnd
 *       k7zIdUnpackInfo (0x07)
 *         k7zIdFolder (0x0B)
 *         numFolders (GILP = 1)
 *         external (0)
 *         numCoders (GILP)
 *         coder definitions
 *         [bonds when numCoders > 1]
 *         k7zIdCodersUnpackSize (0x0C) + unpackSize per coder out-stream
 *         k7zIdEnd
 *       k7zIdSubStreamsInfo (0x08)   [only when numUnpackStreams > 0]
 *         k7zIdNumUnpackStream (0x0D) + numStreams per folder
 *         k7zIdSize (0x09) + sizes for first (numStreams-1) substreams
 *         k7zIdCRC (0x0A) + allDefined(1) + CRCs
 *         k7zIdEnd
 *       k7zIdEnd
 *     k7zIdFilesInfo (0x05)
 *       numFiles (GILP)
 *       [k7zIdEmptyStream (0x0E) + bitvector]   [if any empty streams]
 *       [k7zIdEmptyFile (0x0F) + bitvector]     [if any empty streams]
 *       k7zIdName (0x11) + size + external(0) + UTF-16LE names
 *       k7zIdEnd
 *     k7zIdEnd
 *
 * `packTotal` is the TOTAL packed-stream size across all volumes
 * (single-volume: pack.n; split: the full pack stream length). */
static int z7enc_build_header(const Z7EncInput* in, const Z7EncOptions* o,
                              UInt32 dict, UInt32 nFiles, UInt64 totalUnpack,
                              int haveAes, int useCopy, int usePPMd,
                              const Byte* ppmdProps,
                              const Byte* aesProps, size_t aesPropsSize,
                              size_t packTotal, size_t aesUnpackSize,
                              WB* out_h, Byte** out_all) {
  const Z7EncEntry* files = in->entries;
  Byte lzma2Prop = z7enc_lzma2_prop(dict);
  Byte mainProps[1] = { lzma2Prop };
  Byte ppmdMainProps[5];
  WB* h = out_h;
  int wr = 0;
  int numCoders = haveAes ? 2 : 1;
  UInt32 i;
  UInt32 numEmptyStreams = 0, numUnpackStreams = 0;
  UInt64 totalNameLen = 0;
  if (usePPMd) z7enc_ppmd_props(ppmdMainProps, z7enc_ppmd_order(o->level),
                                 o->ppmdMemSize ? o->ppmdMemSize
                                                : (UInt32)(totalUnpack * 64 + 1024));

  /* Classify entries: empty streams are dirs and zero-size files; every
   * other entry is one substream of the single folder. */
  for (i = 0; i < nFiles; i++) {
    if (files[i].isDir || files[i].size == 0) {
      numEmptyStreams++;
    } else {
      numUnpackStreams++;
    }
    totalNameLen += (UInt64)(strlen(files[i].name) + 1) * 2;
  }

  /* ---- k7zIdHeader ---- */
  if (wb_byte(h, 0x01)) wr = 1;

  /* ---- k7zIdMainStreamsInfo ---- */
  if (!wr && wb_byte(h, 0x04)) wr = 1;

  /* k7zIdPackInfo */
  if (!wr) {
    WB f = {0};
    if (wb_byte(&f, 0x06) ||          /* k7zIdPackInfo */
        wb_gilp(&f, 0) ||             /* dataOffset */
        wb_gilp(&f, 1) ||             /* numPackStreams */
        wb_byte(&f, 0x09) ||          /* k7zIdSize */
        wb_gilp(&f, (UInt64)packTotal) ||
        wb_byte(&f, 0x00))            /* k7zIdEnd */
      wr = 1;
    if (wr) { free(f.d); return -1; }
    wr = wb_splice(h, &f);
    if (wr) return -1;
  }

  /* k7zIdUnpackInfo */
  if (!wr) {
    WB f = {0};
    if (wb_byte(&f, 0x07) ||          /* k7zIdUnpackInfo */
        wb_byte(&f, 0x0B) ||          /* k7zIdFolder */
        wb_gilp(&f, 1) ||             /* numFolders */
        wb_byte(&f, 0) ||             /* external */
        wb_byte(&f, (Byte)numCoders)) /* numCoders */
      wr = 1;
    /* coder definitions: [AES, main] when encrypted, else [main]. */
    if (!wr && haveAes) {
      if (wb_coder_item(&f, z7_aes_method_id(), 1, aesProps, aesPropsSize)) wr = 1;
      if (!wr && useCopy) {
        if (wb_coder_item(&f, 0, 1, NULL, 0)) wr = 1;
      } else if (!wr && usePPMd) {
        if (wb_coder_item(&f, Z7ENC_PPMD_METHOD_ID, 1, ppmdMainProps, Z7ENC_PPMD_PROPS_SIZE)) wr = 1;
      } else if (!wr) {
        if (wb_coder_item(&f, 0x21, 1, mainProps, 1)) wr = 1;
      }
      /* Bond: AES (coder 0) output feeds the main coder (coder 1).
       * 7z bond = {InIndex, OutIndex} where InIndex is the in-stream index
       * (1 = main's input) and OutIndex is the coder whose output is routed
       * (0 = AES). Matches py7zr's Bond(incoder=1, outcoder=0) and 7-Zip's
       * CheckSupportedFolder expectation {InIndex=1, OutIndex=0}. */
      if (!wr && (wb_byte(&f, 1) || wb_byte(&f, 0))) wr = 1;
    } else if (!wr && useCopy) {
      if (wb_coder_item(&f, 0, 0, NULL, 0)) wr = 1;
    } else if (!wr && usePPMd) {
      if (wb_coder_item(&f, Z7ENC_PPMD_METHOD_ID, 0, ppmdMainProps, Z7ENC_PPMD_PROPS_SIZE)) wr = 1;
    } else if (!wr) {
      if (wb_coder_item(&f, 0x21, 0, mainProps, 1)) wr = 1;
    }
    /* k7zIdCodersUnpackSize: one size per coder output stream.
     * AES coder output = packed data size before AES padding.
     * Main coder output = totalUnpack (uncompressed). */
    if (!wr && wb_byte(&f, 0x0C)) wr = 1;
    if (!wr && haveAes && wb_gilp(&f, aesUnpackSize)) wr = 1;  /* AES out */
    if (!wr && wb_gilp(&f, totalUnpack)) wr = 1;              /* main out */
    if (!wr && wb_byte(&f, 0x00)) wr = 1;          /* k7zIdEnd */
    if (wr) { free(f.d); return -1; }
    wr = wb_splice(h, &f);
    if (wr) return -1;
  }

  /* k7zIdSubStreamsInfo (only when there are non-empty files) */
  if (!wr && numUnpackStreams > 0) {
    WB f = {0};
    if (wb_byte(&f, 0x08)) wr = 1;                 /* k7zIdSubStreamsInfo */
    /* k7zIdNumUnpackStream: one folder, numStreams = numUnpackStreams */
    if (!wr && wb_byte(&f, 0x0D)) wr = 1;
    if (!wr && wb_gilp(&f, (UInt64)numUnpackStreams)) wr = 1;
    /* k7zIdSize: sizes for the first (numUnpackStreams - 1) substreams.
     * The decoder derives the last one from the folder unpack size. */
    if (!wr && wb_byte(&f, 0x09)) wr = 1;
    {
      UInt32 written = 0;
      for (i = 0; !wr && i < nFiles && written + 1 < numUnpackStreams; i++) {
        if (files[i].isDir || files[i].size == 0) continue;
        if (wb_gilp(&f, files[i].size)) wr = 1;
        written++;
      }
    }
    /* k7zIdCRC: allDefined=1 + one CRC per substream */
    if (!wr && wb_byte(&f, 0x0A)) wr = 1;
    if (!wr && wb_byte(&f, 1)) wr = 1;
    for (i = 0; !wr && i < nFiles; i++) {
      if (files[i].isDir || files[i].size == 0) continue;
      Byte* data = z7enc_entry_data(in, i);
      if (!data) { wr = 1; break; }
      if (wb_le32(&f, CrcCalc(data, files[i].size))) wr = 1;
      free(data);
    }
    if (!wr && wb_byte(&f, 0x00)) wr = 1;          /* k7zIdEnd */
    if (wr) { free(f.d); return -1; }
    wr = wb_splice(h, &f);
    if (wr) return -1;
  }

  /* k7zIdEnd of MainStreamsInfo */
  if (!wr && wb_byte(h, 0x00)) wr = 1;
  if (wr) return -1;

  /* ---- k7zIdFilesInfo ---- */
  if (!wr) {
    WB f = {0};
    Byte* emptyStreamFlags = NULL;
    Byte* emptyFileFlags = NULL;
    if (wb_byte(&f, 0x05) ||          /* k7zIdFilesInfo */
        wb_gilp(&f, (UInt64)nFiles))
      wr = 1;
    if (!wr && numEmptyStreams > 0) {
      emptyStreamFlags = (Byte*)calloc((nFiles + 7) / 8, 1);
      emptyFileFlags = (Byte*)calloc((numEmptyStreams + 7) / 8, 1);
      if (!emptyStreamFlags || !emptyFileFlags) {
        free(emptyStreamFlags); free(emptyFileFlags);
        free(f.d);
        return -1;
      }
      {
        UInt32 efi = 0;
        for (i = 0; i < nFiles; i++) {
          if (files[i].isDir || files[i].size == 0) {
            emptyStreamFlags[i >> 3] |= (Byte)(0x80 >> (i & 7));
            if (!files[i].isDir)
              emptyFileFlags[efi >> 3] |= (Byte)(0x80 >> (efi & 7));
            efi++;
          }
        }
      }
      if (wb_byte(&f, 0x0E) ||        /* k7zIdEmptyStream */
          wb_gilp(&f, (UInt64)((nFiles + 7) / 8)))
        wr = 1;
      if (!wr && wb_bytes(&f, emptyStreamFlags, (nFiles + 7) / 8)) wr = 1;
      if (!wr && wb_byte(&f, 0x0F) || /* k7zIdEmptyFile */
          wb_gilp(&f, (UInt64)((numEmptyStreams + 7) / 8)))
        wr = 1;
      if (!wr && wb_bytes(&f, emptyFileFlags, (numEmptyStreams + 7) / 8)) wr = 1;
      free(emptyStreamFlags);
      free(emptyFileFlags);
    }
    /* k7zIdName: size includes the external byte, matching the fixture. */
    if (!wr && wb_byte(&f, 0x11)) wr = 1;
    if (!wr && wb_gilp(&f, (UInt64)(1 + totalNameLen))) wr = 1;
    if (!wr && wb_byte(&f, 0)) wr = 1;   /* external = 0 */
    for (i = 0; !wr && i < nFiles; i++) {
      const char* nm = files[i].name;
      size_t l = strlen(nm);
      for (size_t k = 0; !wr && k <= l; k++) {
        Byte ch = (k < l) ? (Byte)nm[k] : 0;
        if (wb_byte(&f, ch) || wb_byte(&f, 0)) wr = 1;
      }
    }
    if (!wr && wb_byte(&f, 0x00)) wr = 1;  /* k7zIdEnd */
    if (wr) { free(f.d); return -1; }
    wr = wb_splice(h, &f);
    if (wr) return -1;
  }

  /* k7zIdEnd of Header */
  if (!wr && wb_byte(h, 0x00)) wr = 1;
  if (wr) { free(h->d); return -1; }
  if (out_all) *out_all = NULL;
  return 0;
}

/* Write the 32-byte start header to `f` (a CFileOutStream).
 *
 * 7z StartHeader layout (32 bytes):
 *   0..5    signature '7z\xBC\xAF\x27\x1C'
 *   6       major version (0)
 *   7       minor version (4)
 *   8..11   StartHeaderCRC  = CRC32(bytes 12..31)
 *   12..19  NextHeaderOffset (UInt64, relative to end of this 32-byte header)
 *   20..27  NextHeaderSize   (UInt64)
 *   28..31  NextHeaderCRC    (UInt32)
 *
 * `nextHeadPos` is the offset of the NextHeader block relative to the end of
 * the start header; `nextHeadSize` is its byte length; `nextHeadCRC` is the
 * CRC32 of the NextHeader block. For single-volume archives nextHeadPos =
 * 32 + packLen. For split archives, every volume's start header points to
 * volume 1's NextHeader (the decoder reads volume 1 to find the full header).
 *
 * The StartHeaderCRC at bytes 8..11 is computed over bytes 12..31 *after*
 * those fields are filled, so the decoder's own check
 * (CrcCalc(header + 12, 20) == GetUi32(header + 8)) passes. */
static int z7enc_write_start_header(CFileOutStream* fos, UInt32 nextHeadCRC,
                                    UInt64 nextHeadPos, UInt64 nextHeadSize) {
  Byte sh[32];
  const Byte sig[6] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };
  size_t k;
  UInt32 startHeadCRC;
  memcpy(sh, sig, 6);
  sh[6] = 0; /* major */
  sh[7] = 4; /* minor */
  /* bytes 8..11 = StartHeaderCRC, filled last */
  for (k = 0; k < 8; k++)
    sh[12 + k] = (Byte)((nextHeadPos >> (8 * k)) & 0xFF);   /* NextHeaderOffset */
  for (k = 0; k < 8; k++)
    sh[20 + k] = (Byte)((nextHeadSize >> (8 * k)) & 0xFF);  /* NextHeaderSize  */
  sh[28] = (Byte)(nextHeadCRC & 0xFF);                     /* NextHeaderCRC   */
  sh[29] = (Byte)((nextHeadCRC >> 8) & 0xFF);
  sh[30] = (Byte)((nextHeadCRC >> 16) & 0xFF);
  sh[31] = (Byte)((nextHeadCRC >> 24) & 0xFF);
  startHeadCRC = CrcCalc(sh + 12, 20);
  sh[8]  = (Byte)(startHeadCRC & 0xFF);
  sh[9]  = (Byte)((startHeadCRC >> 8) & 0xFF);
  sh[10] = (Byte)((startHeadCRC >> 16) & 0xFF);
  sh[11] = (Byte)((startHeadCRC >> 24) & 0xFF);
  {
    size_t wrote = ISeqOutStream_Write(&fos->vt, sh, sizeof(sh));
    return wrote == sizeof(sh) ? 0 : -1;
  }
}

/* Write a volume file: start header + a slice of the pack stream.
 * `packStart` is the byte offset into the full pack stream; `packLen` is
 * how many bytes of the pack stream this volume carries. `writeHead` is
 * 1 to emit the 32-byte start header (volume 1), 0 for bare slices
 * (volumes 2..N). */
static int z7enc_write_volume(const char* volPath, CSzFile* file, CFileOutStream* fos,
                              UInt32 nextHeadCRC, UInt64 nextHeadPos,
                              UInt64 nextHeadSize,
                              int writeHead,
                              const Byte* pack, size_t packStart, size_t packLen,
                              Z7EncResult* out) {
  (void)volPath;
  if (writeHead) {
    if (z7enc_write_start_header(fos, nextHeadCRC, nextHeadPos, nextHeadSize) != 0) {
      File_Close(file);
      out->error = "short write (volume start header)";
      return -1;
    }
  }
  if (packLen > 0) {
    size_t wrote = ISeqOutStream_Write(&fos->vt, pack + packStart, packLen);
    if (wrote != packLen) {
      File_Close(file);
      out->error = "short write (volume pack slice)";
      return -1;
    }
  }
  return 0;
}
/* Compress into a single .7z file (volume 1 only). */
static int z7enc_compress_single(const Z7EncInput* in, const Z7EncOptions* o,
                                 UInt32 dict, unsigned threads,
                                 Z7EncResult* out) {
  const Z7EncEntry* files = in->entries;
  UInt32 nFiles = (UInt32)in->numEntries;
  UInt64 totalUnpack = 0;
  int haveAes = (o->password && o->password[0]) ? 1 : 0;
  int useCopy = (o->method == Z7ENC_METHOD_COPY) ? 1 : 0;
  int usePPMd = (o->method == Z7ENC_METHOD_PPMd) ? 1 : 0;
  Byte* src = NULL;
  WB pack = {0};
  WB h = {0};

  for (size_t i = 0; i < in->numEntries; i++)
    totalUnpack += (UInt64)files[i].size;

  /* Concatenate all file bytes into one source buffer. */
  if (totalUnpack > 0) {
    src = (Byte*)malloc((size_t)totalUnpack);
    if (!src) { out->error = "out of memory"; return -1; }
    size_t off = 0;
    for (size_t i = 0; i < in->numEntries; i++) {
      if (files[i].isDir || files[i].size == 0) continue;
      Byte* data = z7enc_entry_data(in, i);
      if (!data) { free(src); out->error = "cannot read entry data"; return -1; }
      memcpy(src + off, data, files[i].size);
      off += files[i].size;
      free(data);
    }
  }

  /* ---- compress the source with one solid stream ---- */
  {
    SRes res = SZ_OK;
    if (useCopy) {
      if (totalUnpack > 0) {
        pack.d = (Byte*)malloc((size_t)totalUnpack);
        if (!pack.d) { free(src); out->error = "out of memory (copy)"; return -1; }
        pack.n = (size_t)totalUnpack;
        memcpy(pack.d, src, pack.n);
      } else {
        pack.d = (Byte*)malloc(1);
        if (!pack.d) { free(src); out->error = "out of memory (copy)"; return -1; }
        pack.n = 0;
      }
    } else if (usePPMd) {
      UInt32 ppmdMem = o->ppmdMemSize;
      if (z7enc_ppmd_encode(src ? src : (const Byte*)"",
                             (size_t)totalUnpack, o->level, ppmdMem, &pack) != 0) {
        free(src);
        out->error = "ppmd encode failed";
        return -1;
      }
    } else {
      CLzma2EncProps p;
      CLzma2EncHandle enc;
      UInt64 solid = o->solidSize;
      if (solid > 0 && solid < ((UInt64)1 << 12)) solid = (UInt64)1 << 12;
      z7enc_fill_lzma2_props(&p, dict, o->wordSize, solid, threads, totalUnpack);

      enc = Lzma2Enc_Create(&g_EncAlloc, &g_EncAlloc);
      if (!enc) { free(src); out->error = "out of memory (lzma2)"; return -1; }
      Lzma2Enc_SetProps(enc, &p);
      Lzma2Enc_SetDataSize(enc, totalUnpack ? totalUnpack : (UInt64)(Int64)-1);

      {
        size_t cap = (size_t)totalUnpack + ((size_t)totalUnpack >> 2) + 128;
        if (haveAes) cap = z7_aes_pad_to_block(cap) + 16;
        if (cap < 128) cap = 128;
        pack.d = (Byte*)malloc(cap);
        if (!pack.d) { Lzma2Enc_Destroy(enc); free(src); out->error = "out of memory"; return -1; }
        pack.n = cap;
      }
      res = Lzma2Enc_Encode2(enc,
          NULL, pack.d, &pack.n,
          NULL, src ? src : (const Byte*)"", (size_t)totalUnpack,
          NULL);
      Lzma2Enc_Destroy(enc);
      if (res != SZ_OK) { free(pack.d); free(src); out->error = "lzma2 encode failed"; return -1; }
    }
  }

  /* ---- optional AES wrap over the packed stream ---- */
  Byte aesProps[64];
  size_t aesPropsSize = 0;
  size_t aesUnpackSize = 0;  /* packed data size before AES padding */
  if (haveAes) {
    size_t pad = z7_aes_pad_to_block(pack.n);
    aesUnpackSize = pack.n;  /* save pre-pad size for CoderUnpackSizes */
    if (pad != pack.n) {
      Byte* nd = (Byte*)realloc(pack.d, pad ? pad : 16);
      if (!nd) { free(pack.d); free(src); out->error = "out of memory (aes pad)"; return -1; }
      memset(nd + pack.n, 0, pad - pack.n);
      pack.d = nd;
      pack.n = pad;
    }
    if (z7_aes_encode(o->password, aesProps, &aesPropsSize, pack.d, pack.n) != Z7_AES_ENC_OK) {
      free(pack.d); free(src); out->error = "AES encryption failed"; return -1;
    }
  }
  free(src);
  src = NULL;

  /* ---- build the NextHeader block ---- */
  Byte* all = NULL;
  if (z7enc_build_header(in, o, dict, nFiles, totalUnpack,
                         haveAes, useCopy, usePPMd, NULL,
                         aesProps, aesPropsSize,
                         pack.n, aesUnpackSize, &h, &all) != 0) {
    free(pack.d);
    out->error = "out of memory (header)";
    return -1;
  }

  /* ---- write: start header + pack stream + NextHeader block ---- */
  {
    CSzFile file;
    CFileOutStream fos;
    WRes wres = OutFile_Open(&file, in->archiveOut);
    if (wres != 0) { free(h.d); free(pack.d); out->error = "cannot open output file"; return -1; }
    FileOutStream_CreateVTable(&fos);
    fos.file = file;

    UInt32 nhc = CrcCalc(h.d, h.n);
    /* NextHeaderOffset is relative to the end of the 32-byte start header,
     * i.e. it equals the pack-stream length (pack.n), not 32 + pack.n. */
    UInt64 nextPos = (UInt64)pack.n;
    if (z7enc_write_start_header(&fos, nhc, nextPos, (UInt64)h.n) != 0) {
      File_Close(&file); free(h.d); free(pack.d);
      out->error = "short write (start header)";
      return -1;
    }
    if (pack.n > 0) {
      size_t wrote = ISeqOutStream_Write(&fos.vt, pack.d, pack.n);
      if (wrote != pack.n) { File_Close(&file); free(h.d); free(pack.d); out->error = "short write (pack)"; return -1; }
    }
    if (h.n > 0) {
      size_t wrote = ISeqOutStream_Write(&fos.vt, h.d, h.n);
      if (wrote != h.n) { File_Close(&file); free(h.d); free(pack.d); out->error = "short write (header)"; return -1; }
    }
    File_Close(&file);
    free(h.d);
    free(pack.d);
  }

  out->filesCount = nFiles;
  out->totalUnpacked = totalUnpack;
  out->totalPacked = pack.n;
  out->error = NULL;
  return 0;
}

/* Compress into split .7z volume files following the 7-Zip split convention:
 * volume 1 is `in->archiveOut` itself (start header + first slice + the full
 * NextHeader); volumes 2..N are `in->archiveOut.001`, `.002`, ... — bare
 * packed slices of `volSize` bytes each (last one shorter).
 *
 * The NextHeader's NextHeadPos points to volume 1's NextHeader offset so a
 * single-volume reader can open volume 1. A split archive is only fully
 * readable by an external tool (7z / p7zip) that reassembles the volumes;
 * the in-tree decoder reads a single file.
 *
 * `in->outVolCount` (when non-NULL) receives the number of volume files
 * written. */
static int z7enc_compress_split(const Z7EncInput* in, const Z7EncOptions* o,
                                UInt32 dict, unsigned threads,
                                Z7EncResult* out, int* outVolCount) {
  const Z7EncEntry* files = in->entries;
  UInt32 nFiles = (UInt32)in->numEntries;
  UInt64 totalUnpack = 0;
  int haveAes = (o->password && o->password[0]) ? 1 : 0;
  int useCopy = (o->method == Z7ENC_METHOD_COPY) ? 1 : 0;
  int usePPMd = (o->method == Z7ENC_METHOD_PPMd) ? 1 : 0;
  Byte* src = NULL;
  WB pack = {0};
  WB h = {0};
  UInt64 volSize = o->volumeSize;
  if (volSize < 1) volSize = 1;

  for (size_t i = 0; i < in->numEntries; i++)
    totalUnpack += (UInt64)files[i].size;

  if (totalUnpack > 0) {
    src = (Byte*)malloc((size_t)totalUnpack);
    if (!src) { out->error = "out of memory"; return -1; }
    size_t off = 0;
    for (size_t i = 0; i < in->numEntries; i++) {
      if (files[i].isDir || files[i].size == 0) continue;
      Byte* data = z7enc_entry_data(in, i);
      if (!data) { free(src); out->error = "cannot read entry data"; return -1; }
      memcpy(src + off, data, files[i].size);
      off += files[i].size;
      free(data);
    }
  }

  /* compress */
  {
    SRes res = SZ_OK;
    if (useCopy) {
      if (totalUnpack > 0) {
        pack.d = (Byte*)malloc((size_t)totalUnpack);
        if (!pack.d) { free(src); out->error = "out of memory (copy)"; return -1; }
        pack.n = (size_t)totalUnpack;
        memcpy(pack.d, src, pack.n);
      } else {
        pack.d = (Byte*)malloc(1);
        if (!pack.d) { free(src); out->error = "out of memory (copy)"; return -1; }
        pack.n = 0;
      }
    } else if (usePPMd) {
      UInt32 ppmdMem = o->ppmdMemSize;
      if (z7enc_ppmd_encode(src ? src : (const Byte*)"",
                             (size_t)totalUnpack, o->level, ppmdMem, &pack) != 0) {
        free(src);
        out->error = "ppmd encode failed";
        return -1;
      }
    } else {
      CLzma2EncProps p;
      CLzma2EncHandle enc;
      UInt64 solid = o->solidSize;
      if (solid > 0 && solid < ((UInt64)1 << 12)) solid = (UInt64)1 << 12;
      z7enc_fill_lzma2_props(&p, dict, o->wordSize, solid, threads, totalUnpack);
      enc = Lzma2Enc_Create(&g_EncAlloc, &g_EncAlloc);
      if (!enc) { free(src); out->error = "out of memory (lzma2)"; return -1; }
      Lzma2Enc_SetProps(enc, &p);
      Lzma2Enc_SetDataSize(enc, totalUnpack ? totalUnpack : (UInt64)(Int64)-1);
      {
        size_t cap = (size_t)totalUnpack + ((size_t)totalUnpack >> 2) + 128;
        if (haveAes) cap = z7_aes_pad_to_block(cap) + 16;
        if (cap < 128) cap = 128;
        pack.d = (Byte*)malloc(cap);
        if (!pack.d) { Lzma2Enc_Destroy(enc); free(src); out->error = "out of memory"; return -1; }
        pack.n = cap;
      }
      res = Lzma2Enc_Encode2(enc, NULL, pack.d, &pack.n,
          NULL, src ? src : (const Byte*)"", (size_t)totalUnpack, NULL);
      Lzma2Enc_Destroy(enc);
      if (res != SZ_OK) { free(pack.d); free(src); out->error = "lzma2 encode failed"; return -1; }
    }
  }

  Byte aesProps[64];
  size_t aesPropsSize = 0;
  size_t aesUnpackSize = 0;  /* packed data size before AES padding */
  if (haveAes) {
    size_t pad = z7_aes_pad_to_block(pack.n);
    aesUnpackSize = pack.n;  /* save pre-pad size for CoderUnpackSizes */
    if (pad != pack.n) {
      Byte* nd = (Byte*)realloc(pack.d, pad ? pad : 16);
      if (!nd) { free(pack.d); free(src); out->error = "out of memory (aes pad)"; return -1; }
      memset(nd + pack.n, 0, pad - pack.n);
      pack.d = nd;
      pack.n = pad;
    }
    if (z7_aes_encode(o->password, aesProps, &aesPropsSize, pack.d, pack.n) != Z7_AES_ENC_OK) {
      free(pack.d); free(src); out->error = "AES encryption failed"; return -1;
    }
  }
  free(src);
  src = NULL;

  Byte* all = NULL;
  if (z7enc_build_header(in, o, dict, nFiles, totalUnpack,
                         haveAes, useCopy, usePPMd, NULL,
                         aesProps, aesPropsSize,
                         pack.n, aesUnpackSize, &h, &all) != 0) {
    free(pack.d);
    out->error = "out of memory (header)";
    return -1;
  }

  UInt32 nhc = CrcCalc(h.d, h.n);
  /* Volume 1's NextHeader offset is relative to the end of the 32-byte start
   * header, i.e. it equals the first slice's length (volSize). */
  UInt64 vol1NextPos = volSize;

  int nVols = (int)((pack.n + volSize - 1) / volSize);
  if (nVols < 1) nVols = 1;
  int v;
  for (v = 0; v < nVols; v++) {
    /* Volume 1 is `in->archiveOut` itself; volumes 2..N are
     * `in->archiveOut.001`, `.002`, ... */
    const char* volPath = (v == 0) ? in->archiveOut
                                    : NULL;
    char contPath[4096];
    if (v != 0) {
      snprintf(contPath, sizeof(contPath), "%s.%03d", in->archiveOut, v);
      volPath = contPath;
    }
    CSzFile file;
    CFileOutStream fos;
    WRes wres = OutFile_Open(&file, volPath);
    if (wres != 0) {
      if (v == 0) free(h.d);
      free(pack.d);
      out->error = "cannot open volume file";
      return -1;
    }
    FileOutStream_CreateVTable(&fos);
    fos.file = file;

    if (v == 0) {
      /* Volume 1: start header + first slice + NextHeader */
      if (z7enc_write_start_header(&fos, nhc, vol1NextPos, (UInt64)h.n) != 0) {
        File_Close(&file); free(h.d); free(pack.d);
        out->error = "short write (vol1 start)";
        return -1;
      }
      size_t slice1 = (volSize < pack.n) ? (size_t)volSize : pack.n;
      if (slice1 > 0) {
        size_t wrote = ISeqOutStream_Write(&fos.vt, pack.d, slice1);
        if (wrote != slice1) {
          File_Close(&file); free(h.d); free(pack.d);
          out->error = "short write (vol1 pack)";
          return -1;
        }
      }
      if (h.n > 0) {
        size_t wrote = ISeqOutStream_Write(&fos.vt, h.d, h.n);
        if (wrote != h.n) {
          File_Close(&file); free(h.d); free(pack.d);
          out->error = "short write (vol1 header)";
          return -1;
        }
      }
      free(h.d);
      h.d = NULL;
    } else {
      /* Volumes 2..N: bare packed slice */
      size_t start = v * volSize;
      if (start >= pack.n) {
        File_Close(&file);
        continue;
      }
      size_t slen = pack.n - start;
      if (slen > volSize) slen = (size_t)volSize;
      if (slen > 0) {
        size_t wrote = ISeqOutStream_Write(&fos.vt, pack.d + start, slen);
        if (wrote != slen) {
          File_Close(&file); free(pack.d);
          out->error = "short write (vol pack slice)";
          return -1;
        }
      }
    }
    File_Close(&file);
  }
  if (pack.d) free(pack.d);

  if (outVolCount) *outVolCount = nVols;

  out->filesCount = nFiles;
  out->totalUnpacked = totalUnpack;
  out->totalPacked = pack.n;
  out->error = NULL;
  return 0;
}

int z7enc_compress7z(const Z7EncInput* in, const Z7EncOptions* optsIn, Z7EncResult* out) {
  Z7EncOptions o;
  UInt32 dict;
  unsigned threads, level;

  if (!in || !in->archiveOut || !out) { out->error = "invalid arguments"; return -1; }
  o = *optsIn;
  if (Z7EncOptions_Resolve(&o, &dict, &threads, &level) != 0) {
    out->error = "invalid options";
    return -1;
  }
  if (o.format == Z7ENC_FMT_XZ) {
    out->error = "xz format is not implemented (7z only)";
    return -1;
  }
  CrcGenerateTable();

  if (o.volumeSize > 0)
    return z7enc_compress_split(in, &o, dict, threads, out, in->outVolCount);
  return z7enc_compress_single(in, &o, dict, threads, out);
}
