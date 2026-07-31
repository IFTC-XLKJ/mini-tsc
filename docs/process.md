# process 模块 - 进程信息

## 概述

`process` 模块提供了当前进程的信息和控制功能，包括环境变量、命令行参数、工作目录、标准输入输出等。

## API 列表

### 属性和方法

#### `process.argv`

获取命令行参数数组。

**返回值**: `string[]`

**示例**:
```typescript
import * as process from 'process';

// 运行: ./myapp arg1 arg2
console.log(process.argv);
// ["./myapp", "arg1", "arg2"]
```

---

#### `process.env`

获取环境变量对象。

**返回值**: `Record<string, string>`

**示例**:
```typescript
const home = process.env.HOME;
const path = process.env.PATH;
console.log(`Home: ${home}`);
```

---

#### `process.cwd()`

获取当前工作目录。

**返回值**: `string`

**示例**:
```typescript
console.log(`Current directory: ${process.cwd()}`);
```

---

#### `process.chdir(dir)`

切换当前工作目录。

**参数**:
- `dir`: 目标目录

**返回值**: `void`

**示例**:
```typescript
process.chdir('/tmp');
console.log(process.cwd()); // "/tmp"
```

---

#### `process.exit(code?)`

终止进程。

**参数**:
- `code`: 退出码，默认 0

**返回值**: `void`

**示例**:
```typescript
process.exit(0);   // 成功退出
process.exit(1);   // 错误退出
```

---

#### `process.pid`

获取当前进程 ID。

**返回值**: `number`

**示例**:
```typescript
console.log(`PID: ${process.pid}`);
```

---

### 标准输入输出

#### `process.stdin`

标准输入流。

**类型**: `ReadStream`

**示例**:
```typescript
process.stdin.on('data', (data) => {
  console.log(`Received: ${data}`);
});
```

---

#### `process.stdout`

标准输出流。

**类型**: `WriteStream`

**示例**:
```typescript
process.stdout.write('Hello ');
process.stdout.write('World\n');
```

---

#### `process.stderr`

标准错误流。

**类型**: `WriteStream`

**示例**:
```typescript
process.stderr.write('Error: something went wrong\n');
```

---

### WriteStream 方法

#### `stream.write(data)`

写入数据到流。

**参数**:
- `data`: 要写入的数据

**返回值**: `void`

---

#### `stream.on(event, callback)`

监听流事件。

**参数**:
- `event`: 事件名称
- `callback`: 回调函数

**支持的事件**:
- `'data'`: 数据到达
- `'end'`: 流结束
- `'error'`: 发生错误

---

### 终端控制方法

#### `stdout.cursorTo(x, y?)`

移动光标到指定位置。

**参数**:
- `x`: 列号
- `y`: 行号（可选）

---

#### `stdout.moveCursor(dx, dy)`

相对移动光标。

**参数**:
- `dx`: 水平移动量
- `dy`: 垂直移动量

---

#### `stdout.clearScreenDown()`

清除光标下方的内容。

---

#### `stdout.clearLine(dir)`

清除当前行。

**参数**:
- `dir`: 清除方向
  - `-1`: 清除左侧
  - `0`: 清除整行
  - `1`: 清除右侧

---

#### `stderr.cursorTo(x, y?)`

移动 stderr 光标。

---

#### `stderr.moveCursor(dx, dy)`

相对移动 stderr 光标。

---

#### `stderr.clearScreenDown()`

清除 stderr 光标下方内容。

---

#### `stderr.clearLine(dir)`

清除 stderr 当前行。

---

## 使用示例

```typescript
import * as process from 'process';

// 解析命令行参数
function parseArgs(): Record<string, string> {
  const args: Record<string, string> = {};
  const argv = process.argv.slice(2); // 跳过程序名和脚本名
  
  for (let i = 0; i < argv.length; i++) {
    if (argv[i].startsWith('--')) {
      const key = argv[i].slice(2);
      const value = argv[i + 1];
      args[key] = value;
      i++;
    }
  }
  
  return args;
}

// 进度条显示
function showProgress(percent: number): void {
  const width = 40;
  const filled = Math.floor(width * percent / 100);
  const empty = width - filled;
  
  const bar = '█'.repeat(filled) + '░'.repeat(empty);
  process.stdout.write(`\r[${bar}] ${percent}%`);
}

// 命令行交互
async function askQuestion(question: string): Promise<string> {
  return new Promise((resolve) => {
    process.stdout.write(question);
    process.stdin.on('data', (data) => {
      resolve(data.toString().trim());
    });
  });
}

// 环境配置
function getConfig(): Record<string, string> {
  return {
    NODE_ENV: process.env.NODE_ENV || 'development',
    PORT: process.env.PORT || '3000',
    DEBUG: process.env.DEBUG || 'false',
  };
}
```

## 实现细节

### 命令行参数捕获

在 `main()` 函数中通过 `node_process_set_argv(argc, argv)` 捕获命令行参数：

```c
int main(int argc, char* argv[]) {
  node_process_set_argv(argc, argv);
  // ...
}
```

### 标准流实现

标准流使用文件描述符实现：
- `stdin`: 文件描述符 0
- `stdout`: 文件描述符 1
- `stderr`: 文件描述符 2

### 终端控制

终端控制功能使用平台特定的 API：
- Windows: `conio.h` 和 Windows Console API
- Linux/macOS: `termios.h` 和 ANSI 转义序列

## 平台差异

| 功能 | Windows | Linux/macOS |
|------|---------|-------------|
| 环境变量访问 | `GetEnvironmentVariable` | `getenv` |
| 终端控制 | Windows Console API | ANSI 转义序列 |
| 进程 ID | `GetCurrentProcessId` | `getpid` |

## 注意事项

1. `process.exit()` 会立即终止进程，不会执行后续代码
2. 标准流是同步的，写入操作会阻塞
3. 环境变量在进程启动时就已确定，运行时修改不会影响系统环境
