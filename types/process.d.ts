/** Node.js `process` ambient types for mini-tsc. */
declare module "process" {
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

  interface ProcessEnv {
    [key: string]: string | undefined;
  }

  interface Process {
    readonly env: ProcessEnv;
    readonly argv: string[];
    readonly pid: number;
    readonly platform: string;
    readonly stdin: ProcessStream;
    readonly stdout: ProcessStream;
    readonly stderr: ProcessStream;
    cwd(): string;
    chdir(directory: string): void;
    exit(code?: number): never;
    on(event: "message", listener: (message: any) => void): this;
    on(event: string, listener: (...args: any[]) => void): this;
    send(message: any): boolean;
  }

  const process: Process;
  const env: ProcessEnv;
  const argv: string[];
  const pid: number;
  const platform: string;
  const stdin: ProcessStream;
  const stdout: ProcessStream;
  const stderr: ProcessStream;
  function cwd(): string;
  function chdir(directory: string): void;
  function exit(code?: number): never;

  export {
    process,
    env,
    argv,
    pid,
    platform,
    stdin,
    stdout,
    stderr,
    cwd,
    chdir,
    exit,
    Process,
    ProcessStream,
    ProcessEnv,
  };
  export default process;
}
