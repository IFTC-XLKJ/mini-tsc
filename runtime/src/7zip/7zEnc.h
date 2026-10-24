/* 7zEnc.h -- 7z archive writer API.
 * Implemented in 7zEnc.c over the vendored LZMA2 / 7zAES encoders. */

#ifndef ZIP7_INC_7Z_ENC_H
#define ZIP7_INC_7Z_ENC_H

#include "7zTypes.h"

EXTERN_C_BEGIN

/* Archive formats the writer supports. */
enum {
  Z7ENC_FMT_7Z = 0,
  Z7ENC_FMT_XZ = 1, /* XZ container is not implemented; only 7z output */
};

/* Compression methods. */
enum {
  Z7ENC_METHOD_LZMA2 = 0,
  Z7ENC_METHOD_COPY  = 1,
  Z7ENC_METHOD_PPMd  = 2, /* PPMdI (PPM8) */
};

typedef struct {
  const char* name;      /* archive-relative path, slash-separated, UTF-8 */
  const char* srcPath;   /* on-disk source path */
  UInt64 size;
  int isDir;
  UInt64 mtime;          /* unix seconds, 0 = not stored */
} Z7EncEntry;

/* Reader callback: fill `buf` (capacity `cap`) with entry `index`'s bytes.
 * Returns the number of bytes written (must equal entry size for files). */
typedef size_t (*Z7EncReadFunc)(void* in, size_t index, Byte* buf, size_t cap, void* ctx);

typedef struct {
  const Z7EncEntry* entries;
  size_t numEntries;
  /* Returns entry `index`'s bytes into `buf` (capacity `cap`); returns the
   * number of bytes written (must equal the entry's size for file entries). */
  Z7EncReadFunc readFile;
  /* Opaque context passed to `readFile`. */
  void* ctx;
  /* Volume 1 output file. Volume split: `out` is volume 1; volumes 2..N
   * are written as `out.001`, `out.002`, ... `outVolCount` (when non-NULL)
   * receives the total number of volume files written. NULL for
   * single-file (no split) archives. */
  const char* archiveOut;
  int* outVolCount;
} Z7EncInput;

typedef struct {
  int format;        /* Z7ENC_FMT_7Z (only); Z7ENC_FMT_XZ unsupported */
  int level;         /* 0..9, default 6 */
  int method;        /* Z7ENC_METHOD_LZMA2 (default) / Z7ENC_METHOD_COPY /
                      * Z7ENC_METHOD_PPMd */
  UInt32 dictSize;   /* bytes, 0 = auto from level (LZMA2 only) */
  UInt32 wordSize;   /* LZMA lc (literal context bits) 0..8; 0 = default (3) */
  int threads;       /* 1..N, 0 = auto (single-thread) */
  UInt64 solidSize;  /* LZMA2 solid-block bytes; 0 = single solid block */
  UInt64 memory;     /* memory budget in bytes; when set below the level's
                      * default dict, the dict is capped to fit (encoder
                      * still allocates what it needs) */
  UInt64 volumeSize; /* bytes per split volume (out.001/.002...), 0 = no split */
  const char* password; /* UTF-8, NULL/empty = no encryption */
  UInt32 ppmdMemSize; /* PPMd8 working-memory budget in bytes (method=PPMd
                      * only); 0 = auto. Higher = better ratio, more RAM. */
} Z7EncOptions;

typedef struct {
  UInt32 filesCount;
  UInt64 totalUnpacked;
  UInt64 totalPacked;
  char* error; /* malloc'd, caller frees via Z7EncResult_Clear */
} Z7EncResult;

void Z7EncOptions_Default(Z7EncOptions* o);

/* Resolve level to dictSize/threads. Fills outDict/outThreads/outLevel. */
int Z7EncOptions_Resolve(const Z7EncOptions* in, UInt32* outDict,
                         unsigned* outThreads, unsigned* outLevel);

int z7enc_compress7z(const Z7EncInput* in, const Z7EncOptions* o, Z7EncResult* out);

void Z7EncResult_Clear(Z7EncResult* r);

EXTERN_C_END

#endif
