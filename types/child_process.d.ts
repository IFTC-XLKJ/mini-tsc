/** Node.js `child_process` ambient types for mini-tsc. */
declare module "child_process" {
  interface ChildProcessStream {
    on(event: "data", listener: (data: string | Buffer) => void): this;
    on(event: string, listener: (...args: any[]) => void): this;
  }

  interface ChildProcess {
    readonly pid?: number;
    readonly status?: number | null;
    /* mini-tsc always attaches stream objects (may be no-op stubs) */
    readonly stdout: ChildProcessStream;
    readonly stderr: ChildProcessStream;
    readonly stdin?: ChildProcessStream;
    on(event: "close", listener: (code: number | null, signal?: string | null) => void): this;
    on(event: "message", listener: (message: any) => void): this;
    on(event: "error", listener: (err: any) => void): this;
    on(event: "exit", listener: (code: number | null, signal?: string | null) => void): this;
    on(event: string, listener: (...args: any[]) => void): this;
    send(message: any): boolean;
    kill?(signal?: string | number): boolean;
  }

  type StdioOptions =
    | "pipe"
    | "ignore"
    | "inherit"
    | Array<"pipe" | "ipc" | "ignore" | "inherit" | "overlapped" | number | null | undefined>
    | any;

  interface CommonExecOptions {
    cwd?: string | URL;
    env?: { [key: string]: string | undefined };
    encoding?: BufferEncoding | string | null;
    timeout?: number;
    maxBuffer?: number;
    killSignal?: string | number;
    uid?: number;
    gid?: number;
    windowsHide?: boolean;
    shell?: string | boolean;
    /** Node execFile/execSync: "pipe" | "ignore" | "inherit" | array */
    stdio?: StdioOptions;
    input?: string | Buffer | Uint8Array;
  }

  interface ExecOptions extends CommonExecOptions {}
  interface ExecSyncOptions extends CommonExecOptions {}
  interface ExecFileOptions extends CommonExecOptions {}
  interface ExecFileSyncOptions extends CommonExecOptions {}

  interface SpawnOptions extends CommonExecOptions {
    argv0?: string;
    detached?: boolean;
    stdio?: StdioOptions;
  }

  type ExecCallback = (error: any, stdout: string | Buffer, stderr: string | Buffer) => void;

  function execSync(command: string, options?: ExecSyncOptions): string | Buffer;
  function execFileSync(file: string, options?: ExecFileSyncOptions): string | Buffer;
  function execFileSync(
    file: string,
    args?: readonly string[] | null,
    options?: ExecFileSyncOptions,
  ): string | Buffer;

  function exec(command: string, callback?: ExecCallback): ChildProcess;
  function exec(command: string, options: ExecOptions, callback?: ExecCallback): ChildProcess;

  function execFile(file: string, callback?: ExecCallback): ChildProcess;
  function execFile(file: string, args?: readonly string[] | null, callback?: ExecCallback): ChildProcess;
  function execFile(
    file: string,
    args: readonly string[] | null | undefined,
    options: ExecFileOptions,
    callback?: ExecCallback,
  ): ChildProcess;

  function spawn(command: string, options?: SpawnOptions): ChildProcess;
  function spawn(command: string, args?: readonly string[], options?: SpawnOptions): ChildProcess;

  function fork(modulePath: string, options?: SpawnOptions): ChildProcess;
  function fork(modulePath: string, args?: readonly string[], options?: SpawnOptions): ChildProcess;

  export {
    execSync,
    execFileSync,
    exec,
    execFile,
    spawn,
    fork,
    ChildProcess,
    ChildProcessStream,
    ExecCallback,
    ExecOptions,
    ExecSyncOptions,
    ExecFileOptions,
    ExecFileSyncOptions,
    SpawnOptions,
    CommonExecOptions,
    StdioOptions,
  };
}
