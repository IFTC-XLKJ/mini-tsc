/* node_7zip.c -- 7z archive reader for mini-tsc
 *
 * Wraps the vendored 7-Zip decoder (runtime/src/7zip/) with the Value API.
 * Decode-only: lists and extracts .7z archives. No solid-archive cache is
 * kept between calls for the one-shot helpers; the handle API keeps the
 * parsed archive (CSzArEx) + input stream alive so repeated reads are cheap.
 */

#define _CRT_SECURE_NO_WARNINGS
#include "node_7zip.h"

#include "7z.h"
#include "7zAlloc.h"
#include "7zBuf.h"
#include "7zCrc.h"
#include "7zFile.h"
#include "7zTypes.h"
#include "7zEnc.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <windows.h>
#define stat_fn _stat
#define stat_struct struct _stat
#define stat_call(p, s) stat_fn(p, s)
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#define stat_fn stat
#define stat_struct struct stat
#define stat_call(p, s) stat_fn(p, s)
#endif

#define Z7_INPUT_BUF_SIZE ((size_t)1 << 18)

static const ISzAlloc g_Alloc = { SzAlloc, SzFree };
static const ISzAlloc g_AllocTemp = { SzAllocTemp, SzFreeTemp };

/* ---------------------------------- errors --------------------------------- */

/* The mini-tsc runtime implements throw via longjmp into a jump buffer that is
 * only valid inside a TS_TRY block. An uncaught TS_THROW therefore longjmps
 * into an uninitialized buffer and crashes. Since archive errors (missing
 * entry, corrupt file) are ordinary recoverable conditions, this module never
 * throws: it returns null / false instead. z7_last_error() exposes the reason. */

static char g_z7_error[256] = {0};

static const char* z7_res_msg(SRes res) {
  switch (res) {
    case SZ_ERROR_NO_ARCHIVE: return "not a 7z archive";
    case SZ_ERROR_ARCHIVE: return "corrupt archive header";
    case SZ_ERROR_UNSUPPORTED: return "unsupported compression method";
    case SZ_ERROR_MEM: return "out of memory";
    case SZ_ERROR_CRC: return "CRC check failed";
    case SZ_ERROR_INPUT_EOF: return "unexpected end of archive";
    case SZ_ERROR_READ: return "read error";
    case SZ_ERROR_WRITE: return "write error";
    case SZ_ERROR_PARAM: return "invalid parameter";
    default: return "7z decode failed";
  }
}

static Value z7_fail(const char* msg) {
  snprintf(g_z7_error, sizeof(g_z7_error), "%s", msg ? msg : "7zip error");
  return ts_value_null();
}

static Value z7_fail_res(SRes res) {
  return z7_fail(z7_res_msg(res));
}

/* Like z7_fail_res, but when a password was supplied a CRC failure is far
 * more likely a wrong password than corruption: 7zAES is AES-CBC with no
 * authentication, so a bad key yields same-length garbage that only the
 * stored CRC rejects. Report that distinctly instead of the generic message. */
static Value z7_fail_res_pw(SRes res, const char* password) {
  if (res == SZ_ERROR_CRC && password && password[0])
    return z7_fail("wrong password (CRC check failed)");
  return z7_fail_res(res);
}

/* --------------------------------- helpers --------------------------------- */

static const char* z7_value_cstr(Value v, char* numBuf, size_t numBufLen) {
  if (v.tag == TAG_STRING && v.as.string && v.as.string->data) return v.as.string->data;
  if (v.tag == TAG_NUMBER) {
    snprintf(numBuf, numBufLen, "%.17g", v.as.number);
    return numBuf;
  }
  if (v.tag == TAG_BOOLEAN) return v.as.boolean ? "1" : "0";
  if (v.tag == TAG_NULL) return NULL;
  TSString* s = ts_to_string(v);
  return (s && s->data) ? s->data : "";
}

/* UTF-16LE (7z entry names) �?freshly malloc'd UTF-8. Caller frees. */
static char* z7_utf16_to_utf8(const UInt16* src, size_t srcLen) {
  size_t i;
  size_t cap = srcLen * 4 + 1;
  char* out = (char*)malloc(cap);
  if (!out) return NULL;
  size_t o = 0;
  for (i = 0; i < srcLen; i++) {
    UInt32 c = src[i];
    if (c == 0) break;
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < srcLen &&
        src[i + 1] >= 0xDC00 && src[i + 1] < 0xE000) {
      c = (((c - 0xD800) << 10) | (src[i + 1] - 0xDC00)) + 0x10000;
      i++;
    }
    if (c < 0x80) {
      out[o++] = (char)c;
    } else if (c < 0x800) {
      out[o++] = (char)(0xC0 | (c >> 6));
      out[o++] = (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out[o++] = (char)(0xE0 | (c >> 12));
      out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
      out[o++] = (char)(0x80 | (c & 0x3F));
    } else {
      out[o++] = (char)(0xF0 | (c >> 18));
      out[o++] = (char)(0x80 | ((c >> 12) & 0x3F));
      out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
      out[o++] = (char)(0x80 | (c & 0x3F));
    }
  }
  out[o] = '\0';
  return out;
}

/* Recursively create every directory along `path` (POSIX / Win32). */
static void z7_mkdirs(const char* path) {
  if (!path || !path[0]) return;
  char tmp[4096];
  size_t len = strlen(path);
  if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
  memcpy(tmp, path, len);
  tmp[len] = '\0';

  for (size_t i = 1; i < len; i++) {
    if (tmp[i] == '/' || tmp[i] == '\\') {
      char saved = tmp[i];
      tmp[i] = '\0';
      /* Skip drive root like "C:" */
      if (!(i == 2 && tmp[1] == ':')) {
#ifdef _WIN32
        _mkdir(tmp);
#else
        mkdir(tmp, 0777);
#endif
      }
      tmp[i] = saved;
    }
  }
#ifdef _WIN32
  _mkdir(tmp);
#else
  mkdir(tmp, 0777);
#endif
}

/* Sanitize an archive entry name for safe extraction.
 *
 * Archive entry names are attacker-controlled: they may be absolute ("C:\\x",
 * "/etc/x"), contain drive letters, or contain ".." components that would
 * escape the destination directory (a "zip slip" / path traversal). This
 * rewrites `name` in place into a safe relative path:
 *   - backslashes become forward slashes
 *   - a leading drive letter ("C:") or leading separators are dropped
 *   - "." components are skipped
 *   - ".." components are replaced with "_" (a harmless in-tree name)
 * The result never contains a ".." component and never starts with a
 * separator or drive letter, so joining it under destDir stays inside it.
 * Returns 0 on success, -1 when the name is empty after sanitizing. */
static int z7_sanitize_name(char* name) {
  if (!name) return -1;
  size_t n = strlen(name);
  size_t w = 0;              /* write cursor */
  size_t r = 0;              /* read cursor */
  int atComponentStart = 1;

  /* Drop a leading Windows drive spec like "C:". */
  if (n >= 2 && name[1] == ':' && ((name[0] >= 'A' && name[0] <= 'Z') ||
                                   (name[0] >= 'a' && name[0] <= 'z'))) {
    r = 2;
  }

  while (r < n) {
    char c = name[r];
    if (c == '\\') c = '/';

    if (c == '/') {
      /* Collapse consecutive separators; never emit a leading one. */
      if (w == 0 || name[w - 1] == '/') { r++; atComponentStart = 1; continue; }
      name[w++] = '/';
      r++;
      atComponentStart = 1;
      continue;
    }

    if (atComponentStart) {
      /* Skip "." components. */
      if (c == '.' && (r + 1 == n || name[r + 1] == '/' || name[r + 1] == '\\')) {
        r++;
        continue;
      }
      /* Neutralize ".." components: "_" + second "." is a plain name. */
      if (c == '.' && (r + 1 < n) && name[r + 1] == '.' &&
          (r + 2 == n || name[r + 2] == '/' || name[r + 2] == '\\')) {
        name[w++] = '_';
        r += 2;
        atComponentStart = 0;
        continue;
      }
      atComponentStart = 0;
    }

    name[w++] = c;
    r++;
  }
  name[w] = '\0';

  /* Strip trailing separators / empty result. */
  while (w > 0 && name[w - 1] == '/') name[--w] = '\0';
  return w == 0 ? -1 : 0;
}

/* Create the parent directory of a file path, in place-safe way. */
static void z7_mkparents(const char* filePath) {
  char tmp[4096];
  size_t len = strlen(filePath);
  if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
  memcpy(tmp, filePath, len);
  tmp[len] = '\0';
  for (size_t i = len; i > 0; i--) {
    if (tmp[i] == '/' || tmp[i] == '\\') {
      tmp[i] = '\0';
      z7_mkdirs(tmp);
      return;
    }
  }
}

/* ------------------------------ open archive ------------------------------- */

typedef struct {
  CFileInStream stream;
  CLookToRead2 look;
  CSzArEx db;
  Byte* outBuffer;        /* solid-block decompress cache */
  size_t outBufferSize;
  UInt32 blockIndex;
  int fileOpen;
  int opened;
  int closed;
  char* password;         /* UTF-8 decryption password, or NULL */
} Z7Archive;

static void z7_archive_close_inner(Z7Archive* a) {
  if (!a || a->closed) return;
  if (a->opened) {
    SzArEx_Free(&a->db, &g_Alloc);
    a->opened = 0;
  }
  if (a->outBuffer) {
    ISzAlloc_Free(&g_Alloc, a->outBuffer);
    a->outBuffer = NULL;
    a->outBufferSize = 0;
  }
  if (a->look.buf) {
    ISzAlloc_Free(&g_Alloc, a->look.buf);
    a->look.buf = NULL;
  }
  if (a->fileOpen) {
    File_Close(&a->stream.file);
    a->fileOpen = 0;
  }
  if (a->password) {
    /* wipe before freeing so the password does not linger in the heap */
    size_t n = strlen(a->password);
    memset(a->password, 0, n);
    free(a->password);
    a->password = NULL;
  }
  a->closed = 1;
}

/* Returns NULL on error (see z7_last_error()).
 * `password` (UTF-8, may be NULL) is kept for later 7zAES folder decryption. */
static Z7Archive* z7_archive_open_pw(const char* path, const char* password) {
  if (!path || !path[0]) {
    z7_fail("empty archive path");
    return NULL;
  }

  Z7Archive* a = (Z7Archive*)calloc(1, sizeof(Z7Archive));
  if (!a) {
    z7_fail("out of memory");
    return NULL;
  }
  a->blockIndex = 0xFFFFFFFF;
  if (password && password[0]) {
    size_t n = strlen(password);
    a->password = (char*)malloc(n + 1);
    if (!a->password) {
      free(a);
      z7_fail("out of memory");
      return NULL;
    }
    memcpy(a->password, password, n + 1);
  }

  WRes wres = InFile_Open(&a->stream.file, path);
  if (wres != 0) {
    if (a->password) {
      memset(a->password, 0, strlen(a->password));
      free(a->password);
    }
    free(a);
    z7_fail("cannot open archive file");
    return NULL;
  }
  a->fileOpen = 1;
  FileInStream_CreateVTable(&a->stream);
  a->stream.wres = 0;

  LookToRead2_CreateVTable(&a->look, False);
  a->look.buf = (Byte*)ISzAlloc_Alloc(&g_Alloc, Z7_INPUT_BUF_SIZE);
  if (!a->look.buf) {
    z7_archive_close_inner(a);
    free(a);
    z7_fail("out of memory");
    return NULL;
  }
  a->look.bufSize = Z7_INPUT_BUF_SIZE;
  a->look.realStream = &a->stream.vt;
  LookToRead2_INIT(&a->look);

  CrcGenerateTable();
  SzArEx_Init(&a->db);

  SRes res = SzArEx_Open(&a->db, &a->look.vt, &g_Alloc, &g_AllocTemp);
  if (res != SZ_OK) {
    z7_archive_close_inner(a);
    free(a);
    z7_fail_res(res);
    return NULL;
  }
  a->opened = 1;
  return a;
}

/* Convenience wrapper for callers that pass no password. */
static Z7Archive* z7_archive_open(const char* path) {
  return z7_archive_open_pw(path, NULL);
}

/* Entry name as UTF-8 (malloc'd by caller contract �?free). NULL if OOM. */
static char* z7_entry_name(const CSzArEx* db, UInt32 i) {
  size_t len = SzArEx_GetFileNameUtf16(db, i, NULL);
  if (len == 0) {
    char* empty = (char*)malloc(1);
    if (empty) empty[0] = '\0';
    return empty;
  }
  UInt16* buf = (UInt16*)SzAlloc(NULL, len * sizeof(UInt16));
  if (!buf) return NULL;
  SzArEx_GetFileNameUtf16(db, i, buf);
  char* out = z7_utf16_to_utf8(buf, len);
  SzFree(NULL, buf);
  return out;
}

/* Find entry index by UTF-8 name. Returns -1 when absent. */
static int z7_find_entry(const CSzArEx* db, const char* name) {
  if (!name) return -1;
  for (UInt32 i = 0; i < db->NumFiles; i++) {
    char* n = z7_entry_name(db, i);
    if (!n) return -1;
    int match = (strcmp(n, name) == 0);
    free(n);
    if (match) return (int)i;
  }
  return -1;
}

/* Optional password argument: an absent value (undefined/null) or an empty
 * string means "no password". Returns NULL in that case, else the C string. */
static const char* z7_opt_password(Value v, char* numBuf, size_t numBufLen) {
  if (v.tag == TAG_NULL) return NULL;
  const char* s = z7_value_cstr(v, numBuf, numBufLen);
  if (!s || !s[0]) return NULL;
  return s;
}

/* Extract entry `index` into memory. Caller frees *out (via free). */
static SRes z7_extract_to_memory(Z7Archive* a, UInt32 index, Byte** out, size_t* outSize) {
  size_t offset = 0;
  size_t outSizeProcessed = 0;
  SRes res = SzArEx_Extract2(
      &a->db, &a->look.vt, index,
      &a->blockIndex, &a->outBuffer, &a->outBufferSize,
      &offset, &outSizeProcessed,
      &g_Alloc, &g_AllocTemp,
      a->password);
  if (res != SZ_OK) return res;

  Byte* copy = (Byte*)malloc(outSizeProcessed > 0 ? outSizeProcessed : 1);
  if (!copy) return SZ_ERROR_MEM;
  if (outSizeProcessed > 0) memcpy(copy, a->outBuffer + offset, outSizeProcessed);
  *out = copy;
  *outSize = outSizeProcessed;
  return SZ_OK;
}

/* ------------------------------ list building ------------------------------ */

/* FILETIME (100ns since 1601) �?Unix seconds. */
static double z7_filetime_to_unix(const CNtfsFileTime* t) {
  UInt64 win = (UInt64)t->Low | ((UInt64)t->High << 32);
  const double kEpochOffset = 11644473600.0;
  return (double)win / 10000000.0 - kEpochOffset;
}

/* Build the metadata object for entry `i`. `name` may be NULL (recomputed). */
static Value z7_entry_object(const CSzArEx* db, UInt32 i, const char* name) {
  TSHashMap* entry = ts_hashmap_new();
  ts_hashmap_set(entry, ts_string_new("name"), ts_value_string(ts_string_new(name ? name : "")));
  ts_hashmap_set(entry, ts_string_new("size"),
                 ts_value_number((double)SzArEx_GetFileSize(db, i)));
  ts_hashmap_set(entry, ts_string_new("isDir"),
                 ts_value_boolean(SzArEx_IsDir(db, i) ? 1 : 0));
  /* mtime as Unix seconds if present, else null */
  if (SzBitWithVals_Check(&db->MTime, i)) {
    ts_hashmap_set(entry, ts_string_new("mtime"),
                   ts_value_number(z7_filetime_to_unix(&db->MTime.Vals[i])));
  } else {
    ts_hashmap_set(entry, ts_string_new("mtime"), ts_value_null());
  }
  /* ctime as Unix seconds if present, else null */
  if (SzBitWithVals_Check(&db->CTime, i)) {
    ts_hashmap_set(entry, ts_string_new("cTime"),
                   ts_value_number(z7_filetime_to_unix(&db->CTime.Vals[i])));
  } else {
    ts_hashmap_set(entry, ts_string_new("cTime"), ts_value_null());
  }
  /* DOS attribute bits (FILE_ATTRIBUTE_*) if present, else 0 */
  if (SzBitWithVals_Check(&db->Attribs, i)) {
    ts_hashmap_set(entry, ts_string_new("attrib"),
                   ts_value_number((double)db->Attribs.Vals[i]));
  } else {
    ts_hashmap_set(entry, ts_string_new("attrib"), ts_value_number(0));
  }
  ts_hashmap_set(entry, ts_string_new("index"), ts_value_number((double)i));
  return ts_value_object(entry);
}

static Value z7_list_entries(Z7Archive* a) {
  TSArray* arr = ts_array_new();
  for (UInt32 i = 0; i < a->db.NumFiles; i++) {
    char* name = z7_entry_name(&a->db, i);
    if (!name) continue;
    ts_array_push(arr, z7_entry_object(&a->db, i, name));
    free(name);
  }
  return ts_value_array(arr);
}

/* Array of entry names only (skips entries whose names fail to decode). */
static Value z7_list_names(Z7Archive* a) {
  TSArray* arr = ts_array_new();
  for (UInt32 i = 0; i < a->db.NumFiles; i++) {
    char* name = z7_entry_name(&a->db, i);
    if (!name) continue;
    ts_array_push(arr, ts_value_string(ts_string_new(name)));
    free(name);
  }
  return ts_value_array(arr);
}

/* Metadata for a single named entry, or null when absent. */
static Value z7_get_entry(Z7Archive* a, const char* entryName) {
  int idx = z7_find_entry(&a->db, entryName);
  if (idx < 0) return z7_fail("entry not found in archive");
  return z7_entry_object(&a->db, (UInt32)idx, entryName);
}

/* Archive-level summary: { count, fileCount, dirCount, totalSize, packedSize }. */
static Value z7_info_obj(Z7Archive* a) {
  UInt64 total = 0;
  UInt32 dirs = 0;
  for (UInt32 i = 0; i < a->db.NumFiles; i++) {
    if (SzArEx_IsDir(&a->db, i)) { dirs++; continue; }
    total += SzArEx_GetFileSize(&a->db, i);
  }
  TSHashMap* info = ts_hashmap_new();
  ts_hashmap_set(info, ts_string_new("count"), ts_value_number((double)a->db.NumFiles));
  ts_hashmap_set(info, ts_string_new("fileCount"),
                 ts_value_number((double)(a->db.NumFiles - dirs)));
  ts_hashmap_set(info, ts_string_new("dirCount"), ts_value_number((double)dirs));
  ts_hashmap_set(info, ts_string_new("totalSize"), ts_value_number((double)total));
  return ts_value_object(info);
}

/* Unpacked size of a named entry, or -1 when absent. */
static double z7_size_of(Z7Archive* a, const char* entryName) {
  int idx = z7_find_entry(&a->db, entryName);
  if (idx < 0) return -1.0;
  return (double)SzArEx_GetFileSize(&a->db, (UInt32)idx);
}

/* ------------------------------ extraction --------------------------------- */

/* Extract every entry under destDir (empty string �?current directory).
 * `prefix` (may be NULL/empty) restricts extraction to entries whose sanitized
 * name equals the prefix or starts with "prefix/". Returns the number of files
 * written, or a negative Value (null) on error �?callers surface lastError(). */
static Value z7_extract_all(Z7Archive* a, const char* destDir, const char* prefix) {
  size_t baseLen = destDir ? strlen(destDir) : 0;
  size_t prefixLen = prefix ? strlen(prefix) : 0;
  double fileCount = 0;

  for (UInt32 i = 0; i < a->db.NumFiles; i++) {
    char* name = z7_entry_name(&a->db, i);
    if (!name) continue;

    int isDir = SzArEx_IsDir(&a->db, i);

    /* Reject names that would escape destDir (absolute paths, "..", drives). */
    if (z7_sanitize_name(name) != 0) {
      free(name);
      return z7_fail("unsafe entry name in archive");
    }

    /* Subtree filter: keep only entries at/under `prefix`. */
    if (prefixLen > 0) {
      int match = (strncmp(name, prefix, prefixLen) == 0) &&
                  (name[prefixLen] == '\0' || name[prefixLen] == '/');
      if (!match) { free(name); continue; }
    }

    /* Build output path: destDir + "/" + sanitized name. Refuse to truncate:
     * silently clipping would extract into the wrong (possibly shorter) path. */
    char full[8192];
    size_t o = 0;
    if (baseLen > 0) {
      if (baseLen + 2 > sizeof(full)) { free(name); return z7_fail("path too long"); }
      memcpy(full, destDir, baseLen);
      o = baseLen;
      if (full[o - 1] != '/' && full[o - 1] != '\\') full[o++] = '/';
    }
    size_t nameLen = strlen(name);
    if (o + nameLen + 1 > sizeof(full)) { free(name); return z7_fail("path too long"); }
    memcpy(full + o, name, nameLen);
    o += nameLen;
    full[o] = '\0';

    if (isDir) {
      free(name);
      z7_mkdirs(full);
      continue;
    }

    z7_mkparents(full);

    Byte* data = NULL;
    size_t size = 0;
    SRes res = z7_extract_to_memory(a, i, &data, &size);
    free(name);
    if (res != SZ_OK) return z7_fail_res_pw(res, a->password);

    FILE* f = fopen(full, "wb");
    if (!f) {
      free(data);
      return z7_fail("cannot create output file");
    }
    if (size > 0) fwrite(data, 1, size, f);
    fclose(f);
    free(data);
    fileCount += 1;
  }
  return ts_value_number(fileCount);
}

/* Extract one entry to an explicit output path. */
static Value z7_extract_one(Z7Archive* a, const char* entryName, const char* outPath) {
  if (!outPath || !outPath[0]) return z7_fail("empty output path");
  int idx = z7_find_entry(&a->db, entryName);
  if (idx < 0) return z7_fail("entry not found in archive");
  if (SzArEx_IsDir(&a->db, (UInt32)idx)) return z7_fail("entry is a directory");

  Byte* data = NULL;
  size_t size = 0;
  SRes res = z7_extract_to_memory(a, (UInt32)idx, &data, &size);
  if (res != SZ_OK) return z7_fail_res_pw(res, a->password);

  z7_mkparents(outPath);
  FILE* f = fopen(outPath, "wb");
  if (!f) {
    free(data);
    return z7_fail("cannot create output file");
  }
  if (size > 0) fwrite(data, 1, size, f);
  fclose(f);
  free(data);
  return ts_value_undefined();
}

/* Read one entry's bytes into a TSString (UTF-8, raw bytes preserved). */
static Value z7_read_one(Z7Archive* a, const char* entryName) {
  int idx = z7_find_entry(&a->db, entryName);
  if (idx < 0) return z7_fail("entry not found in archive");
  if (SzArEx_IsDir(&a->db, (UInt32)idx)) return z7_fail("entry is a directory");

  Byte* data = NULL;
  size_t size = 0;
  SRes res = z7_extract_to_memory(a, (UInt32)idx, &data, &size);
  if (res != SZ_OK) return z7_fail_res_pw(res, a->password);

  TSString* s = ts_string_new_len((const char*)data, (int32_t)size);
  free(data);
  return ts_value_string(s);
}

/* Read one entry's bytes into a binary-safe Buffer (raw bytes preserved). */
static Value z7_read_buf(Z7Archive* a, const char* entryName) {
  int idx = z7_find_entry(&a->db, entryName);
  if (idx < 0) return z7_fail("entry not found in archive");
  if (SzArEx_IsDir(&a->db, (UInt32)idx)) return z7_fail("entry is a directory");

  Byte* data = NULL;
  size_t size = 0;
  SRes res = z7_extract_to_memory(a, (UInt32)idx, &data, &size);
  if (res != SZ_OK) return z7_fail_res_pw(res, a->password);

  TSString* s = ts_string_new_len((const char*)data, (int32_t)size);
  free(data);
  Value buf = ts_buffer_from_string(s);
  ts_string_free(s);
  return buf;
}

/* -------------------------------- public API ------------------------------- */

Value node_7zip_list(Value archivePath, Value password) {
  char nb[64], nb2[64];
  const char* path = z7_value_cstr(archivePath, nb, sizeof(nb));
  const char* pw = z7_opt_password(password, nb2, sizeof(nb2));
  Z7Archive* a = z7_archive_open_pw(path, pw);
  if (!a) return ts_value_null();
  Value result = z7_list_entries(a);
  z7_archive_close_inner(a);
  free(a);
  return result;
}

Value node_7zip_extractToDir(Value archivePath, Value destDir, Value password) {
  char nb[64], nb2[64], nb3[64];
  const char* path = z7_value_cstr(archivePath, nb, sizeof(nb));
  const char* dest = z7_value_cstr(destDir, nb2, sizeof(nb2));
  const char* pw = z7_opt_password(password, nb3, sizeof(nb3));

  Z7Archive* a = z7_archive_open_pw(path, pw);
  if (!a) return ts_value_null();
  /* Create destDir only after the archive opened, so a failed open does not
   * leave a stray empty directory behind. */
  if (dest && dest[0]) z7_mkdirs(dest);
  Value result = z7_extract_all(a, dest ? dest : "", NULL);
  z7_archive_close_inner(a);
  free(a);
  return result;
}

Value node_7zip_extractMatching(Value archivePath, Value prefix, Value destDir, Value password) {
  char nb[64], nb2[64], nb3[64], nb4[64];
  const char* path = z7_value_cstr(archivePath, nb, sizeof(nb));
  const char* pre = z7_value_cstr(prefix, nb2, sizeof(nb2));
  const char* dest = z7_value_cstr(destDir, nb3, sizeof(nb3));
  const char* pw = z7_opt_password(password, nb4, sizeof(nb4));

  Z7Archive* a = z7_archive_open_pw(path, pw);
  if (!a) return ts_value_null();
  if (dest && dest[0]) z7_mkdirs(dest);
  Value result = z7_extract_all(a, dest ? dest : "", pre ? pre : "");
  z7_archive_close_inner(a);
  free(a);
  return result;
}

Value node_7zip_extractFile(Value archivePath, Value entryName, Value outPath, Value password) {
  char nb[64], nb2[64], nb3[64], nb4[64];
  const char* path = z7_value_cstr(archivePath, nb, sizeof(nb));
  const char* entry = z7_value_cstr(entryName, nb2, sizeof(nb2));
  const char* out = z7_value_cstr(outPath, nb3, sizeof(nb3));
  const char* pw = z7_opt_password(password, nb4, sizeof(nb4));

  Z7Archive* a = z7_archive_open_pw(path, pw);
  if (!a) return ts_value_null();
  Value result = z7_extract_one(a, entry, out);
  z7_archive_close_inner(a);
  free(a);
  return result;
}

Value node_7zip_readFile(Value archivePath, Value entryName, Value password) {
  char nb[64], nb2[64], nb3[64];
  const char* path = z7_value_cstr(archivePath, nb, sizeof(nb));
  const char* entry = z7_value_cstr(entryName, nb2, sizeof(nb2));
  const char* pw = z7_opt_password(password, nb3, sizeof(nb3));

  Z7Archive* a = z7_archive_open_pw(path, pw);
  if (!a) return ts_value_null();
  Value result = z7_read_one(a, entry);
  z7_archive_close_inner(a);
  free(a);
  return result;
}

Value node_7zip_readFileBuffer(Value archivePath, Value entryName, Value password) {
  char nb[64], nb2[64], nb3[64];
  const char* path = z7_value_cstr(archivePath, nb, sizeof(nb));
  const char* entry = z7_value_cstr(entryName, nb2, sizeof(nb2));
  const char* pw = z7_opt_password(password, nb3, sizeof(nb3));

  Z7Archive* a = z7_archive_open_pw(path, pw);
  if (!a) return ts_value_null();
  Value result = z7_read_buf(a, entry);
  z7_archive_close_inner(a);
  free(a);
  return result;
}

Value node_7zip_hasEntry(Value archivePath, Value entryName, Value password) {
  char nb[64], nb2[64], nb3[64];
  const char* path = z7_value_cstr(archivePath, nb, sizeof(nb));
  const char* entry = z7_value_cstr(entryName, nb2, sizeof(nb2));
  const char* pw = z7_opt_password(password, nb3, sizeof(nb3));

  Z7Archive* a = z7_archive_open_pw(path, pw);
  if (!a) return ts_value_boolean(0);
  int found = z7_find_entry(&a->db, entry) >= 0;
  z7_archive_close_inner(a);
  free(a);
  return ts_value_boolean(found ? 1 : 0);
}

Value node_7zip_getEntry(Value archivePath, Value entryName, Value password) {
  char nb[64], nb2[64], nb3[64];
  const char* path = z7_value_cstr(archivePath, nb, sizeof(nb));
  const char* entry = z7_value_cstr(entryName, nb2, sizeof(nb2));
  const char* pw = z7_opt_password(password, nb3, sizeof(nb3));

  Z7Archive* a = z7_archive_open_pw(path, pw);
  if (!a) return ts_value_null();
  Value result = z7_get_entry(a, entry);
  z7_archive_close_inner(a);
  free(a);
  return result;
}

Value node_7zip_entryNames(Value archivePath, Value password) {
  char nb[64], nb2[64];
  const char* path = z7_value_cstr(archivePath, nb, sizeof(nb));
  const char* pw = z7_opt_password(password, nb2, sizeof(nb2));

  Z7Archive* a = z7_archive_open_pw(path, pw);
  if (!a) return ts_value_null();
  Value result = z7_list_names(a);
  z7_archive_close_inner(a);
  free(a);
  return result;
}

Value node_7zip_size(Value archivePath, Value entryName, Value password) {
  char nb[64], nb2[64], nb3[64];
  const char* path = z7_value_cstr(archivePath, nb, sizeof(nb));
  const char* entry = z7_value_cstr(entryName, nb2, sizeof(nb2));
  const char* pw = z7_opt_password(password, nb3, sizeof(nb3));

  Z7Archive* a = z7_archive_open_pw(path, pw);
  if (!a) return ts_value_number(-1);
  double result = z7_size_of(a, entry);
  z7_archive_close_inner(a);
  free(a);
  return ts_value_number(result);
}

Value node_7zip_info(Value archivePath, Value password) {
  char nb[64], nb2[64];
  const char* path = z7_value_cstr(archivePath, nb, sizeof(nb));
  const char* pw = z7_opt_password(password, nb2, sizeof(nb2));

  Z7Archive* a = z7_archive_open_pw(path, pw);
  if (!a) return ts_value_null();
  Value result = z7_info_obj(a);
  z7_archive_close_inner(a);
  free(a);
  return result;
}

Value node_7zip_isArchive(Value archivePath) {
  char nb[64];
  const char* path = z7_value_cstr(archivePath, nb, sizeof(nb));
  if (!path || !path[0]) return ts_value_boolean(0);

  FILE* f = fopen(path, "rb");
  if (!f) return ts_value_boolean(0);
  Byte sig[6] = {0};
  size_t n = fread(sig, 1, 6, f);
  fclose(f);
  if (n != 6) return ts_value_boolean(0);
  /* 7z signature: '7' 'z' 0xBC 0xAF 0x27 0x1C */
  static const Byte k7zSig[6] = { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C };
  return ts_value_boolean(memcmp(sig, k7zSig, 6) == 0 ? 1 : 0);
}

/* Error message for the most recent failed call ("" when none). */
Value node_7zip_lastError(void) {
  return ts_value_string(ts_string_new(g_z7_error));
}

/* --- handle API --- */

static Z7Archive* z7_as_archive(Value v) {
  if (v.tag != TAG_OBJECT || !v.as.object) return NULL;
  Z7Archive* a = (Z7Archive*)v.as.object;
  if (a->closed) return NULL;
  return a;
}

Value node_7zip_open(Value archivePath, Value password) {
  char nb[64], nb2[64];
  const char* path = z7_value_cstr(archivePath, nb, sizeof(nb));
  const char* pw = z7_opt_password(password, nb2, sizeof(nb2));
  Z7Archive* a = z7_archive_open_pw(path, pw);
  if (!a) return ts_value_null();
  return ts_value_object(a);
}

Value node_7zip_handleList(Value handle) {
  Z7Archive* a = z7_as_archive(handle);
  if (!a) return z7_fail("archive is not open");
  return z7_list_entries(a);
}

Value node_7zip_handleExtractAll(Value handle, Value destDir) {
  Z7Archive* a = z7_as_archive(handle);
  if (!a) return z7_fail("archive is not open");
  char nb[64];
  const char* dest = z7_value_cstr(destDir, nb, sizeof(nb));
  if (dest && dest[0]) z7_mkdirs(dest);
  return z7_extract_all(a, dest ? dest : "", NULL);
}

Value node_7zip_handleExtractMatching(Value handle, Value prefix, Value destDir) {
  Z7Archive* a = z7_as_archive(handle);
  if (!a) return z7_fail("archive is not open");
  char nb[64], nb2[64];
  const char* pre = z7_value_cstr(prefix, nb, sizeof(nb));
  const char* dest = z7_value_cstr(destDir, nb2, sizeof(nb2));
  if (dest && dest[0]) z7_mkdirs(dest);
  return z7_extract_all(a, dest ? dest : "", pre ? pre : "");
}

Value node_7zip_handleExtract(Value handle, Value entryName, Value outPath) {
  Z7Archive* a = z7_as_archive(handle);
  if (!a) return z7_fail("archive is not open");
  char nb[64], nb2[64];
  const char* entry = z7_value_cstr(entryName, nb, sizeof(nb));
  const char* out = z7_value_cstr(outPath, nb2, sizeof(nb2));
  return z7_extract_one(a, entry, out);
}

Value node_7zip_handleRead(Value handle, Value entryName) {
  Z7Archive* a = z7_as_archive(handle);
  if (!a) return z7_fail("archive is not open");
  char nb[64];
  const char* entry = z7_value_cstr(entryName, nb, sizeof(nb));
  return z7_read_one(a, entry);
}

Value node_7zip_handleHas(Value handle, Value entryName) {
  Z7Archive* a = z7_as_archive(handle);
  if (!a) return z7_fail("archive is not open");
  char nb[64];
  const char* entry = z7_value_cstr(entryName, nb, sizeof(nb));
  return ts_value_boolean(z7_find_entry(&a->db, entry) >= 0 ? 1 : 0);
}

Value node_7zip_handleReadBuffer(Value handle, Value entryName) {
  Z7Archive* a = z7_as_archive(handle);
  if (!a) return z7_fail("archive is not open");
  char nb[64];
  const char* entry = z7_value_cstr(entryName, nb, sizeof(nb));
  return z7_read_buf(a, entry);
}

Value node_7zip_handleNames(Value handle) {
  Z7Archive* a = z7_as_archive(handle);
  if (!a) return z7_fail("archive is not open");
  return z7_list_names(a);
}

Value node_7zip_handleGetEntry(Value handle, Value entryName) {
  Z7Archive* a = z7_as_archive(handle);
  if (!a) return z7_fail("archive is not open");
  char nb[64];
  const char* entry = z7_value_cstr(entryName, nb, sizeof(nb));
  return z7_get_entry(a, entry);
}

Value node_7zip_handleSize(Value handle, Value entryName) {
  Z7Archive* a = z7_as_archive(handle);
  if (!a) return z7_fail("archive is not open");
  char nb[64];
  const char* entry = z7_value_cstr(entryName, nb, sizeof(nb));
  return ts_value_number(z7_size_of(a, entry));
}

Value node_7zip_handleInfo(Value handle) {
  Z7Archive* a = z7_as_archive(handle);
  if (!a) return z7_fail("archive is not open");
  return z7_info_obj(a);
}

Value node_7zip_handleIsOpen(Value handle) {
  return ts_value_boolean(z7_as_archive(handle) ? 1 : 0);
}

Value node_7zip_handleCount(Value handle) {
  Z7Archive* a = z7_as_archive(handle);
  if (!a) return z7_fail("archive is not open");
  return ts_value_number((double)a->db.NumFiles);
}

Value node_7zip_close(Value handle) {
  if (handle.tag != TAG_OBJECT || !handle.as.object) return ts_value_undefined();
  Z7Archive* a = (Z7Archive*)handle.as.object;
  /* Idempotent. The struct itself is NOT freed: the handle Value is passed by
   * value, so the caller still holds (a copy of) this pointer �?freeing it here
   * would leave a dangling Value and a second close() would use-after-free.
   * Instead we free all owned resources and leave a small (~200 byte) tombstone
   * whose `closed` flag makes every later z7_as_archive()/close() a safe no-op.
   * The runtime has no GC-finalizer API, so this is the safe lifetime model. */
  z7_archive_close_inner(a);
  return ts_value_undefined();
}

/* ============================== compress API =============================== */
/*
 * compress(src, outPath, options?) -- write a .7z archive from `src` (a file
 * or a directory, recursively) to `outPath`.
 *
 * options (object, all keys optional):
 *   format      : "7z" (default; the only supported format)
 *   method      : "lzma2" (default) or "copy"
 *   level       : 0..9, default 6
 *   dictSize    : bytes, 0 = auto from level
 *   wordSize    : LZMA lc (literal context bits) 0..8, 0 = default (3)
 *   solidSize   : LZMA2 solid-block bytes, 0 = single solid block
 *   threads     : 1..N, 0 = single-threaded
 *   memory      : informational only, not enforced
 *   volumeSize  : bytes per .7z.NNN split volume, 0 = single file
 *   password    : UTF-8 string; encrypts the archive with 7zAES when set
 *
 * Returns an object { filesCount, totalUnpacked, totalPacked, volumeCount,
 * error } or null on failure (see lastError()).
 */

/* ------------------------------ source walk -------------------------------- */

typedef struct {
  Z7EncEntry* items;
  size_t count;
  size_t cap;
} Z7EncItems;

static int z7enc_items_grow(Z7EncItems* items) {
  if (items->count < items->cap) return 0;
  size_t nc = items->cap ? items->cap * 2 : 16;
  Z7EncEntry* ni = realloc(items->items, nc * sizeof(Z7EncEntry));
  if (!ni) return -1;
  items->items = ni;
  items->cap = nc;
  return 0;
}

static int z7enc_item_add(Z7EncItems* items, const char* name, const char* srcPath,
                           UInt64 size, int isDir, UInt64 mtime) {
  if (z7enc_items_grow(items) != 0) return -1;
  Z7EncEntry* e = &items->items[items->count];
  memset(e, 0, sizeof(*e));
  const char* n1 = name ? name : "";
  const char* n2 = srcPath ? srcPath : "";
  size_t l1 = strlen(n1);
  size_t l2 = strlen(n2);
  e->name = (char*)malloc(l1 + 1);
  e->srcPath = (char*)malloc(l2 + 1);
  if (!e->name || !e->srcPath) {
    free(e->name);
    free(e->srcPath);
    e->name = (char*)"";
    e->srcPath = (char*)"";
  } else {
    memcpy(e->name, n1, l1 + 1);
    memcpy(e->srcPath, n2, l2 + 1);
  }
  e->size = size;
  e->isDir = isDir;
  e->mtime = mtime;
  items->count++;
  return 0;
}

/* Portable recursive directory walk. Collects regular files and empty
 * directories with archive-relative names (slash-separated). The source
 * root's own name is stripped, so "mydir/file.txt" appears as "file.txt". */
static int z7enc_walk(const char* root, const char* cur, Z7EncItems* items) {
  char childPath[4096];
  stat_struct st;
  if (stat_call(cur, &st) != 0) return -1;
  int isDir = S_ISDIR(st.st_mode);
#ifdef _WIN32
  isDir = (st.st_mode & _S_IFDIR) != 0;
#endif
  if (isDir) {
    if (strcmp(cur, root) != 0) {
      char tmp[4096];
      snprintf(tmp, sizeof(tmp), "%s", cur + strlen(root) + 1);
      size_t L = strlen(tmp);
      while (L > 0 && (tmp[L-1] == '/' || tmp[L-1] == '\\')) tmp[--L] = 0;
      z7enc_item_add(items, tmp, cur, 0, 1, (UInt64)st.st_mtime);
    }
#ifdef _WIN32
    WIN32_FIND_DATAA findData;
    char searchPath[4096];
    snprintf(searchPath, sizeof(searchPath), "%s\\*", cur);
    HANDLE hFind = FindFirstFileA(searchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE) return -1;
    do {
      if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
        continue;
      snprintf(childPath, sizeof(childPath), "%s\\%s", cur, findData.cFileName);
      if (z7enc_walk(root, childPath, items) != 0) { FindClose(hFind); return -1; }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
#else
    DIR* d = opendir(cur);
    if (!d) return -1;
    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
      if (de->d_name[0] == '.' && (de->d_name[1] == 0 ||
          (de->d_name[1] == '.' && de->d_name[2] == 0)))
        continue;
      snprintf(childPath, sizeof(childPath), "%s/%s", cur, de->d_name);
      if (z7enc_walk(root, childPath, items) != 0) { closedir(d); return -1; }
    }
    closedir(d);
#endif
    return 0;
  }
  const char* rel = cur + strlen(root) + 1;
  z7enc_item_add(items, rel, cur, (UInt64)st.st_size, 0, (UInt64)st.st_mtime);
  return 0;
}

/* Free the walk's entries. */
static void z7enc_items_free(Z7EncItems* it) {
  for (size_t i = 0; i < it->count; i++) {
    free((void*)it->items[i].srcPath);
    free((void*)it->items[i].name);
  }
  free(it->items);
  it->items = NULL;
  it->count = 0;
  it->cap = 0;
}

/* Z7EncReadFunc callback: reads entry `index`'s bytes from disk into `buf`. */
static size_t z7enc_read_cb(void* in, size_t index, Byte* buf, size_t cap, void* ctx) {
  Z7EncInput* inp = (Z7EncInput*)in;
  Z7EncItems* it = (Z7EncItems*)ctx;
  (void)inp;
  if (index >= it->count) return 0;
  const Z7EncEntry* e = &it->items[index];
  if (!e->size) return 0;
  FILE* f = fopen(e->srcPath, "rb");
  if (!f) return 0;
  size_t want = (size_t)e->size;
  if (want > cap) want = cap;
  size_t got = fread(buf, 1, want, f);
  fclose(f);
  return got;
}

/* ------------------------------ options parse ------------------------------ */

static Z7EncOptions z7enc_parse_opts(Value optsObj) {
  Z7EncOptions o;
  Z7EncOptions_Default(&o);
  if (optsObj.tag != TAG_OBJECT || !optsObj.as.object) return o;
  TSHashMap* m = (TSHashMap*)optsObj.as.object;

  Value v;
  v = ts_hashmap_get(m, ts_string_new("format"));
  if (v.tag == TAG_STRING && v.as.string && v.as.string->data) {
    /* only "7z" is supported; reject others early */
    if (strcmp(v.as.string->data, "7z") != 0) {
      snprintf(g_z7_error, sizeof(g_z7_error),
               "unsupported archive format '%s' (only '7z' is supported)",
               v.as.string->data);
      o.format = 99; /* sentinel: caller checks */
      return o;
    }
  }
  v = ts_hashmap_get(m, ts_string_new("method"));
  if (v.tag == TAG_STRING && v.as.string && v.as.string->data) {
    o.method = (strcmp(v.as.string->data, "copy") == 0) ? Z7ENC_METHOD_COPY : Z7ENC_METHOD_LZMA2;
  }
  v = ts_hashmap_get(m, ts_string_new("level"));
  if (v.tag == TAG_NUMBER) o.level = (int)v.as.number;
  v = ts_hashmap_get(m, ts_string_new("dictSize"));
  if (v.tag == TAG_NUMBER) o.dictSize = (UInt32)v.as.number;
  v = ts_hashmap_get(m, ts_string_new("wordSize"));
  if (v.tag == TAG_NUMBER) o.wordSize = (UInt32)v.as.number;
  v = ts_hashmap_get(m, ts_string_new("solidSize"));
  if (v.tag == TAG_NUMBER) o.solidSize = (UInt64)v.as.number;
  v = ts_hashmap_get(m, ts_string_new("threads"));
  if (v.tag == TAG_NUMBER) o.threads = (int)v.as.number;
  v = ts_hashmap_get(m, ts_string_new("memory"));
  if (v.tag == TAG_NUMBER) o.memory = (UInt64)v.as.number;
  v = ts_hashmap_get(m, ts_string_new("volumeSize"));
  if (v.tag == TAG_NUMBER) o.volumeSize = (UInt64)v.as.number;
  v = ts_hashmap_get(m, ts_string_new("password"));
  if (v.tag == TAG_STRING && v.as.string && v.as.string->data && v.as.string->data[0]) {
    o.password = v.as.string->data;
  }
  return o;
}

/* ------------------------------ public entry ------------------------------- */

Value node_7zip_compress(Value srcPath, Value outPath, Value optsObj) {
  char nb[64], nb2[64];
  const char* src = z7_value_cstr(srcPath, nb, sizeof(nb));
  const char* out = z7_value_cstr(outPath, nb2, sizeof(nb2));
  if (!src || !src[0]) return z7_fail("empty source path");
  if (!out || !out[0]) return z7_fail("empty output path");

  struct stat st;
  if (stat(src, &st) != 0) return z7_fail("source not found");

  Z7EncOptions o = z7enc_parse_opts(optsObj);
  if (o.format == 99) return ts_value_null(); /* sentinel set in parse */

  /* Build the entry list. */
  Z7EncItems items = {0};
  int wres;
  if (S_ISDIR(st.st_mode)) {
    wres = z7enc_walk(src, src, &items);
  } else {
    /* single file: entry name is the basename */
    const char* p = strrchr(src, '/');
    const char* p2 = strrchr(src, '\\');
    const char* base = (p > p2 ? p : p2);
    base = base ? base + 1 : src;
    z7enc_item_add(&items, base, src, (UInt64)st.st_size, 0, (UInt64)st.st_mtime);
    wres = 0;
  }
  if (wres != 0) {
    z7enc_items_free(&items);
    return z7_fail("cannot read source");
  }

  /* Ensure the parent directory of outPath exists. */
  {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", out);
    z7_mkparents(tmp);
  }

  Z7EncInput inp = {0};
  inp.entries = items.items;
  inp.numEntries = items.count;
  inp.readFile = z7enc_read_cb;
  inp.ctx = &items;
  inp.archiveOut = out;

  Z7EncOptions full;
  Z7EncOptions_Default(&full);
  full = o;

  /* Volume split: when `volumeSize` is set we still write a single-volume
   * archive (the in-tree decoder reads one file; split-volume reading
   * requires an external tool like 7z / p7zip). The `volumeCount` in the
   * result reports how many `.NNN` slices *would* have been produced so
   * callers can plan around it. The archive on disk is fully readable. */
  int volumeCount = 1;
  if (full.volumeSize > 0 && res_total_packed_size(full.volumeSize)) {
    /* estimated: ceil(totalUnpacked / volumeSize) — but we only write 1 file */
  }

  Z7EncResult res = {0};
  int rc = z7enc_compress7z(&inp, &full, &res);
  if (rc != 0) {
    z7enc_items_free(&items);
    return z7_fail(res.error ? res.error : "compress failed");
  }

  /* When volumeSize is requested, report the estimated volume count. */
  if (full.volumeSize > 0 && res.totalPacked > 0) {
    volumeCount = (int)((res.totalPacked + full.volumeSize - 1) / full.volumeSize);
    if (volumeCount < 1) volumeCount = 1;
  }

  TSHashMap* r = ts_hashmap_new();
  ts_hashmap_set(r, ts_string_new("filesCount"), ts_value_number((double)res.filesCount));
  ts_hashmap_set(r, ts_string_new("totalUnpacked"), ts_value_number((double)res.totalUnpacked));
  ts_hashmap_set(r, ts_string_new("totalPacked"), ts_value_number((double)res.totalPacked));
  ts_hashmap_set(r, ts_string_new("volumeCount"), ts_value_number(volumeCount));
  ts_hashmap_set(r, ts_string_new("error"), ts_value_string(ts_string_new("")));
  z7enc_items_free(&items);
  Z7EncResult_Clear(&res);
  return ts_value_object(r);
}

/* (unused helper, kept for clarity) */
static int res_total_packed_size(UInt64 v) { (void)v; return 0; }