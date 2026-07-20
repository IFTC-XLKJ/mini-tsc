/** Minimal Node.js `process` ambient types for mini-tsc. */
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

  interface Process {
    readonly env: { [key: string]: string | undefined };
    readonly argv: string[];
    readonly pid: number;
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
  const env: Process["env"];
  const argv: string[];
  const pid: number;
  const stdin: ProcessStream;
  const stdout: ProcessStream;
  const stderr: ProcessStream;
  function cwd(): string;
  function chdir(directory: string): void;
  function exit(code?: number): never;

  export {
    process, env, argv, pid, stdin, stdout, stderr, cwd, chdir, exit,
    Process, ProcessStream,
  };
  export default process;
}
