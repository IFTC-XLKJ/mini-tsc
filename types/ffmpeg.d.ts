/** mini-tsc ambient types for the `ffmpeg` module (system ffmpeg/ffprobe wrappers). */
declare module "ffmpeg" {
  export interface RunOptions {
    cwd?: string;
    bin?: string;
    args?: string[]; // extra args for probe()
  }

  export interface RunResult {
    stdout: string;
    stderr: string;
    status: number;
    exitCode: number;
    ok: boolean;
    file?: string;
  }

  /** Run ffmpeg with argument array. */
  function run(args: string[], options?: RunOptions): RunResult;
  /** Run a raw ffmpeg command line via shell. */
  function exec(command: string, options?: RunOptions): RunResult;
  /** Run ffprobe with argument array. */
  function ffprobe(args: string[], options?: RunOptions): RunResult;
  /** Probe a media file via ffprobe and return JSON in stdout (parse with JSON.parse). */
  function probe(file: string, options?: RunOptions): RunResult;
  /** Return `ffmpeg -version` output string. */
  function version(): string;
  /** Whether ffmpeg binary is available on PATH. */
  function isAvailable(): boolean;

  export { run, exec, ffprobe, probe, version, isAvailable };
}
