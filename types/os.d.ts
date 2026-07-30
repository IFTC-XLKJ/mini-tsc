/** Node.js `os` ambient types for mini-tsc. */
declare module "os" {
  interface CpuInfo {
    model?: string;
    speed?: number;
    times?: { user: number; nice: number; sys: number; idle: number; irq: number };
    usage?: number;
  }

  interface UserInfo {
    username: string;
    uid?: number;
    gid?: number;
    shell?: string;
    homedir?: string;
  }

  interface GpuInfo {
    name?: string;
    vendor?: string;
    memoryMB?: number;
    driverVersion?: string;
    utilization?: number;
    temperature?: number;
  }

  function platform(): string;
  function hostname(): string;
  function totalmem(): number;
  function freemem(): number;
  function arch(): string;
  function cpus(): CpuInfo[];
  function userInfo(): UserInfo;
  function type(): string;
  function release(): string;
  function uptime(): number;
  function loadavg(): number[];
  function homedir(): string;
  function tmpdir(): string;
  function version(): string;
  function machine(): string;
  function manufacturer(): string;
  function model(): string;
  function serial(): string;
  function biosVersion(): string;
  function biosReleaseDate(): string;
  function gpuInfo(): GpuInfo[];

  const EOL: string;
  const devNull: string;
  const defaultEncoding: string;

  export {
    platform,
    hostname,
    totalmem,
    freemem,
    arch,
    cpus,
    userInfo,
    type,
    release,
    uptime,
    loadavg,
    homedir,
    tmpdir,
    version,
    machine,
    manufacturer,
    model,
    serial,
    biosVersion,
    biosReleaseDate,
    gpuInfo,
    EOL,
    devNull,
    defaultEncoding,
    CpuInfo,
    UserInfo,
    GpuInfo,
  };
}
