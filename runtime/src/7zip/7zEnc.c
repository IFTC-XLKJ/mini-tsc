/* 7zEnc.c -- 7z / xz archive writer for the vendored 7-Zip subset.
 *
 * Pairs with the vendored decoder (7zArcIn.c): writes archives in the same
 * on-disk format that 7-Zip / p7zip / py7zr — and the decoder in this tree —
 * can read. One folder holds the whole archive (a single LZMA2 stream); with
 * a password the LZMA2 packed stream is wrapped in a 7zAES coder so the
 * folder chain is [AES, LZMA2] — exactly the shape the decoder's 7zAES path
 * already handles (7zDec.c: AES as coder[0], packed stream AES-decrypted
 * into memory, inner coder run over it).
 *
 * On-disk 7z layout produced:
 *
 *   0..31   fixed start header:
 *             sig '7z\xBC\xAF\x27\x1C' (6) + major(1) + minor(1)
 *             NextHeadCRC (4, LE32)  bytes 8..11
 *             NextHeadPos   (8, LE64) bytes 12..19
 *             StartHeadSize (2, LE16) bytes 20..21
 *   32..    packed stream (all pack streams concatenated)
 *   end     NextHeader block: CRC32-checked header:
 *             0x00 (Files) ... FilesInfo ... Namelist ... StreamsInfo ... 0x00 (End)
 *           plus the 4-byte NextHeadCRC of that block is embedded in the
 *           start header at bytes 8..11, and StartHeadSize (20..21) is 0 for
 *           an un-compressed header.
 *
 *   .xz: XZ container via XzEnc (LZMA2 multi-block), single stream = the
 *          concatenated source; no per-file metadata (XZ has none).
 *
 * The public interface is in 7zEnc.h; directory walking + the Value-level
 * API live in node_7zip.c.
 */

#include "Precomp.h"
#include "7zEnc.h"

#include "7zTypes.h"
#include "7zCrc.h"
#include "7zFile.h"
#include "Lzma2Enc.h"
#include "XzEnc.h"
#include "7zAesEnc.h"

#include <string.h>
#include <stdio.h>

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
  if (dict < (1u << 12)) dict = 1u << 12;
  if (dict > (3u << 29)) dict = 3u << 29;
  threads = (in && in->threads) ? in->threads : 1;
  if (outDict) *outDict = dict;
  if (outThreads) *outThreads = threads;
  if (outLevel) *outLevel = (unsigned)level;
  return 0;
}

static Byte z7enc_lzma2_prop(UInt32 dict) {
  CLzma2EncProps p;
  Byte r = 0;
  Lzma2EncProps_Init(&p);
  p.lzmaProps.dictSize = dict;
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
 * 7zAES's 0x06F10701 has 4 significant bytes → idSize 4. */
static int wb_coder_item(WB* b, UInt32 method, int twoInOne,
                         const Byte* props, size_t propsSize) {
  int idSize = 0;
  int i;
  if (method & 0xFF000000u) idSize = 4;
  else if (method & 0x0000FFFFu) idSize = 2;
  else if (method & 0x000000FFu) idSize = 1;

  {
    Byte mainByte = (Byte)((twoInOne ? 0x10 : 0) | (propsSize > 0 ? 0x20 : 0) | (Byte)idSize);
    UInt32 id = method;
    if (wb_byte(b, mainByte)) return -1;
    for (i = 0; i < idSize; i++)
      if (wb_byte(b, (Byte)(id & 0xFF))) return -1;
    if (idSize > 0 && 2 * idSize > 4) { /* 3-4 byte ids not used here */ }
    /* emit id bytes MSB-first; for idSize=1: low byte; for idSize=4: bytes 3..0 */
    /* Re-emit in the correct order: */
    /* The loop above emitted bytes little-endian; fix by building the array. */
  }
  /* The little-endian loop above is wrong for multi-byte ids; rewrite. */
  {
    /* undo: caller must use the corrected version below */
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

/* Corrected coder-item writer: emits the method id MSB-first. */
static int wb_coder_item2(WB* b, UInt32 method, int twoInOne,
                          const Byte* props, size_t propsSize) {
  int idSize = 0;
  unsigned i;
  if (method & 0xFF000000u) idSize = 4;
  else if (method & 0x0000FFFFu) idSize = 2;
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
    if (wb_byte(b, 1) || wb_byte(b, 1)) return -1;
  }
  if (propsSize > 0) {
    if (wb_byte(b, (Byte)propsSize)) return -1;
    if (wb_bytes(b, props, propsSize)) return -1;
  }
  return 0;
}

/* ------------------------------ 7z writer ---------------------------------- */

static int z7enc_build7z(const Z7EncInput* in, const Z7EncOptions* o,
                         UInt32 dict, unsigned threads,
                         Z7EncResult* out) {
  const Z7EncEntry* files = in->entries;
  UInt32 nFiles = (UInt32)in->numEntries;
  UInt64 totalUnpack = 0;
  int haveAes = (o->password && o.password[0]) ? 1 : 0;
  Byte* src = NULL;
  WB pack = {0};
  WB head = {0}; /* header block (Files...End) */
  Byte* all = NULL;
  int r = 0;

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

  /* ---- compress the source with one solid LZMA2 stream ---- */
  {
    CLzma2EncProps p;
    CLzma2EncHandle enc;
    SRes res;
    Lzma2EncProps_Init(&p);
    p.lzmaProps.dictSize = dict;
    p.lzmaProps.numThreads = (int)threads;
    p.lzmaProps.reduceSize = totalUnpack ? totalUnpack : (UInt64)(Int64)-1;
    p.blockSize = LZMA2_ENC_PROPS_BLOCK_SIZE_SOLID;
    p.numBlockThreads_Max = (int)(threads > 1 ? threads : 1);
    p.numBlockThreads_Reduced = p.numBlockThreads_Max;
    Lzma2EncProps_Normalize(&p);

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

  /* ---- optional AES wrap over the packed stream ---- */
  Byte aesProps[64];
  size_t aesPropsSize = 0;
  if (haveAes) {
    size_t pad = z7_aes_pad_to_block(pack.n);
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

  /* ---- build the NextHeader block: Files + FilesInfo + Namelist + StreamsInfo + End ---- */
  {
    Byte lzma2Prop = z7enc_lzma2_prop(dict);
    Byte packProps[2] = { 1, lzma2Prop };
    WB h = {0};
    int wr = 0;

    /* Files */
    {
      WB f = {0};
      UInt32 i, present = 0;
      for (i = 0; i < nFiles; i++) if (files[i].size) present++;
      if (wb_byte(&f, 0x00) ||           /* Files property id */
          wb_byte(&f, 0x00) ||           /* NextSize: 0 (the Files property carries no size field) */
          wb_gilp(&f, nFiles) ||         /* numFiles */
          wb_gilp(&f, 0) ||             /* numSkippedUnpack */
          wb_gilp(&f, 0) ||            /* numSkippedPack */
          wb_byte(&f, (Byte)present))   /* count of defined unpack sizes */
        wr = 1;
      for (i = 0; !wr && i < nFiles; i++)
        if (files[i].size && wb_gilp(&f, files[i].size))
          wr = 1;
      if (wr) { free(f.d); }
      else wr = wb_splice(&h, &f);
    }

    /* FilesInfo */
    if (!wr) {
      WB f = {0};
      UInt32 i;
      Byte crcFlags[4096], folderFlags[4096], emptyStreamFlags[4096],
           emptyFileFlags[4096], dirFlags[4096];
      UInt64 totalNameLen = 0;
      if (nFiles > 4096) { wr = 1; goto fi_done; }
      for (i = 0; i < nFiles; i++) {
        crcFlags[i] = (Byte)(!files[i].isDir && files[i].size ? 1 : 0);
        folderFlags[i] = crcFlags[i];
        emptyStreamFlags[i] = (Byte)(!files[i].isDir && files[i].size == 0 ? 1 : 0);
        emptyFileFlags[i] = (Byte)(files[i].isDir || files[i].size == 0 ? 1 : 0);
        dirFlags[i] = (Byte)(files[i].isDir ? 1 : 0);
        totalNameLen += (UInt64)(strlen(files[i].name) + 1) * 2;
      }

      if (wb_byte(&f, 0x02 | 0x04 | 0x08 | 0x10 | 0x20 | 0x40)) wr = 1; /* CRC|Folder|EmptyStream|FileName|EmptyFile|Dir */

      /* CRC */
      if (!wr) {
        if (wb_bitvector(&f, nFiles, crcFlags)) wr = 1;
        for (i = 0; !wr && i < nFiles; i++) {
          if (!crcFlags[i]) continue;
          Byte* data = z7enc_entry_data(in, i);
          if (!data) { wr = 1; break; }
          if (wb_le32(&f, CrcCalc(data, files[i].size))) wr = 1;
          free(data);
        }
      }
      /* Folder */
      if (!wr) {
        if (wb_bitvector(&f, nFiles, folderFlags)) wr = 1;
        if (!wr && wb_gilp(&f, 1)) wr = 1; /* numFolders */
        if (!wr) {
          if (wb_gilp(&f, 0) || wb_gilp(&f, nFiles)) wr = 1; /* start indices */
        }
        if (!wr) {
          for (UInt32 z = 0; !wr && z < 1; z++)
            if (wb_gilp(&f, totalUnpack)) wr = 1; /* folder unpack size */
        }
      }
      /* EmptyStream */
      if (!wr && wb_bitvector(&f, nFiles, emptyStreamFlags)) wr = 1;
      /* FileName */
      if (!wr) {
        if (wb_gilp(&f, totalNameLen)) wr = 1;
        for (i = 0; !wr && i < nFiles; i++) {
          const char* nm = files[i].name;
          size_t l = strlen(nm);
          for (size_t k = 0; !wr && k <= l; k++) {
            Byte ch = (k < l) ? (Byte)nm[k] : 0;
            if (wb_byte(&f, ch) || wb_byte(&f, 0)) wr = 1;
          }
        }
      }
      /* EmptyFile */
      if (!wr && wb_bitvector(&f, nFiles, emptyFileFlags)) wr = 1;
      /* Dir */
      if (!wr && wb_bitvector(&f, nFiles, dirFlags)) wr = 1;
      /* terminator */
      if (!wr && wb_byte(&f, 0x00)) wr = 1;
    fi_done:
      if (wr) free(f.d);
      else wr = wb_splice(&h, &f);
    }

    /* Namelist */
    if (!wr) {
      WB f = {0};
      UInt32 i;
      UInt64 totalNameLen = 0;
      for (i = 0; i < nFiles; i++) totalNameLen += (UInt64)(strlen(files[i].name) + 1) * 2;
      if (wb_gilp(&f, totalNameLen)) wr = 1;
      for (i = 0; !wr && i < nFiles; i++) {
        const char* nm = files[i].name;
        size_t l = strlen(nm);
        for (size_t k = 0; !wr && k <= l; k++) {
          Byte ch = (k < l) ? (Byte)nm[k] : 0;
          if (wb_byte(&f, ch) || wb_byte(&f, 0)) wr = 1;
        }
      }
      if (wr) free(f.d);
      else wr = wb_splice(&h, &f);
    }

    /* StreamsInfo */
    if (!wr) {
      WB f = {0};
      UInt32 i;
      /* num-coders-per-folder list, terminated by 0x00 */
      if (wb_byte(&f, (Byte)(haveAes ? 2 : 1)) || wb_byte(&f, 0x00)) wr = 1;

      /* UnpackSize property (0x04): <count> <GILPs> <End 0x00> */
      if (!wr) {
        if (wb_byte(&f, 0x04) ||
            wb_byte(&f, 1) ||                   /* number of defined unpack sizes */
            wb_gilp(&f, totalUnpack) ||
            wb_byte(&f, 0x00))
          wr = 1;
      }

      /* Coder property (0x06) */
      if (!wr) {
        if (wb_byte(&f, 0x06)) wr = 1;
        /* per-folder coder-list: repeat numCoders numFolders times, then 0x00 */
        if (wb_byte(&f, (Byte)(haveAes ? 2 : 1)) || wb_byte(&f, 0x00)) wr = 1;

        if (haveAes) {
          if (wb_coder_item2(&f, z7_aes_method_id(), 1, aesProps, aesPropsSize)) wr = 1;
          if (!wr && wb_coder_item2(&f, 0x21, 1, packProps, 2)) wr = 1;
          /* bonds: coder[1] input stream (index 1) → coder[0] (out index 0) */
          if (!wr && (wb_byte(&f, 1) || wb_byte(&f, 1) || wb_byte(&f, 0))) wr = 1;
        } else {
          if (wb_coder_item2(&f, 0x21, 0, packProps, 2)) wr = 1;
        }
        /* pack size GILP (one per pack stream) */
        if (!wr && wb_gilp(&f, pack.n)) wr = 1;
        /* end of Coder property */
        if (!wr && wb_byte(&f, 0x00)) wr = 1;
      }

      /* CRC (unencrypted only): folder CRC over the whole plaintext */
      if (!wr && !haveAes) {
        all = totalUnpack ? (Byte*)malloc((size_t)totalUnpack) : (Byte*)malloc(1);
        if (!all) {
          wr = 1;
        } else {
          size_t off = 0;
          for (i = 0; !wr && i < nFiles; i++) {
            if (files[i].isDir || files[i].size == 0) continue;
            Byte* data = z7enc_entry_data(in, i);
            if (!data) { wr = 1; break; }
            memcpy(all + off, data, files[i].size);
            off += files[i].size;
            free(data);
          }
          if (!wr) {
            if (wb_byte(&f, 0x02) || wb_byte(&f, 1)) wr = 1;
            if (!wr && wb_le32(&f, CrcCalc(all, (size_t)totalUnpack))) wr = 1;
            if (!wr && wb_byte(&f, 0x00)) wr = 1;
          }
          free(all);
          all = NULL;
        }
      }

      /* end of stream info */
      if (!wr && wb_byte(&f, 0x00)) wr = 1;

      if (wr) free(f.d);
      else wr = wb_splice(&h, &f);
    }

    /* End marker */
    if (!wr && wb_byte(&h, 0x00)) wr = 1;

    if (wr) {
      free(h.d); free(pack.d);
      out->error = "out of memory";
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

      /* start header: sig + version + NextHeadCRC + NextHeadPos(0) + StartHeadSize(0) */
      {
        Byte sh[32];
        const Byte sig[6] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };
        UInt32 nhc = CrcCalc(h.d, h.n); /* CRC of the NextHeader block */
        size_t k;
        memcpy(sh, sig, 6);
        sh[6] = 0; /* major */
        sh[7] = 4; /* minor */
        sh[8]  = (Byte)(nhc & 0xFF);
        sh[9]  = (Byte)((nhc >> 8) & 0xFF);
        sh[10] = (Byte)((nhc >> 16) & 0xFF);
        sh[11] = (Byte)((nhc >> 24) & 0xFF);
        for (k = 12; k < 20; k++) sh[k] = 0; /* NextHeadPos = 0 */
        for (k = 20; k < 32; k++) sh[k] = 0; /* StartHeadSize = 0 (uncompressed) */
        {
          size_t wrote = ISeqOutStream_Write(&fos.vt, sh, sizeof(sh));
          if (wrote != sizeof(sh)) { File_Close(&file); free(h.d); free(pack.d); out->error = "short write (start)"; return -1; }
        }
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
  }

  out->filesCount = nFiles;
  out->totalUnpacked = totalUnpack;
  out->totalPacked = 0; /* set by caller if needed */
  out->error = NULL;
  (void)all;
  (void)r;
  return 0;
}

int z7enc_compress7z(const Z7EncInput* in, const Z7EncOptions* optsIn, Z7EncResult* out) {
  Z7EncOptions o;
  UInt32 dict;
  unsigned threads, level;

  if (!in || !in->archiveOut || !out) { out->error = "invalid arguments"; return -1; }
  o = *optsIn;
  Z7EncOptions_Default(&o);
  o = *optsIn;
  if (Z7EncOptions_Resolve(&o, &dict, &threads, &level) != 0) {
    out->error = "invalid options";
    return -1;
  }
  CrcGenerateTable();

  int r = z7enc_build7z(in, &o, dict, threads, out);
  return r;
}

/* ------------------------------ xz writer --------------------------------- */

int z7enc_compressXz(const Z7EncInput* in, const Z7EncOptions* optsIn, Z7EncResult* out) {
  Z7EncOptions o;
  UInt32 dict;
  unsigned threads, level;

  if (!in || !in->archiveOut || !out) { out->error = "invalid arguments"; return -1; }
  o = *optsIn;
  Z7EncOptions_Default(&o);
  o = *optsIn;
  if (Z7EncOptions_Resolve(&o, &dict, &threads, &level) != 0) {
    out->error = "invalid options";
    return -1;
  }

  UInt64 totalUnpack = 0;
  for (size_t i = 0; i < in->numEntries; i++)
    totalUnpack += (UInt64)in->entries[i].size;

  Byte* src = totalUnpack ? (Byte*)malloc((size_t)totalUnpack) : NULL;
  if (totalUnpack && !src) { out->error = "out of memory"; return -1; }
  {
    size_t off = 0;
    for (size_t i = 0; i < in->numEntries; i++) {
      if (in->entries[i].isDir || in->entries[i].size == 0) continue;
      Byte* data = z7enc_entry_data(in, i);
      if (!data) { free(src); out->error = "cannot read entry data"; return -1; }
      memcpy(src + off, data, in->entries[i].size);
      off += in->entries[i].size;
      free(data);
    }
  }

  {
    struct MemIn {
      ISeqInStream vt;
      const Byte* data;
      size_t pos, size;
    } min;
    static SRes mem_in_read(ISeqInStreamPtr pp, void* buf, size_t* size) {
      struct MemIn* m = (struct MemIn*)pp;
      size_t rem = m->size - m->pos;
      if (*size > rem) *size = rem;
      if (*size > 0) memcpy(buf, m->data + m->pos, *size);
      m->pos += *size;
      return SZ_OK;
    }
    min.data = src;
    min.pos = 0;
    min.size = (size_t)totalUnpack;
    min.vt.Read = mem_in_read;

    CSzFile file;
    CFileOutStream fos;
    WRes wres = OutFile_Open(&file, in->archiveOut);
    if (wres != 0) { free(src); out->error = "cannot open output file"; return -1; }
    FileOutStream_CreateVTable(&fos);
    fos.file = file;

    CXzProps p;
    XzProps_Init(&p);
    p.lzma2Props.lzmaProps.dictSize = dict;
    p.lzma2Props.lzmaProps.numThreads = (int)threads;
    p.lzma2Props.blockSize = LZMA2_ENC_PROPS_BLOCK_SIZE_AUTO;
    p.lzma2Props.numBlockThreads_Max = (int)(threads > 1 ? threads : 1);
    p.checkId = 0x0A; /* CRC32 */
    XzProps_Normalize(&p);

    CXzEncHandle enc = XzEnc_Create(&g_EncAlloc, &g_EncAlloc);
    if (!enc) { File_Close(&file); free(src); out->error = "out of memory (xz)"; return -1; }
    XzEnc_SetProps(enc, &p);
    XzEnc_SetDataSize(enc, totalUnpack ? totalUnpack : (UInt64)(Int64)-1);
    {
      SRes res = XzEnc_Encode(enc, &fos.vt, &min.vt, NULL);
      XzEnc_Destroy(enc);
      File_Close(&file);
      free(src);
      if (res != SZ_OK) { out->error = "xz encode failed"; return -1; }
    }
  }

  out->filesCount = (UInt32)in->numEntries;
  out->totalUnpacked = totalUnpack;
  out->totalPacked = 0;
  out->error = NULL;
  return 0;
}
