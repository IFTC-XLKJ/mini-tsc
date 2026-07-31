# fs 模块 - 文件系统操作

## 概述

`fs` 模块提供了文件系统相关的操作，包括文件读写、目录操作、文件属性查询等。支持同步和异步两种操作模式。

## 类型定义

```typescript
interface Stats {
  size: number;        // 文件大小（字节）
  mtime: number;       // 修改时间（毫秒时间戳）
  isFile: boolean;     // 是否为文件
  isDirectory: boolean; // 是否为目录
}

type FileData = string | Buffer;

interface ReadFileOptions {
  encoding?: string;   // 编码格式，如 'utf-8'
}
```

## API 列表

### 同步函数

#### `readFileSync(path, options?)`

读取文件内容。

**参数**:
- `path`: 文件路径
- `options`: 可选，指定编码返回字符串，否则返回 Buffer

**返回值**: `string` 或 `Buffer`

**示例**:
```typescript
import * as fs from 'fs';

// 返回 Buffer
const buffer = fs.readFileSync('data.bin');

// 返回字符串
const text = fs.readFileSync('file.txt', 'utf-8');
```

**异常**: 文件不存在时抛出异常

---

#### `writeFileSync(path, data, options?)`

写入文件内容。

**参数**:
- `path`: 文件路径
- `data`: 要写入的数据
- `options`: 可选

**返回值**: `void`

**示例**:
```typescript
fs.writeFileSync('output.txt', 'Hello World');
fs.writeFileSync('data.json', JSON.stringify({ key: 'value' }));
```

---

#### `existsSync(path)`

检查文件是否存在。

**参数**:
- `path`: 文件路径

**返回值**: `boolean`

**示例**:
```typescript
if (fs.existsSync('config.json')) {
  const config = fs.readFileSync('config.json', 'utf-8');
}
```

---

#### `mkdirSync(path, options?)`

创建目录。

**参数**:
- `path`: 目录路径
- `options`: 可选

**返回值**: `void`

**示例**:
```typescript
fs.mkdirSync('new-folder');
```

---

#### `readdirSync(path)`

读取目录内容。

**参数**:
- `path`: 目录路径

**返回值**: `string[]` - 文件和子目录名称数组

**示例**:
```typescript
const files = fs.readdirSync('./src');
console.log(files); // ['index.ts', 'utils.ts', ...]
```

---

#### `unlinkSync(path)`

删除文件。

**参数**:
- `path`: 文件路径

**返回值**: `void`

**示例**:
```typescript
fs.unlinkSync('temp.txt');
```

---

#### `statSync(path)`

获取文件信息。

**参数**:
- `path`: 文件路径

**返回值**: `Stats` 对象

**示例**:
```typescript
const stats = fs.statSync('file.txt');
console.log(stats.size);      // 文件大小
console.log(stats.isFile());  // true
```

---

#### `rmdirSync(path)`

删除空目录。

**参数**:
- `path`: 目录路径

**返回值**: `void`

---

#### `renameSync(oldPath, newPath)`

重命名文件或目录。

**参数**:
- `oldPath`: 原路径
- `newPath`: 新路径

**返回值**: `void`

---

#### `readlinkSync(path)`

读取符号链接目标。

**参数**:
- `path`: 符号链接路径

**返回值**: `string` - 目标路径

---

#### `symlinkSync(target, path)`

创建符号链接。

**参数**:
- `target`: 目标路径
- `path`: 符号链接路径

**返回值**: `void`

---

#### `chmodSync(path, mode)`

修改文件权限。

**参数**:
- `path`: 文件路径
- `mode`: 权限模式（八进制）

**返回值**: `void`

---

### 异步函数

异步函数返回 Promise，可以配合 `await` 使用。

#### `readFile(path, options?)`

异步读取文件。

**返回值**: `Promise<string | Buffer>`

**示例**:
```typescript
const content = await fs.readFile('file.txt', 'utf-8');
```

---

#### `writeFile(path, data, options?)`

异步写入文件。

**返回值**: `Promise<boolean>`

**示例**:
```typescript
await fs.writeFile('output.txt', 'Hello');
```

---

#### `access(path, mode?)`

检查文件是否可访问。

**返回值**: `Promise<boolean>`

---

#### `mkdir(path, options?)`

异步创建目录。

**返回值**: `Promise<boolean>`

---

#### `readdir(path)`

异步读取目录。

**返回值**: `Promise<string[]>`

---

#### `unlink(path)`

异步删除文件。

**返回值**: `Promise<boolean>`

---

#### `stat(path)`

异步获取文件信息。

**返回值**: `Promise<Stats>`

---

#### `rmdir(path)`

异步删除目录。

**返回值**: `Promise<boolean>`

---

#### `rename(oldPath, newPath)`

异步重命名。

**返回值**: `Promise<boolean>`

---

#### `readlink(path)`

异步读取符号链接。

**返回值**: `Promise<string>`

---

#### `symlink(target, path)`

异步创建符号链接。

**返回值**: `Promise<boolean>`

---

#### `chmod(path, mode)`

异步修改权限。

**返回值**: `Promise<boolean>`

---

## 实现细节

### 平台兼容性

| 操作 | Windows | Linux/macOS |
|------|---------|-------------|
| 文件读写 | `_fopen` | `fopen` |
| 目录创建 | `_mkdir` | `mkdir` |
| 文件删除 | `_unlink` | `unlink` |
| 目录删除 | `_rmdir` | `rmdir` |
| 符号链接 | `CreateSymbolicLinkA` | `symlink` |

### 异步实现

异步函数使用线程池（Thread Pool）实现：

1. 主线程创建 Promise 和任务结构体
2. 任务提交到线程池执行
3. 完成后在主线程回调中 resolve/reject Promise

### 编码处理

- 无 encoding 选项：返回 `Buffer` 对象
- 有 encoding 选项（如 `'utf-8'`）：返回 `TSString*`

## 使用示例

```typescript
import * as fs from 'fs';
import * as path from 'path';

// 同步读取配置
function loadConfig(): any {
  const configPath = path.join(process.cwd(), 'config.json');
  if (!fs.existsSync(configPath)) {
    return {};
  }
  const content = fs.readFileSync(configPath, 'utf-8');
  return JSON.parse(content);
}

// 异步复制文件
async function copyFile(src: string, dest: string): Promise<void> {
  const content = await fs.readFile(src);
  await fs.writeFile(dest, content);
}

// 遍历目录
function listFiles(dir: string): string[] {
  const entries = fs.readdirSync(dir);
  const files: string[] = [];
  
  for (const entry of entries) {
    const fullPath = path.join(dir, entry);
    const stats = fs.statSync(fullPath);
    
    if (stats.isFile()) {
      files.push(fullPath);
    } else if (stats.isDirectory()) {
      files.push(...listFiles(fullPath));
    }
  }
  
  return files;
}
```

## 错误处理

所有同步函数在失败时会抛出异常（通过 `TS_THROW`）。

```typescript
try {
  const content = fs.readFileSync('nonexistent.txt', 'utf-8');
} catch (e) {
  console.log('Error:', e); // "File not found: nonexistent.txt"
}
```

异步函数返回的 Promise 在失败时会被 reject。

```typescript
try {
  await fs.readFile('nonexistent.txt', 'utf-8');
} catch (e) {
  console.log('Error:', e);
}
```
