/** Minimal Node.js `child_process` ambient types for mini-tsc. */
declare module "child_process" {
  interface ChildProcessStream {
    on(event: "data", listener: (data: string | Buffer) => void): this;
    on(event: string, listener: (...args: any[]) => void): this;
  }

  interface ChildProcess {
    readonly pid?: number;
    readonly status?: number;
    readonly stdout: ChildProcessStream;
    readonly stderr: ChildProcessStream;
    on(event: "close", listener: (code: number | null, signal: string | null) => void): this;
    on(event: "message", listener: (message: any) => void): this;
    on(event: "error", listener: (err: any) => void): this;
    on(event: string, listener: (...args: any[]) => void): this;
    send(message: any): boolean;
  }

  type ExecCallback = (error: any, stdout: string, stderr: string) => void;

  function execSync(command: string, options?: any): string;
  function execFileSync(file: string, args?: string[], options?: any): string;
  function exec(command: string, callback?: ExecCallback): string;
  function exec(command: string, options: any, callback?: ExecCallback): string;
  function execFile(file: string, args?: string[], callback?: ExecCallback): string;
  function execFile(file: string, args: string[], options: any, callback?: ExecCallback): string;
  function spawn(command: string, args?: string[], options?: any): ChildProcess;
  function fork(modulePath: string, args?: string[], options?: any): ChildProcess;

  export {
    execSync, execFileSync, exec, execFile, spawn, fork,
    ChildProcess, ChildProcessStream, ExecCallback,
  };
}
