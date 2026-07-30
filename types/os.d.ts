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

  interface DiskInfo {
    mount?: string;
    filesystem?: string;
    fstype?: string;
    total?: number;
    free?: number;
    used?: number;
    usagePercent?: number;
  }

  interface NetworkStats {
    name?: string;
    description?: string;
    mac?: string;
    bytesReceived?: number;
    bytesSent?: number;
    packetsReceived?: number;
    packetsSent?: number;
    errorsReceived?: number;
    errorsSent?: number;
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
  function cpuTemperature(): number[];
  function diskUsage(): DiskInfo[];
  function networkStats(): NetworkStats[];

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
    cpuTemperature,
    diskUsage,
    networkStats,
    EOL,
    devNull,
    defaultEncoding,
    CpuInfo,
    UserInfo,
    GpuInfo,
    DiskInfo,
    NetworkStats,
  };
}
