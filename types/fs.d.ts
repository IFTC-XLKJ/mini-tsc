/** Minimal Node.js `fs` ambient types for mini-tsc. */
declare module "fs" {
  interface Stats {
    size: number;
    mtime: number;
    isFile(): boolean;
    isDirectory(): boolean;
  }

  interface WriteFileOptions {
    encoding?: BufferEncoding | null;
    mode?: number | string;
    flag?: string;
  }

  interface ReadFileOptions {
    encoding?: BufferEncoding | null;
    flag?: string;
  }

  interface MkdirOptions {
    recursive?: boolean;
    mode?: number | string;
  }

  /* Synchronous */
  function readFileSync(path: string): Buffer;
  function readFileSync(path: string, encoding: BufferEncoding): string;
  function readFileSync(path: string, options: ReadFileOptions): Buffer | string;

  function writeFileSync(path: string, data: string | Buffer, options?: WriteFileOptions | BufferEncoding): void;
  function existsSync(path: string): boolean;
  function mkdirSync(path: string, options?: MkdirOptions | number): void;
  function readdirSync(path: string): string[];
  function unlinkSync(path: string): void;
  function statSync(path: string): Stats;
  function rmdirSync(path: string): void;
  function renameSync(oldPath: string, newPath: string): void;
  function readlinkSync(path: string): string;
  function symlinkSync(target: string, path: string): void;
  function chmodSync(path: string, mode: number | string): void;

  /* Asynchronous (Promise-style in mini-tsc) */
  function readFile(path: string): Promise<Buffer>;
  function readFile(path: string, encoding: BufferEncoding): Promise<string>;
  function readFile(path: string, options: ReadFileOptions): Promise<Buffer | string>;

  function writeFile(path: string, data: string | Buffer, options?: WriteFileOptions | BufferEncoding): Promise<void>;
  function access(path: string, mode?: number): Promise<boolean>;
  function mkdir(path: string, options?: MkdirOptions | number): Promise<void>;
  function readdir(path: string): Promise<string[]>;
  function unlink(path: string): Promise<void>;
  function stat(path: string): Promise<Stats>;
  function rmdir(path: string): Promise<void>;
  function rename(oldPath: string, newPath: string): Promise<void>;
  function readlink(path: string): Promise<string>;
  function symlink(target: string, path: string): Promise<void>;
  function chmod(path: string, mode: number | string): Promise<void>;

  export {
    readFileSync, writeFileSync, existsSync, mkdirSync, readdirSync,
    unlinkSync, statSync, rmdirSync, renameSync, readlinkSync, symlinkSync, chmodSync,
    readFile, writeFile, access, mkdir, readdir, unlink, stat, rmdir, rename, readlink, symlink, chmod,
    Stats, WriteFileOptions, ReadFileOptions, MkdirOptions,
  };
}

declare module "fs/promises" {
  interface Stats {
    size: number;
    mtime: number;
    isFile(): boolean;
    isDirectory(): boolean;
  }

  interface WriteFileOptions {
    encoding?: BufferEncoding | null;
    mode?: number | string;
    flag?: string;
  }

  interface ReadFileOptions {
    encoding?: BufferEncoding | null;
    flag?: string;
  }

  interface MkdirOptions {
    recursive?: boolean;
    mode?: number | string;
  }

  function readFile(path: string): Promise<Buffer>;
  function readFile(path: string, encoding: BufferEncoding): Promise<string>;
  function readFile(path: string, options: ReadFileOptions): Promise<Buffer | string>;

  function writeFile(path: string, data: string | Buffer, options?: WriteFileOptions | BufferEncoding): Promise<void>;
  function access(path: string, mode?: number): Promise<boolean>;
  function mkdir(path: string, options?: MkdirOptions | number): Promise<void>;
  function readdir(path: string): Promise<string[]>;
  function unlink(path: string): Promise<void>;
  function stat(path: string): Promise<Stats>;
  function rmdir(path: string): Promise<void>;
  function rename(oldPath: string, newPath: string): Promise<void>;
  function readlink(path: string): Promise<string>;
  function symlink(target: string, path: string): Promise<void>;
  function chmod(path: string, mode: number | string): Promise<void>;

  export {
    readFile, writeFile, access, mkdir, readdir, unlink, stat, rmdir, rename, readlink, symlink, chmod,
    Stats, WriteFileOptions, ReadFileOptions, MkdirOptions,
  };
}
