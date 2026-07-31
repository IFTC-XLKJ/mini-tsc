# chalk 模块 - 终端颜色和样式

## 概述

`chalk` 模块提供了终端文本颜色和样式功能，可以美化命令行输出。

## 类型定义

```typescript
type ColorName = 
  | 'black' | 'red' | 'green' | 'yellow' | 'blue' | 'magenta' | 'cyan' | 'white'
  | 'gray' | 'grey'
  | 'redBright' | 'greenBright' | 'yellowBright' | 'blueBright' | 'magentaBright' | 'cyanBright' | 'whiteBright';

type StyleName = 
  | 'reset' | 'bold' | 'dim' | 'italic' | 'underline' | 'inverse' | 'hidden' | 'strikethrough'
  | 'visible';

type ChalkFn = (text: string) => string;

interface Chalk {
  // 颜色
  black: ChalkFn;
  red: ChalkFn;
  green: ChalkFn;
  yellow: ChalkFn;
  blue: ChalkFn;
  magenta: ChalkFn;
  cyan: ChalkFn;
  white: ChalkFn;
  gray: ChalkFn;
  grey: ChalkFn;
  redBright: ChalkFn;
  greenBright: ChalkFn;
  yellowBright: ChalkFn;
  blueBright: ChalkFn;
  magentaBright: ChalkFn;
  cyanBright: ChalkFn;
  whiteBright: ChalkFn;
  
  // 样式
  reset: ChalkFn;
  bold: ChalkFn;
  dim: ChalkFn;
  italic: ChalkFn;
  underline: ChalkFn;
  inverse: ChalkFn;
  hidden: ChalkFn;
  strikethrough: ChalkFn;
  visible: ChalkFn;
  
  // 组合使用
  (text: string): string;
}
```

## API 列表

### 颜色函数

#### `chalk.black(text)`

将文本设置为黑色。

**参数**:
- `text`: 要着色的文本

**返回值**: `string` - 带有 ANSI 颜色代码的文本

**示例**:
```typescript
import chalk from 'chalk';

console.log(chalk.black('Black text'));
```

---

#### `chalk.red(text)`

将文本设置为红色。

**参数**:
- `text`: 要着色的文本

**返回值**: `string`

**示例**:
```typescript
console.log(chalk.red('Error: Something went wrong'));
```

---

#### `chalk.green(text)`

将文本设置为绿色。

**参数**:
- `text`: 要着色的文本

**返回值**: `string`

**示例**:
```typescript
console.log(chalk.green('Success!'));
```

---

#### `chalk.yellow(text)`

将文本设置为黄色。

**参数**:
- `text`: 要着色的文本

**返回值**: `string`

**示例**:
```typescript
console.log(chalk.yellow('Warning: Be careful'));
```

---

#### `chalk.blue(text)`

将文本设置为蓝色。

**参数**:
- `text`: 要着色的文本

**返回值**: `string`

**示例**:
```typescript
console.log(chalk.blue('Info: Processing'));
```

---

#### `chalk.magenta(text)`

将文本设置为品红色。

**参数**:
- `text`: 要着色的文本

**返回值**: `string`

---

#### `chalk.cyan(text)`

将文本设置为青色。

**参数**:
- `text`: 要着色的文本

**返回值**: `string`

---

#### `chalk.white(text)`

将文本设置为白色。

**参数**:
- `text`: 要着色的文本

**返回值**: `string`

---

#### `chalk.gray(text)` / `chalk.grey(text)`

将文本设置为灰色。

**参数**:
- `text`: 要着色的文本

**返回值**: `string`

---

### 亮色函数

亮色函数提供更明亮的颜色变体。

#### `chalk.redBright(text)`

将文本设置为亮红色。

**参数**:
- `text`: 要着色的文本

**返回值**: `string`

---

#### `chalk.greenBright(text)`

将文本设置为亮绿色。

---

#### `chalk.yellowBright(text)`

将文本设置为亮黄色。

---

#### `chalk.blueBright(text)`

将文本设置为亮蓝色。

---

#### `chalk.magentaBright(text)`

将文本设置为亮品红色。

---

#### `chalk.cyanBright(text)`

将文本设置为亮青色。

---

#### `chalk.whiteBright(text)`

将文本设置为亮白色。

---

### 样式函数

#### `chalk.bold(text)`

将文本设置为粗体。

**参数**:
- `text`: 要样式化的文本

**返回值**: `string`

**示例**:
```typescript
console.log(chalk.bold('Bold text'));
```

---

#### `chalk.dim(text)`

将文本设置为暗淡。

**参数**:
- `text`: 要样式化的文本

**返回值**: `string`

---

#### `chalk.italic(text)`

将文本设置为斜体。

**参数**:
- `text`: 要样式化的文本

**返回值**: `string`

**示例**:
```typescript
console.log(chalk.italic('Italic text'));
```

---

#### `chalk.underline(text)`

为文本添加下划线。

**参数**:
- `text`: 要样式化的文本

**返回值**: `string`

**示例**:
```typescript
console.log(chalk.underline('Underlined text'));
```

---

#### `chalk.inverse(text)`

反转文本颜色和背景色。

**参数**:
- `text`: 要样式化的文本

**返回值**: `string`

---

#### `chalk.hidden(text)`

隐藏文本。

**参数**:
- `text`: 要隐藏的文本

**返回值**: `string`

---

#### `chalk.strikethrough(text)`

为文本添加删除线。

**参数**:
- `text`: 要样式化的文本

**返回值**: `string`

---

#### `chalk.visible(text)`

仅在支持颜色的终端显示文本。

**参数**:
- `text`: 要显示的文本

**返回值**: `string`

---

### 链式调用

可以链式组合多个样式和颜色：

```typescript
import chalk from 'chalk';

// 粗体红色文本
console.log(chalk.bold.red('Bold red text'));

// 下划线绿色文本
console.log(chalk.underline.green('Underlined green text'));

// 粗体斜体黄色文本
console.log(chalk.bold.italic.yellow('Bold italic yellow text'));
```

### 嵌套使用

```typescript
import chalk from 'chalk';

// 嵌套样式
console.log(
  chalk.red('Red ') +
  chalk.green('Green ') +
  chalk.blue('Blue')
);

// 使用模板字符串
console.log(`${chalk.bold('Bold')} and ${chalk.dim('dim')}`);
```

## 使用示例

### 日志格式化

```typescript
import chalk from 'chalk';

function log(level: 'info' | 'warn' | 'error' | 'success', message: string): void {
  const timestamp = new Date().toISOString();
  
  switch (level) {
    case 'info':
      console.log(`${chalk.blue('INFO')} ${chalk.gray(timestamp)} ${message}`);
      break;
    case 'warn':
      console.log(`${chalk.yellow('WARN')} ${chalk.gray(timestamp)} ${message}`);
      break;
    case 'error':
      console.log(`${chalk.red('ERROR')} ${chalk.gray(timestamp)} ${message}`);
      break;
    case 'success':
      console.log(`${chalk.green('SUCCESS')} ${chalk.gray(timestamp)} ${message}`);
      break;
  }
}

// 使用示例
log('info', 'Server started on port 3000');
log('warn', 'High memory usage detected');
log('error', 'Failed to connect to database');
log('success', 'Deployment completed');
```

### 进度条

```typescript
import chalk from 'chalk';

function showProgress(current: number, total: number, width: number = 40): void {
  const percent = Math.floor((current / total) * 100);
  const filled = Math.floor((current / total) * width);
  const empty = width - filled;
  
  const bar = chalk.green('█'.repeat(filled)) + chalk.gray('░'.repeat(empty));
  
  process.stdout.write(`\r[${bar}] ${percent}%`);
  
  if (current === total) {
    console.log('');
  }
}

// 使用示例
let progress = 0;
const interval = setInterval(() => {
  progress += 5;
  showProgress(progress, 100);
  
  if (progress >= 100) {
    clearInterval(interval);
  }
}, 100);
```

### 表格输出

```typescript
import chalk from 'chalk';

interface Column {
  header: string;
  width: number;
}

function printTable<T extends Record<string, any>>(
  columns: Column[],
  data: T[]
): void {
  // 打印表头
  const header = columns
    .map(col => col.header.padEnd(col.width))
    .join(' | ');
  
  console.log(chalk.bold(header));
  console.log(chalk.gray('-'.repeat(header.length)));
  
  // 打印数据行
  for (const row of data) {
    const line = columns
      .map(col => String(row[col.header] || '').padEnd(col.width))
      .join(' | ');
    
    console.log(line);
  }
}

// 使用示例
const columns: Column[] = [
  { header: 'Name', width: 20 },
  { header: 'Age', width: 10 },
  { header: 'Email', width: 30 }
];

const data = [
  { Name: 'Alice', Age: 30, Email: 'alice@example.com' },
  { Name: 'Bob', Age: 25, Email: 'bob@example.com' },
  { Name: 'Charlie', Age: 35, Email: 'charlie@example.com' }
];

printTable(columns, data);
```

### 彩色菜单

```typescript
import chalk from 'chalk';
import * as readline from 'readline';

function showMenu(): void {
  console.log('\n' + chalk.bold.cyan('=== Main Menu ==='));
  console.log('');
  console.log(`${chalk.green('1.')} ${chalk.white('View files')}`);
  console.log(`${chalk.green('2.')} ${chalk.white('Create new file')}`);
  console.log(`${chalk.green('3.')} ${chalk.white('Edit file')}`);
  console.log(`${chalk.green('4.')} ${chalk.white('Delete file')}`);
  console.log(`${chalk.red('5.')} ${chalk.white('Exit')}`);
  console.log('');
}

const rl = readline.createInterface({
  input: process.stdin,
  output: process.stdout
});

showMenu();
rl.question(chalk.yellow('Select an option: '), (answer) => {
  console.log(`\nYou selected: ${chalk.bold(answer)}`);
  rl.close();
});
```

### 错误和成功消息

```typescript
import chalk from 'chalk';

function success(message: string): void {
  console.log(chalk.green('✓ ') + message);
}

function error(message: string): void {
  console.log(chalk.red('✗ ') + message);
}

function warning(message: string): void {
  console.log(chalk.yellow('⚠ ') + message);
}

function info(message: string): void {
  console.log(chalk.blue('ℹ ') + message);
}

// 使用示例
success('File saved successfully');
error('Failed to read file');
warning('Disk space low');
info('Processing your request...');
```

## ANSI 转义码

chalk 模块使用 ANSI 转义码实现颜色和样式：

### 前景色代码

| 颜色 | 代码 |
|------|------|
| 黑色 | `\x1b[30m` |
| 红色 | `\x1b[31m` |
| 绿色 | `\x1b[32m` |
| 黄色 | `\x1b[33m` |
| 蓝色 | `\x1b[34m` |
| 品红 | `\x1b[35m` |
| 青色 | `\x1b[36m` |
| 白色 | `\x1b[37m` |

### 背景色代码

| 颜色 | 代码 |
|------|------|
| 黑色背景 | `\x1b[40m` |
| 红色背景 | `\x1b[41m` |
| 绿色背景 | `\x1b[42m` |
| 黄色背景 | `\x1b[43m` |
| 蓝色背景 | `\x1b[44m` |
| 品红背景 | `\x1b[45m` |
| 青色背景 | `\x1b[46m` |
| 白色背景 | `\x1b[47m` |

### 样式代码

| 样式 | 代码 |
|------|------|
| 重置 | `\x1b[0m` |
| 粗体 | `\x1b[1m` |
| 暗淡 | `\x1b[2m` |
| 斜体 | `\x1b[3m` |
| 下划线 | `\x1b[4m` |
| 反转 | `\x1b[7m` |
| 隐藏 | `\x1b[8m` |
| 删除线 | `\x1b[9m` |

## 平台兼容性

| 平台 | 支持程度 |
|------|----------|
| Windows Terminal | 完整支持 |
| Windows CMD | 部分支持（需要启用 ANSI 支持） |
| macOS Terminal | 完整支持 |
| Linux Terminal | 完整支持 |
| VS Code 终端 | 完整支持 |

### Windows CMD ANSI 支持

在 Windows CMD 中，需要启用 ANSI 支持：

```typescript
// 启用 Windows 10+ ANSI 支持
const { execSync } = require('child_process');
try {
  execSync('reg add HKCU\\Console /v VirtualTerminalLevel /t REG_DWORD /d 1 /f');
} catch {
  // 忽略错误
}
```

## 性能考虑

1. **避免过多样式**: 过多的颜色和样式会影响可读性
2. **缓存结果**: 如果多次使用相同样式，可以缓存结果
3. **检测终端支持**: 在不支持颜色的终端禁用样式

```typescript
import chalk from 'chalk;

// 检测终端是否支持颜色
if (!process.stdout.isTTY) {
  chalk.level = 0; // 禁用颜色
}
```
