/** Global ambient types for mini-tsc (timers, process, Buffer, dialogs). */

declare module "url" {
  interface URL {
    href: string;
    protocol: string;
    hostname: string;
    port: string;
    pathname: string;
    search: string;
    hash: string;
    searchParams: URLSearchParams;
    toString(): string;
  }

  interface URLSearchParams {
    get(name: string): string | null;
    set(name: string, value: string): void;
  }

  function fileURLToPath(url: URL | string): string;
  function pathToFileURL(path: string): URL;

  export { URL, URLSearchParams, fileURLToPath, pathToFileURL };
}

type BufferEncoding =
  | "ascii"
  | "utf8"
  | "utf-8"
  | "utf16le"
  | "ucs2"
  | "ucs-2"
  | "base64"
  | "latin1"
  | "binary"
  | "hex";

/* Uint8Array<ArrayBuffer> so Buffer is assignable to DOM BodyInit / BufferSource. */
interface Buffer extends Uint8Array<ArrayBuffer> {
  toString(encoding?: BufferEncoding): string;
  slice(start?: number, end?: number): Buffer;
  readUInt8(offset?: number): number;
  writeUInt8(value: number, offset?: number): number;
}



interface BufferConstructor {
  from(data: string | number[] | Buffer, encoding?: BufferEncoding): Buffer;
  alloc(size: number, fill?: string | number | Buffer, encoding?: BufferEncoding): Buffer;
  allocUnsafe(size: number): Buffer;
  concat(list: Buffer[], totalLength?: number): Buffer;
  isBuffer(obj: any): obj is Buffer;
  new (size: number): Buffer;
}

interface ProcessStream {
  readonly fd: number;
  readonly isTTY: boolean;
  readonly rows: number;
  readonly columns: number;
  write(data: string | Buffer | Uint8Array): boolean;
  on(event: "data", listener: (chunk: string | Buffer) => void): this;
  on(event: string, listener: (...args: any[]) => void): this;
  cursorTo(x: number, y?: number): boolean;
  moveCursor(dx: number, dy: number): boolean;
  clearScreenDown(): boolean;
  clearLine(dir?: number): boolean;
}

interface Process {
  readonly env: { [key: string]: string | undefined };
  readonly argv: string[];
  readonly pid: number;
  readonly stdin: ProcessStream;
  readonly stdout: ProcessStream;
  readonly stderr: ProcessStream;
  readonly platform: string;
  cwd(): string;
  chdir(directory: string): void;
  exit(code?: number): never;
  on(event: "message", listener: (message: any) => void): this;
  on(event: string, listener: (...args: any[]) => void): this;
  send(message: any): boolean;
}

declare function setTimeout(callback: (...args: any[]) => void, delay?: number, ...args: any[]): number;
declare function setInterval(
  callback: (...args: any[]) => void,
  delay?: number,
  ...args: any[]
): number;
declare function clearTimeout(id?: number): void;
declare function clearInterval(id?: number): void;

/** Print message and wait for Enter. */
declare function alert(message?: any): void;
/** Print message + "[y/N]"; loops until y/Y (true) or n/N (false). */
declare function confirm(message?: any): boolean;
/** Print message, read a line; empty input returns null. */
declare function prompt(message?: any): string | null;

declare const __dirname: string;
declare const __filename: string;

declare const Buffer: BufferConstructor;
declare var process: Process;
