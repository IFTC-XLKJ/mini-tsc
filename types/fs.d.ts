/** Node.js `fs` / `fs/promises` ambient types for mini-tsc. */
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

  type PathLike = string;

  /* Synchronous */
  function readFileSync(path: PathLike): Buffer;
  function readFileSync(path: PathLike, encoding: BufferEncoding): string;
  function readFileSync(path: PathLike, options: ReadFileOptions | BufferEncoding): Buffer | string;

  function writeFileSync(
    path: PathLike,
    data: string | Buffer | Uint8Array,
    options?: WriteFileOptions | BufferEncoding,
  ): void;
  function existsSync(path: PathLike): boolean;
  function mkdirSync(path: PathLike, options?: MkdirOptions | number): void;
  function readdirSync(path: PathLike): string[];
  function unlinkSync(path: PathLike): void;
  function statSync(path: PathLike): Stats;
  function rmdirSync(path: PathLike): void;
  function renameSync(oldPath: PathLike, newPath: PathLike): void;
  function readlinkSync(path: PathLike): string;
  function symlinkSync(target: PathLike, path: PathLike): void;
  function chmodSync(path: PathLike, mode: number | string): void;

  /* Asynchronous (Promise-returning in mini-tsc runtime) */
  function readFile(path: PathLike): Promise<Buffer>;
  function readFile(path: PathLike, encoding: BufferEncoding): Promise<string>;
  function readFile(path: PathLike, options: ReadFileOptions | BufferEncoding): Promise<Buffer | string>;

  function writeFile(
    path: PathLike,
    data: string | Buffer | Uint8Array,
    options?: WriteFileOptions | BufferEncoding,
  ): Promise<void>;
  function access(path: PathLike, mode?: number): Promise<boolean>;
  function mkdir(path: PathLike, options?: MkdirOptions | number): Promise<void>;
  function readdir(path: PathLike): Promise<string[]>;
  function unlink(path: PathLike): Promise<void>;
  function stat(path: PathLike): Promise<Stats>;
  function rmdir(path: PathLike): Promise<void>;
  function rename(oldPath: PathLike, newPath: PathLike): Promise<void>;
  function readlink(path: PathLike): Promise<string>;
  function symlink(target: PathLike, path: PathLike): Promise<void>;
  function chmod(path: PathLike, mode: number | string): Promise<void>;

  const promises: typeof import("fs/promises");

  export {
    readFileSync,
    writeFileSync,
    existsSync,
    mkdirSync,
    readdirSync,
    unlinkSync,
    statSync,
    rmdirSync,
    renameSync,
    readlinkSync,
    symlinkSync,
    chmodSync,
    readFile,
    writeFile,
    access,
    mkdir,
    readdir,
    unlink,
    stat,
    rmdir,
    rename,
    readlink,
    symlink,
    chmod,
    promises,
    Stats,
    WriteFileOptions,
    ReadFileOptions,
    MkdirOptions,
    PathLike,
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

  type PathLike = string;

  function readFile(path: PathLike): Promise<Buffer>;
  function readFile(path: PathLike, encoding: BufferEncoding): Promise<string>;
  function readFile(path: PathLike, options: ReadFileOptions | BufferEncoding): Promise<Buffer | string>;

  function writeFile(
    path: PathLike,
    data: string | Buffer | Uint8Array,
    options?: WriteFileOptions | BufferEncoding,
  ): Promise<void>;
  function access(path: PathLike, mode?: number): Promise<boolean>;
  function mkdir(path: PathLike, options?: MkdirOptions | number): Promise<void>;
  function readdir(path: PathLike): Promise<string[]>;
  function unlink(path: PathLike): Promise<void>;
  function stat(path: PathLike): Promise<Stats>;
  function rmdir(path: PathLike): Promise<void>;
  function rename(oldPath: PathLike, newPath: PathLike): Promise<void>;
  function readlink(path: PathLike): Promise<string>;
  function symlink(target: PathLike, path: PathLike): Promise<void>;
  function chmod(path: PathLike, mode: number | string): Promise<void>;

  export {
    readFile,
    writeFile,
    access,
    mkdir,
    readdir,
    unlink,
    stat,
    rmdir,
    rename,
    readlink,
    symlink,
    chmod,
    Stats,
    WriteFileOptions,
    ReadFileOptions,
    MkdirOptions,
    PathLike,
  };
}
