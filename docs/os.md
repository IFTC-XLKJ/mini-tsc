# os 模块 - 操作系统信息

## 概述

`os` 模块提供了操作系统相关的信息，包括平台、架构、内存、CPU、用户信息等。

## API 列表

### 基本信息

#### `os.platform()`

获取操作系统平台。

**返回值**: `string` - `'win32'` | `'linux'` | `'darwin'` | `'android'`

**示例**:
```typescript
import * as os from 'os';

console.log(os.platform()); // "win32" | "linux" | "darwin"
```

---

#### `os.type()`

获取操作系统类型。

**返回值**: `string` - 如 `'Windows_NT'`, `'Linux'`, `'Darwin'`

**示例**:
```typescript
console.log(os.type()); // "Windows_NT" | "Linux" | "Darwin"
```

---

#### `os.release()`

获取操作系统版本。

**返回值**: `string`

**示例**:
```typescript
console.log(os.release()); // "10.0.19045" | "5.15.0" | "22.6.0"
```

---

#### `os.version()`

获取操作系统版本详细信息。

**返回值**: `string`

---

#### `os.hostname()`

获取主机名。

**返回值**: `string`

**示例**:
```typescript
console.log(os.hostname()); // "my-computer"
```

---

#### `os.arch()`

获取 CPU 架构。

**返回值**: `string` - `'x64'` | `'arm'` | `'arm64'` 等

**示例**:
```typescript
console.log(os.arch()); // "x64"
```

---

#### `os.machine()`

获取机器类型。

**返回值**: `string`

---

### 内存信息

#### `os.totalmem()`

获取系统总内存（字节）。

**返回值**: `number`

**示例**:
```typescript
const totalMemory = os.totalmem();
console.log(`Total memory: ${totalMemory / 1024 / 1024 / 1024} GB`);
```

---

#### `os.freemem()`

获取系统可用内存（字节）。

**返回值**: `number`

**示例**:
```typescript
const freeMemory = os.freemem();
console.log(`Free memory: ${freeMemory / 1024 / 1024 / 1024} GB`);
```

---

#### `os.uptime()`

获取系统运行时间（秒）。

**返回值**: `number`

**示例**:
```typescript
const uptime = os.uptime();
const days = Math.floor(uptime / 86400);
const hours = Math.floor((uptime % 86400) / 3600);
console.log(`Uptime: ${days} days, ${hours} hours`);
```

---

### CPU 信息

#### `os.cpus()`

获取 CPU 信息。

**返回值**: `CpuInfo[]`

```typescript
interface CpuInfo {
  model: string;      // CPU 型号
  speed: number;      // 速度（MHz）
  times: {
    user: number;     // 用户模式时间
    nice: number;     // 低优先级用户模式时间
    sys: number;      // 系统模式时间
    idle: number;     // 空闲时间
    irq: number;      // 中断时间
  };
}
```

**示例**:
```typescript
const cpus = os.cpus();
console.log(`CPU count: ${cpus.length}`);
console.log(`CPU model: ${cpus[0].model}`);
console.log(`CPU speed: ${cpus[0].speed} MHz`);
```

---

#### `os.loadavg()`

获取系统负载平均值（仅 Linux/macOS）。

**返回值**: `[number, number, number]` - [1分钟, 5分钟, 15分钟]

**示例**:
```typescript
if (os.platform() !== 'win32') {
  const [load1, load5, load15] = os.loadavg();
  console.log(`Load: ${load1.toFixed(2)}, ${load5.toFixed(2)}, ${load15.toFixed(2)}`);
}
```

---

#### `os.cpuTemperature()`

获取 CPU 温度（如果支持）。

**返回值**: `number` - 温度（摄氏度）

---

### 用户信息

#### `os.userInfo()`

获取当前用户信息。

**返回值**: `UserInfo`

```typescript
interface UserInfo {
  username: string;   // 用户名
  uid: number;        // 用户 ID（Unix）/ -1（Windows）
  gid: number;        // 组 ID（Unix）/ -1（Windows）
  shell: string;      // Shell（Unix）/ ''（Windows）
  homedir: string;    // 主目录
}
```

**示例**:
```typescript
const user = os.userInfo();
console.log(`User: ${user.username}`);
console.log(`Home: ${user.homedir}`);
```

---

### 路径信息

#### `os.homedir()`

获取当前用户主目录。

**返回值**: `string`

**示例**:
```typescript
console.log(os.homedir()); // "/home/user" | "C:\Users\user"
```

---

#### `os.tmpdir()`

获取临时目录。

**返回值**: `string`

**示例**:
```typescript
console.log(os.tmpdir()); // "/tmp" | "C:\Users\user\AppData\Local\Temp"
```

---

### 系统常量

#### `os.EOL`

获取行结束符。

**返回值**: `string` - `'\r\n'` (Windows) | `'\n'` (Linux/macOS)

**示例**:
```typescript
console.log(`Line ending: ${JSON.stringify(os.EOL)}`);
```

---

#### `os.devNull`

获取空设备路径。

**返回值**: `string` - `'NUL'` (Windows) | `'/dev/null'` (Linux/macOS)

---

#### `os.defaultEncoding()`

获取默认编码。

**返回值**: `string`

---

### 硬件信息

#### `os.manufacturer()`

获取系统制造商。

**返回值**: `string`

---

#### `os.model()`

获取系统型号。

**返回值**: `string`

---

#### `os.serial()`

获取系统序列号。

**返回值**: `string`

---

#### `os.biosVersion()`

获取 BIOS 版本。

**返回值**: `string`

---

#### `os.biosReleaseDate()`

获取 BIOS 发布日期。

**返回值**: `string`

---

#### `os.gpuInfo()`

获取 GPU 信息。

**返回值**: `GpuInfo`

---

#### `os.diskUsage()`

获取磁盘使用情况。

**返回值**: `DiskUsage[]`

---

#### `os.networkStats()`

获取网络统计信息。

**返回值**: `NetworkStats[]`

---

## 使用示例

```typescript
import * as os from 'os';

// 系统信息概览
function getSystemInfo(): void {
  console.log('=== System Information ===');
  console.log(`Platform: ${os.platform()}`);
  console.log(`Type: ${os.type()}`);
  console.log(`Release: ${os.release()}`);
  console.log(`Architecture: ${os.arch()}`);
  console.log(`Hostname: ${os.hostname()}`);
  
  console.log('\n=== Memory ===');
  console.log(`Total: ${(os.totalmem() / 1024 / 1024 / 1024).toFixed(2)} GB`);
  console.log(`Free: ${(os.freemem() / 1024 / 1024 / 1024).toFixed(2)} GB`);
  console.log(`Used: ${((os.totalmem() - os.freemem()) / 1024 / 1024 / 1024).toFixed(2)} GB`);
  
  console.log('\n=== CPU ===');
  const cpus = os.cpus();
  console.log(`Count: ${cpus.length}`);
  console.log(`Model: ${cpus[0].model}`);
  console.log(`Speed: ${cpus[0].speed} MHz`);
  
  console.log('\n=== User ===');
  const user = os.userInfo();
  console.log(`Username: ${user.username}`);
  console.log(`Home: ${user.homedir}`);
  
  console.log('\n=== Uptime ===');
  const uptime = os.uptime();
  const days = Math.floor(uptime / 86400);
  const hours = Math.floor((uptime % 86400) / 3600);
  const minutes = Math.floor((uptime % 3600) / 60);
  console.log(`${days}d ${hours}h ${minutes}m`);
}

// 内存监控
function monitorMemory(): void {
  const total = os.totalmem();
  const free = os.freemem();
  const used = total - free;
  const percent = (used / total) * 100;
  
  console.log(`Memory usage: ${percent.toFixed(1)}%`);
  
  if (percent > 90) {
    console.warn('Warning: High memory usage!');
  }
}

// 跨平台路径处理
function getTempPath(filename: string): string {
  return os.tmpdir() + os.EOL + filename;
}

// 平台特定操作
function platformSpecific(): void {
  switch (os.platform()) {
    case 'win32':
      console.log('Windows specific code');
      break;
    case 'linux':
      console.log('Linux specific code');
      break;
    case 'darwin':
      console.log('macOS specific code');
      break;
    default:
      console.log('Unknown platform');
  }
}
```

## 实现细节

### 平台信息获取

- **Windows**: 使用 `GetVersionEx`, `GetSystemInfo`, `GlobalMemoryStatusEx` 等 API
- **Linux**: 使用 `/proc` 文件系统和 `uname` 系统调用
- **macOS**: 使用 `sysctl` 和 `uname` 系统调用

### 内存信息

内存信息通过系统 API 获取：
- Windows: `GlobalMemoryStatusEx`
- Linux: 读取 `/proc/meminfo`
- macOS: `sysctl hw.memsize`

### CPU 信息

CPU 信息通过以下方式获取：
- Windows: `GetSystemInfo`, `RegQueryValueEx`
- Linux: 读取 `/proc/cpuinfo`
- macOS: `sysctl hw.ncpu`, `sysctl machdep.cpu.brand_string`

## 平台差异

| 功能 | Windows | Linux | macOS |
|------|---------|-------|-------|
| 平台标识 | `'win32'` | `'linux'` | `'darwin'` |
| 行结束符 | `'\r\n'` | `'\n'` | `'\n'` |
| 空设备 | `'NUL'` | `'/dev/null'` | `'/dev/null'` |
| 负载平均 | 不支持 | 支持 | 支持 |
| CPU 温度 | 需要额外库 | 需要 root | 需要额外工具 |
