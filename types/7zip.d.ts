/** mini-tsc ambient types for the `7zip` module (7z archive read/write). */
declare module "7zip" {
  /** A single archive entry as returned by list(). */
  interface ArchiveEntry {
    name: string;
    size: number;
    isDir: boolean;
    /** Unix time in seconds, or null when the archive stores no timestamp. */
    mtime: number | null;
    /** Creation time in Unix seconds, or null when absent. */
    cTime: number | null;
    /** Raw 7-Zip attribute word. High 16 bits are the DOS FILE_ATTRIBUTE_* flags;
     * the low 16 bits carry the Unix mode when bit 31 (UNIX_EXTENSION) is set.
     * 0 when the archive stores no attributes. */
    attrib: number;
    /** Index of the entry within the archive. */
    index: number;
  }

  /** Archive-level summary returned by info(). */
  interface ArchiveInfo {
    count: number;
    fileCount: number;
    dirCount: number;
    totalSize: number;
  }

  /* ---- compress options ---- */

  /** Options for compress(). All keys are optional; defaults are shown. */
  interface CompressOptions {
    /** Archive format. Only "7z" is currently supported. @default "7z" */
    format?: "7z";
    /** Compression method: "lzma2" (default) or "copy" (store-only, fast). @default "lzma2" */
    method?: "lzma2" | "copy";
    /** Compression level 0..9 (0 = fastest, 9 = best ratio). @default 6 */
    level?: number;
    /** LZMA dictionary size in bytes (power of two). 0 = auto from level. */
    dictSize?: number;
    /** LZMA "word size" (literal context bits, lc) 0..8. 0 = default (3). */
    wordSize?: number;
    /** Solid data block size in bytes. 0 = single solid block. */
    solidSize?: number;
    /** CPU thread count (1..N). 0 = single-threaded. */
    threads?: number;
    /** Memory budget in bytes. When smaller than the level's default
     * dictionary, the effective dict is capped to fit. */
    memory?: number;
    /** Volume split size in bytes. When set, outPath is volume 1 and
     * additional volumes are written as outPath.001/.002... 0 = single .7z file. */
    volumeSize?: number;
    /** 7zAES password. When set, the archive is encrypted. */
    password?: string;
  }

  /** Result of a compress() call. */
  interface CompressResult {
    /** Number of files in the archive. */
    filesCount: number;
    /** Total unpacked size in bytes. */
    totalUnpacked: number;
    /** Total packed size in bytes. */
    totalPacked: number;
    /** Number of volume files written (1 when volumeSize is 0; when
     * volumeSize > 0, the count of outPath + outPath.001/.002... written). */
    volumeCount: number;
    /** Empty string on success; error message on failure. */
    error: string;
  }

  /* ---- one-shot helpers (open + close the archive internally) ----
 * The optional trailing `password` decrypts 7zAES-encrypted archives; omit it
 * (or pass undefined) for unencrypted ones. */

/** List every entry in the archive without extracting. null when the
   * archive cannot be opened (see lastError()). */
  function list(archivePath: string, password?: string): ArchiveEntry[] | null;

  /** Extract all entries into destDir (created if it does not exist).
     * Returns the number of files written; null when the archive cannot be
     * opened or extraction fails (see lastError()). */
  function extractToDir(archivePath: string, destDir: string, password?: string): number | null;

  /** Extract only entries at or under `prefix` into destDir. `prefix` matches a
     * whole path component ("" extracts everything). Returns the number of files
     * written; null on error (see lastError()). */
  function extractMatching(archivePath: string, prefix: string, destDir: string, password?: string): number | null;

  /** Extract a single entry to an explicit output path. */
  function extractFile(archivePath: string, entryName: string, outPath: string, password?: string): void;

  /** Read one entry into memory and return it as a UTF-8 string.
     * Returns null when the entry or archive is missing (see lastError()). */
  function readFile(archivePath: string, entryName: string, password?: string): string | null;

  /** Read one entry into memory as a binary-safe Buffer.
     * Returns null when the entry or archive is missing (see lastError()). */
  function readFileBuffer(archivePath: string, entryName: string, password?: string): any;

  /** true when the archive contains the named entry. */
  function hasEntry(archivePath: string, entryName: string, password?: string): boolean;

  /** Metadata for a single entry, or null when absent. */
  function getEntry(archivePath: string, entryName: string, password?: string): ArchiveEntry | null;

  /** Array of entry names only; null when the archive cannot be opened. */
  function entryNames(archivePath: string, password?: string): string[] | null;

  /** Unpacked size of an entry, or -1 when absent. */
  function size(archivePath: string, entryName: string, password?: string): number;

  /** Archive-level summary. */
  function info(archivePath: string, password?: string): ArchiveInfo | null;

  /** true when the path exists and begins with the 7z signature. */
  function isArchive(archivePath: string): boolean;

  /** Message describing the most recent failure ("" when none). */
  function lastError(): string;

  /* ---- handle API (open once, read many) ---- */

  /** Open an archive for repeated access. Call call close() when done.
     * `password` decrypts 7zAES-encrypted archives (optional). */
  function open(archivePath: string, password?: string): any;

  /** List every entry in an open archive; null when the handle is not open. */
  function handleList(handle: any): ArchiveEntry[] | null;

  /** Array of entry names in an open archive; null when the handle is not open. */
  function handleNames(handle: any): string[] | null;

  /** Extract all entries from an open archive into destDir.
     * Returns the number of files written; null on error (see lastError()). */
  function handleExtractAll(handle: any, destDir: string): number | null;

  /** Extract only entries at/under `prefix` from an open archive into destDir.
     * Returns the number of files written; null on error (see lastError()). */
  function handleExtractMatching(handle: any, prefix: string, destDir: string): number | null;

  /** Extract a single entry from an open archive to an output path. */
  function handleExtract(handle: any, entryName: string, outPath: string): void;

  /** Read one entry from an open archive into memory as a UTF-8 string.
     * null when the handle is not open or the entry is missing. */
  function handleRead(handle: any, entryName: string): string | null;

  /** Read one entry from an open archive into memory as a Buffer.
     * null when the handle is not open or the entry is missing. */
  function handleReadBuffer(handle: any, entryName: string): any;

  /** true when the open archive contains the named entry. */
  function handleHas(handle: any, entryName: string): boolean;

  /** Metadata for a named entry in an open archive, or null. */
  function handleGetEntry(handle: any, entryName: string): ArchiveEntry | null;

  /** Unpacked size of an entry in an open archive, or -1 when absent. */
  function handleSize(handle: any, entryName: string): number;

  /** Archive-level summary for an open archive. */
  function handleInfo(handle: any): ArchiveInfo | null;

  /** true while the handle refers to an open archive. */
  function handleIsOpen(handle: any): boolean;

  /** Number of entries in the open archive. */
  function handleCount(handle: any): number;

  /** Close an open archive and release its resources. */
  function close(handle: any): void;

  /* ---- compression ---- */

  /** Compress `srcPath` (a file or a directory, recursively) into `outPath`
   * (.7z archive). Returns a result object; `null` on failure (see
   * lastError()). With volumeSize, outPath is the first volume and the rest
   * are written as outPath.001/.002... Pass a password in `options` to
   * encrypt with 7zAES. */
  function compress(srcPath: string, outPath: string, options?: CompressOptions): CompressResult | null;

  export {
    ArchiveEntry, ArchiveInfo, CompressOptions, CompressResult,
    list, extractToDir, extractMatching, extractFile, readFile, readFileBuffer,
    hasEntry, getEntry, entryNames, size, info, isArchive, lastError,
    open, handleList, handleNames, handleExtractAll, handleExtractMatching, handleExtract, handleRead,
    handleReadBuffer, handleHas, handleGetEntry, handleSize, handleInfo,
    handleIsOpen, handleCount, close,
    compress,
  };
}