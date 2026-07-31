# child_process 模块 - 子进程管理

## 概述

`child_process` 模块提供了创建和管理子进程的功能，支持执行系统命令、运行外部程序等。

## 类型定义

```typescript
interface SpawnOptions {
  cwd?: string;           // 工作目录
  env?: Record<string, string>;  // 环境变量
  stdio?: 'pipe' | 'ignore' | 'inherit' | [string, string, string];
}

interface ExecOptions {
  cwd?: string;
  env?: Record<string, string>;
  timeout?: number;
  maxBuffer?: number;
}

interface ChildProcess {
  pid: number;
  stdin: WriteStream;
  stdout: ReadStream;
  stderr: ReadStream;
  
  on(event: string, callback: Function): void;
  once(event: string, callback: Function): void;
  
  kill(signal?: string): void;
}

interface ExecResult {
  stdout: string;
  stderr: string;
}
```

## API 列表

### `child_process.execSync(command, options?)`

同步执行命令。

**参数**:
- `command`: 要执行的命令
- `options`: 可选配置

**返回值**: `string` - 命令输出

**示例**:
```typescript
import { execSync } from 'child_process';

// 执行命令并获取输出
const output = execSync('dir');
console.log(output);

// 指定工作目录
const gitLog = execSync('git log --oneline -10', { cwd: '/path/to/repo' });
```

---

### `child_process.spawn(command, args?, options?)`

创建子进程。

**参数**:
- `command`: 要执行的命令
- `args`: 命令参数数组
- `options`: 配置选项

**返回值**: `ChildProcess` 对象

**示例**:
```typescript
import { spawn } from 'child_process';

const child = spawn('ls', ['-la']);

child.stdout.on('data', (data) => {
  console.log('Output:', data.toString());
});

child.stderr.on('data', (data) => {
  console.error('Error:', data.toString());
});

child.on('close', (code) => {
  console.log(`Process exited with code ${code}`);
});
```

---

### `child_process.exec(command, options?, callback?)`

异步执行命令。

**参数**:
- `command`: 要执行的命令
- `options`: 配置选项
- `callback`: 回调函数 `(error, stdout, stderr) => void`

**返回值**: `ChildProcess` 对象

**示例**:
```typescript
import { exec } from 'child_process';

exec('echo "Hello World"', (error, stdout, stderr) => {
  if (error) {
    console.error('Error:', error);
    return;
  }
  console.log('Output:', stdout);
});
```

---

### `child_process.execFile(file, args?, options?, callback?)`

执行文件。

**参数**:
- `file`: 可执行文件路径
- `args`: 参数数组
- `options`: 配置选项
- `callback`: 回调函数

**返回值**: `ChildProcess` 对象

---

### `child_process.fork(modulePath, args?, options?)`

创建新的 Node.js 进程。

**参数**:
- `modulePath`: 模块路径
- `args`: 参数数组
- `options`: 配置选项

**返回值**: `ChildProcess` 对象

---

## 使用示例

### 执行系统命令

```typescript
import { execSync } from 'child_process';
import * as os from 'os';

function getSystemInfo(): Record<string, string> {
  const info: Record<string, string> = {};
  
  // 获取主机名
  info.hostname = os.hostname();
  
  // 获取 IP 地址
  if (os.platform() === 'win32') {
    info.ip = execSync('ipconfig | findstr /i "IPv4"').toString().trim();
  } else {
    info.ip = execSync("ifconfig | grep 'inet ' | grep -v '127.0.0.1' | awk '{print $2}'").toString().trim();
  }
  
  return info;
}

// 获取 Git 信息
function getGitInfo(): Record<string, string> {
  try {
    const branch = execSync('git branch --show-current').toString().trim();
    const commit = execSync('git rev-parse --short HEAD').toString().trim();
    const status = execSync('git status --porcelain').toString().trim();
    
    return {
      branch,
      commit,
      dirty: status.length > 0 ? 'true' : 'false'
    };
  } catch {
    return { branch: 'unknown', commit: 'unknown', dirty: 'unknown' };
  }
}
```

### 流式处理输出

```typescript
import { spawn } from 'child_process';

function streamCommand(command: string, args: string[]): Promise<void> {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args);
    
    child.stdout.on('data', (data) => {
      console.log('[STDOUT]', data.toString());
    });
    
    child.stderr.on('data', (data) => {
      console.error('[STDERR]', data.toString());
    });
    
    child.on('close', (code) => {
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(`Process exited with code ${code}`));
      }
    });
    
    child.on('error', reject);
  });
}

// 使用示例
async function main() {
  // 列出文件
  await streamCommand('ls', ['-la']);
  
  // 查看 Git 日志
  await streamCommand('git', ['log', '--oneline', '-5']);
}
```

### 进程间通信

```typescript
import { fork } from 'child_process';

// 主进程
function startWorker(): void {
  const worker = fork('./worker.js');
  
  // 发送消息给子进程
  worker.send({ type: 'start', data: { taskId: 123 } });
  
  // 接收子进程消息
  worker.on('message', (message) => {
    console.log('Worker message:', message);
    
    if (message.type === 'complete') {
      console.log('Task completed!');
    }
  });
  
  worker.on('exit', (code) => {
    console.log(`Worker exited with code ${code}`);
  });
}

// worker.js
process.on('message', (message) => {
  console.log('Received:', message);
  
  // 模拟处理
  setTimeout(() => {
    process.send({ type: 'complete', result: 'done' });
    process.exit(0);
  }, 1000);
});
```

### 批量执行

```typescript
import { execSync } from 'child_process';
import * as path from 'path';

interface TestResult {
  file: string;
  passed: boolean;
  output: string;
}

function runTests(testDir: string): TestResult[] {
  const results: TestResult[] = [];
  
  // 查找所有测试文件
  const files = execSync(`find ${testDir} -name "*.test.ts"`)
    .toString()
    .trim()
    .split('\n')
    .filter(f => f);
  
  for (const file of files) {
    try {
      const output = execSync(`npx ts-node ${file}`, {
        timeout: 30000
      }).toString();
      
      results.push({
        file: path.basename(file),
        passed: true,
        output
      });
    } catch (error: any) {
      results.push({
        file: path.basename(file),
        passed: false,
        output: error.stdout?.toString() || error.message
      });
    }
  }
  
  return results;
}

// 打印测试结果
function printResults(results: TestResult[]): void {
  console.log('\n=== Test Results ===');
  
  for (const result of results) {
    const status = result.passed ? '✓' : '✗';
    console.log(`${status} ${result.file}`);
    
    if (!result.passed) {
      console.log(`  ${result.output.split('\n')[0]}`);
    }
  }
  
  const passed = results.filter(r => r.passed).length;
  console.log(`\n${passed}/${results.length} tests passed`);
}
```

## 事件

### ChildProcess 事件

| 事件 | 描述 | 回调参数 |
|------|------|----------|
| `'close'` | 进程关闭 | `code: number, signal: string` |
| `'disconnect'` | 断开连接 | 无 |
| `'error'` | 发生错误 | `err: Error` |
| `'exit'` | 进程退出 | `code: number, signal: string` |
| `'message'` | 收到消息 | `message: any, sendHandle: any` |
| `'spawn'` | 进程创建 | 无 |

## 实现细节

### 平台差异

| 功能 | Windows | Linux/macOS |
|------|---------|-------------|
| 创建进程 | `CreateProcess` | `fork` + `exec` |
| 管道 | 匿名管道 | `pipe()` |
| 信号 | 不支持 | 支持 |
| 进程组 | 有限支持 | 完整支持 |

### 进程创建

1. **Windows**: 使用 `CreateProcess` API
2. **Linux/macOS**: 使用 `fork()` + `exec()` 系列函数

### I/O 重定向

子进程的 I/O 可以通过以下方式重定向：
- `'pipe'`: 创建管道（默认）
- `'ignore'`: 忽略输出
- `'inherit'`: 继承父进程的 I/O
- 自定义文件描述符

## 注意事项

1. **命令注入风险**: 避免直接拼接用户输入到命令中
2. **资源清理**: 及时关闭不再需要的子进程
3. **超时设置**: 为长时间运行的命令设置超时
4. **错误处理**: 始终处理可能的错误情况

## 安全建议

```typescript
// 不安全 - 避免
const userInput = 'file.txt; rm -rf /';
execSync(`cat ${userInput}`);

// 安全 - 使用参数数组
spawn('cat', [userInput]);

// 安全 - 验证输入
if (/^[a-zA-Z0-9._-]+$/.test(userInput)) {
  execSync(`cat ${userInput}`);
}
```
