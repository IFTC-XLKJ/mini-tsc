#ifndef NODE_7ZIP_H
#define NODE_7ZIP_H

#include "runtime.h"

/*
 * 7z archive reader — wraps the vendored 7-Zip decoder
 * (runtime/src/7zip/) with the mini-tsc Value API.
 *
 * Typical usage (TS):
 *
 *   import * as sevenZip from "7zip";
 *
 *   // one-shot extract
 *   sevenZip.extractToDir("app.7z", "./out");
 *
 *   // or inspect first
 *   const entries = sevenZip.list("app.7z");   // [{ name, size, isDir, mtime, cTime, attrib, index }]
 *   const data    = sevenZip.readFile("app.7z", "config.json");   // UTF-8 string
 *   const raw     = sevenZip.readFileBuffer("app.7z", "img.png"); // binary-safe Buffer
 *   const info    = sevenZip.info("app.7z");   // { count, fileCount, dirCount, totalSize }
 *
 * Handles returned by open() must be closed with close().
 *
 * The handle API is function-based (open, handle* functions, and close all take
 * the handle as the first argument) rather than instance methods, which avoids
 * the transpiler's name-based method dispatch ambiguity.
 */

/* ---- one-shot helpers ----
 * `password` is an optional trailing argument: pass undefined/null (or "") for
 * unencrypted archives. It decrypts 7zAES-encrypted folders. */

/* List entries: array of { name, size, isDir, mtime, cTime, attrib, index }. */
Value node_7zip_list(Value archivePath, Value password);

/* Extract every entry into destDir (created if missing).
 * Returns the number of files written, or null on error (see lastError()). */
Value node_7zip_extractToDir(Value archivePath, Value destDir, Value password);

/* Extract only entries at/under `prefix` into destDir (created if missing).
 * `prefix` matches a whole path component ("" extracts everything).
 * Returns the number of files written, or null on error. */
Value node_7zip_extractMatching(Value archivePath, Value prefix, Value destDir, Value password);

/* Extract a single entry to an explicit output path. */
Value node_7zip_extractFile(Value archivePath, Value entryName, Value outPath, Value password);

/* Read one entry into memory and return it as a UTF-8 string. */
Value node_7zip_readFile(Value archivePath, Value entryName, Value password);
/* Same, but returns a binary-safe Buffer of the raw bytes. */
Value node_7zip_readFileBuffer(Value archivePath, Value entryName, Value password);

/* true if the archive contains the named entry. */
Value node_7zip_hasEntry(Value archivePath, Value entryName, Value password);

/* Metadata object { name, size, isDir, mtime, cTime, attrib, index } or null. */
Value node_7zip_getEntry(Value archivePath, Value entryName, Value password);

/* Array of entry names only. */
Value node_7zip_entryNames(Value archivePath, Value password);

/* Unpacked size of an entry, or -1 when absent. */
Value node_7zip_size(Value archivePath, Value entryName, Value password);

/* Archive summary { count, fileCount, dirCount, totalSize }. */
Value node_7zip_info(Value archivePath, Value password);

/* true if the file exists and looks like a 7z archive. */
Value node_7zip_isArchive(Value archivePath);

/* Message describing the most recent failure ("" when none). */
Value node_7zip_lastError(void);

/* ---- handle API ---- */

/* Open an archive for repeated access → opaque handle (TAG_OBJECT).
 * `password` is an optional trailing argument (undefined/null/"" for none). */
Value node_7zip_open(Value archivePath, Value password);

/* List every entry in an open archive → array of metadata objects. */
Value node_7zip_handleList(Value handle);

/* array of entry names only. */
Value node_7zip_handleNames(Value handle);

/* Extract every entry from an open archive into destDir.
 * Returns the number of files written, or null on error. */
Value node_7zip_handleExtractAll(Value handle, Value destDir);

/* Extract only entries at/under `prefix` from an open archive into destDir.
 * Returns the number of files written, or null on error. */
Value node_7zip_handleExtractMatching(Value handle, Value prefix, Value destDir);

/* Extract a single entry from an open archive to an output path. */
Value node_7zip_handleExtract(Value handle, Value entryName, Value outPath);

/* Read one entry from an open archive into memory as a UTF-8 string. */
Value node_7zip_handleRead(Value handle, Value entryName);

/* Read one entry from an open archive into memory as a Buffer. */
Value node_7zip_handleReadBuffer(Value handle, Value entryName);

/* true when the open archive contains the named entry. */
Value node_7zip_handleHas(Value handle, Value entryName);

/* Metadata object for a named entry, or null. */
Value node_7zip_handleGetEntry(Value handle, Value entryName);

/* Unpacked size of an entry, or -1 when absent. */
Value node_7zip_handleSize(Value handle, Value entryName);

/* Archive summary { count, fileCount, dirCount, totalSize }. */
Value node_7zip_handleInfo(Value handle);

/* true while the handle refers to an open archive. */
Value node_7zip_handleIsOpen(Value handle);

/* Number of entries in the open archive. */
Value node_7zip_handleCount(Value handle);

 /* Close an open archive and release its resources. */
Value node_7zip_close(Value handle);

/* ---- compress API ---- */

/* Compress `srcPath` (a file or a directory, recursively) into `outPath`
 * (.7z). `optsObj` is an object with any of: format ("7z"), method
 * ("lzma2"/"copy"), level (0-9), dictSize (bytes), wordSize (LZMA lc,
 * 0-8), solidSize (bytes), threads (1-N), memory (bytes budget; caps the
 * dict when smaller than the level default), volumeSize (bytes per split
 * volume out.001/.002..., 0 = single file), password (string, 7zAES).
 * Returns { filesCount, totalUnpacked, totalPacked, volumeCount, error },
 * or null on failure (see lastError()). */
Value node_7zip_compress(Value srcPath, Value outPath, Value optsObj);

#endif /* NODE_7ZIP_H */