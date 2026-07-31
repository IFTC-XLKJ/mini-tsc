# readline 模块 - 命令行输入

## 概述

`readline` 模块提供了读取标准输入流的接口，支持逐行读取、用户交互、命令行提示等功能。

## 类型定义

```typescript
interface ReadLineOptions {
  input: ReadStream;           // 输入流
  output?: WriteStream;        // 输出流
  terminal?: boolean;          // 是否为终端
  historySize?: number;        // 历史记录大小
  removeHistoryDuplicates?: boolean;  // 移除重复历史
  prompt?: string;             // 提示符
  crlfDelay?: number;          // 换行延迟
  completer?: (line: string) => [string[], string];  // 自动补全
}

interface ReadLine {
  prompt(preserveCursor?: boolean): void;
  setPrompt(prompt: string): void;
  write(data: string, key?: Key): void;
  question(query: string, callback: (answer: string) => void): void;
  close(): void;
  pause(): void;
  resume(): void;
  getPrompt(): string;
  
  on(event: string, callback: Function): this;
  once(event: string, callback: Function): this;
  removeListener(event: string, callback: Function): this;
}

interface Key {
  sequence?: string;
  name?: string;
  ctrl?: boolean;
  meta?: boolean;
  shift?: boolean;
}
```

## API 列表

### `readline.createInterface(options)`

创建读行接口。

**参数**:
- `options`: 配置选项

**返回值**: `ReadLine` 对象

**示例**:
```typescript
import * as readline from 'readline';

const rl = readline.createInterface({
  input: process.stdin,
  output: process.stdout
});
```

---

### `rl.setPrompt(prompt)`

设置提示符。

**参数**:
- `prompt`: 提示符字符串

**返回值**: `void`

**示例**:
```typescript
rl.setPrompt('> ');
```

---

### `rl.getPrompt()`

获取当前提示符。

**返回值**: `string`

---

### `rl.prompt(preserveCursor?)`

显示提示符。

**参数**:
- `preserveCursor`: 是否保留光标位置

**返回值**: `void`

---

### `rl.question(query, callback)`

向用户提问并等待回答。

**参数**:
- `query`: 问题字符串
- `callback`: 回调函数，接收用户输入

**返回值**: `void`

**示例**:
```typescript
rl.question('What is your name? ', (name) => {
  console.log(`Hello, ${name}!`);
  rl.close();
});
```

---

### `rl.write(data, key?)`

写入数据到输出流。

**参数**:
- `data`: 要写入的数据
- `key`: 可选，按键信息

**返回值**: `void`

**示例**:
```typescript
// 写入普通文本
rl.write('Hello');

// 模拟按键
rl.write(null, { name: 'enter' });
rl.write(null, { name: 'backspace' });
```

---

### `rl.close()`

关闭读行接口。

**返回值**: `void`

---

### `rl.pause()`

暂停读行接口。

**返回值**: `void`

---

### `rl.resume()`

恢复读行接口。

**返回值**: `void`

---

## 事件

### ReadLine 事件

| 事件 | 描述 | 回调参数 |
|------|------|----------|
| `'close'` | 接口关闭 | 无 |
| `'line'` | 收到一行输入 | `line: string` |
| `'history'` | 历史记录变化 | `history: string[]` |
| `'pause'` | 接口暂停 | 无 |
| `'resume'` | 接口恢复 | 无 |
| `'SIGINT'` | 收到中断信号 (Ctrl+C) | 无 |
| `'SIGTSTP'` | 收到挂起信号 (Ctrl+Z) | 无 |
| `'SIGWINCH'` | 终端窗口大小变化 | 无 |

## 使用示例

### 基本交互

```typescript
import * as readline from 'readline';

const rl = readline.createInterface({
  input: process.stdin,
  output: process.stdout
});

rl.setPrompt('> ');
rl.prompt();

rl.on('line', (line) => {
  const trimmed = line.trim();
  
  if (trimmed === 'exit') {
    console.log('Goodbye!');
    rl.close();
    return;
  }
  
  console.log(`You said: ${trimmed}`);
  rl.prompt();
});

rl.on('close', () => {
  console.log('Interface closed');
  process.exit(0);
});
```

### 密码输入

```typescript
import * as readline from 'readline';

function askPassword(query: string): Promise<string> {
  return new Promise((resolve) => {
    const rl = readline.createInterface({
      input: process.stdin,
      output: process.stdout
    });
    
    // 隐藏输入
    process.stdout.write(query);
    
    const stdin = process.openStdin();
    process.stdin.on('data', (char) => {
      char = char.toString();
      
      // 处理退格键
      if (char === '\n' || char === '\r' || char === '\u0004') {
        stdin.removeAllListeners('data');
        resolve(password);
        process.stdout.write('\n');
        rl.close();
      } else if (char === '\u007F' || char === '\b') {
        if (password.length > 0) {
          password = password.slice(0, -1);
          process.stdout.write('\b \b');
        }
      } else {
        password += char;
        process.stdout.write('*');
      }
    });
    
    let password = '';
  });
}

async function main() {
  const password = await askPassword('Enter password: ');
  console.log('Password received (length:', password.length, ')');
}
```

### 带历史记录的命令行

```typescript
import * as readline from 'readline';

interface Command {
  name: string;
  description: string;
  handler: (args: string) => void;
}

class Shell {
  private rl: readline.Interface;
  private commands: Map<string, Command> = new Map();
  
  constructor() {
    this.rl = readline.createInterface({
      input: process.stdin,
      output: process.stdout,
      historySize: 100
    });
    
    this.registerCommand('help', 'Show available commands', () => {
      this.showHelp();
    });
    
    this.registerCommand('exit', 'Exit the shell', () => {
      console.log('Goodbye!');
      this.rl.close();
    });
  }
  
  registerCommand(name: string, description: string, handler: (args: string) => void): void {
    this.commands.set(name, { name, description, handler });
  }
  
  private showHelp(): void {
    console.log('\nAvailable commands:');
    for (const [name, cmd] of this.commands) {
      console.log(`  ${name.padEnd(15)} ${cmd.description}`);
    }
    console.log('');
  }
  
  start(): void {
    console.log('Type "help" for available commands.\n');
    this.rl.setPrompt('shell> ');
    this.rl.prompt();
    
    this.rl.on('line', (line) => {
      const trimmed = line.trim();
      
      if (!trimmed) {
        this.rl.prompt();
        return;
      }
      
      const parts = trimmed.split(/\s+/);
      const cmdName = parts[0].toLowerCase();
      const args = parts.slice(1).join(' ');
      
      const cmd = this.commands.get(cmdName);
      if (cmd) {
        cmd.handler(args);
      } else {
        console.log(`Unknown command: ${cmdName}`);
      }
      
      this.rl.prompt();
    });
    
    this.rl.on('close', () => {
      process.exit(0);
    });
  }
}

// 使用示例
const shell = new Shell();

shell.registerCommand('echo', 'Echo a message', (args) => {
  console.log(args);
});

shell.registerCommand('date', 'Show current date/time', () => {
  console.log(new Date().toISOString());
});

shell.start();
```

### 自动补全

```typescript
import * as readline from 'readline';

const commands = ['help', 'exit', 'list', 'create', 'delete', 'update'];

function completer(line: string): [string[], string] {
  const hits = commands.filter(cmd => cmd.startsWith(line.toLowerCase()));
  
  // 如果只有一个匹配，直接补全
  if (hits.length === 1) {
    return [[hits[0]], line];
  }
  
  // 返回所有匹配项
  return [hits.length ? hits : commands, line];
}

const rl = readline.createInterface({
  input: process.stdin,
  output: process.stdout,
  completer
});

rl.setPrompt('shell> ');
rl.prompt();

rl.on('line', (line) => {
  console.log(`Executing: ${line}`);
  rl.prompt();
});
```

### 菜单选择

```typescript
import * as readline from 'readline';

interface MenuItem {
  label: string;
  value: string;
}

async function showMenu(title: string, items: MenuItem[]): Promise<string> {
  return new Promise((resolve) => {
    const rl = readline.createInterface({
      input: process.stdin,
      output: process.stdout
    });
    
    console.log(`\n${title}`);
    console.log('='.repeat(title.length));
    
    items.forEach((item, index) => {
      console.log(`${index + 1}. ${item.label}`);
    });
    
    console.log('');
    
    const ask = () => {
      rl.question('Select an option (number): ', (answer) => {
        const index = parseInt(answer) - 1;
        
        if (index >= 0 && index < items.length) {
          rl.close();
          resolve(items[index].value);
        } else {
          console.log('Invalid option. Please try again.');
          ask();
        }
      });
    };
    
    ask();
  });
}

// 使用示例
async function main() {
  const action = await showMenu('What do you want to do?', [
    { label: 'Create new file', value: 'create' },
    { label: 'Edit existing file', value: 'edit' },
    { label: 'Delete file', value: 'delete' },
    { label: 'Exit', value: 'exit' }
  ]);
  
  console.log(`Selected action: ${action}`);
}
```

## 实现细节

### 键盘输入处理

readline 模块处理键盘输入的特殊字符：
- `Ctrl+C`: SIGINT 信号
- `Ctrl+D`: EOF（文件结束）
- `Ctrl+Z`: SIGTSTP 信号（Windows）
- 退格键: 删除前一个字符
- 方向键: 历史记录导航

### 历史记录

- 维护输入历史数组
- 支持上下箭头键导航
- 可配置历史记录大小
- 可选的重复项移除

### 自动补全

通过 `completer` 函数实现：
1. 接收当前输入行
2. 返回匹配的补全项和当前行
3. 如果只有一个匹配，直接补全
4. 如果有多个匹配，显示所有选项

## 平台差异

| 功能 | Windows | Linux/macOS |
|------|---------|-------------|
| 特殊字符处理 | 控制台 API | termios |
| 信号处理 | 有限支持 | 完整支持 |
| 终端检测 | `isatty()` | `isatty()` |

## 注意事项

1. **资源清理**: 使用完毕后调用 `close()` 释放资源
2. **信号处理**: 监听 SIGINT 事件处理用户中断
3. **异步操作**: `question()` 是异步的，需要使用回调或 Promise
4. **编码**: 确保输入输出流使用正确的编码
