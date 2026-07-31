# path 模块 - 路径处理

## 概述

`path` 模块提供了文件路径处理的工具函数，包括路径拼接、解析、规范化等操作。

## API 列表

### `join(...paths)`

拼接多个路径片段。

**参数**: 可变参数，多个路径字符串

**返回值**: `string` - 拼接后的路径

**示例**:
```typescript
import * as path from 'path';

path.join('src', 'utils', 'index.ts');
// Linux/macOS: "src/utils/index.ts"
// Windows: "src\utils\index.ts"

path.join('/home', 'user', 'docs');
// "/home/user/docs"
```

---

### `resolve(...paths)`

将路径或路径片段解析为绝对路径。

**参数**: 可变参数，多个路径字符串

**返回值**: `string` - 绝对路径

**示例**:
```typescript
path.resolve('src', 'utils', 'index.ts');
// 从当前目录解析为绝对路径

path.resolve('/home', 'user', 'file.txt');
// "/home/user/file.txt"
```

---

### `basename(path, ext?)`

获取路径的最后一部分（文件名）。

**参数**:
- `path`: 文件路径
- `ext`: 可选，要移除的扩展名

**返回值**: `string`

**示例**:
```typescript
path.basename('/home/user/file.txt');
// "file.txt"

path.basename('/home/user/file.txt', '.txt');
// "file"

path.basename('/home/user/documents/');
// "documents"
```

---

### `dirname(path)`

获取路径的目录部分。

**参数**:
- `path`: 文件路径

**返回值**: `string`

**示例**:
```typescript
path.dirname('/home/user/file.txt');
// "/home/user"

path.dirname('src/utils/index.ts');
// "src/utils"
```

---

### `extname(path)`

获取文件扩展名。

**参数**:
- `path`: 文件路径

**返回值**: `string` - 扩展名（包含点号）

**示例**:
```typescript
path.extname('file.txt');
// ".txt"

path.extname('file');
// ""

path.extname('.gitignore');
// ""
```

---

### `normalize(path)`

规范化路径，解析 `.` 和 `..`。

**参数**:
- `path`: 文件路径

**返回值**: `string`

**示例**:
```typescript
path.normalize('src/utils/../index.ts');
// "src/index.ts"

path.normalize('src//utils///index.ts');
// "src/utils/index.ts"
```

---

### `parse(path)`

解析路径为对象。

**参数**:
- `path`: 文件路径

**返回值**: `PathObject`

```typescript
interface PathObject {
  root: string;     // 根目录
  dir: string;      // 目录部分
  base: string;     // 基名（文件名+扩展名）
  ext: string;      // 扩展名
  name: string;     // 文件名（不含扩展名）
}
```

**示例**:
```typescript
const p = path.parse('/home/user/file.txt');
// {
//   root: '/',
//   dir: '/home/user',
//   base: 'file.txt',
//   ext: '.txt',
//   name: 'file'
// }
```

---

### `format(pathObject)`

将路径对象格式化为路径字符串。

**参数**:
- `pathObject`: 路径对象

**返回值**: `string`

**示例**:
```typescript
path.format({
  dir: '/home/user',
  base: 'file.txt'
});
// "/home/user/file.txt"
```

---

### `isAbsolute(path)`

检查路径是否为绝对路径。

**参数**:
- `path`: 文件路径

**返回值**: `boolean`

**示例**:
```typescript
path.isAbsolute('/home/user/file.txt');  // true
path.isAbsolute('src/index.ts');         // false
path.isAbsolute('./file.txt');           // false
```

---

### `relative(from, to)`

计算从 `from` 到 `to` 的相对路径。

**参数**:
- `from`: 源路径
- `to`: 目标路径

**返回值**: `string`

**示例**:
```typescript
path.relative('/home/user/project', '/home/user/project/src/index.ts');
// "src/index.ts"

path.relative('/home/user/a', '/home/user/b');
// "../b"
```

---

## 平台差异

| 特性 | Windows | Linux/macOS |
|------|---------|-------------|
| 路径分隔符 | `\` | `/` |
| 绝对路径 | `C:\...` | `/...` |
| 驱动器 | 支持 | 不支持 |

## 使用示例

```typescript
import * as path from 'path';

// 获取当前文件目录
function getCurrentDir(): string {
  return __dirname;
}

// 构建模块路径
function getModulePath(moduleName: string): string {
  return path.join(__dirname, 'modules', moduleName, 'index.ts');
}

// 检查文件类型
function getFileType(filename: string): string {
  const ext = path.extname(filename).toLowerCase();
  switch (ext) {
    case '.ts':
    case '.js':
      return 'typescript';
    case '.json':
      return 'json';
    case '.md':
      return 'markdown';
    default:
      return 'unknown';
  }
}

// 相对路径计算
function getRelativeImport(from: string, to: string): string {
  const relative = path.relative(path.dirname(from), to);
  return relative.startsWith('.') ? relative : './' + relative;
}
```

## 实现细节

### 路径规范化

1. 替换连续的分隔符为单个分隔符
2. 解析 `.` 当前目录
3. 解析 `..` 上级目录
4. 处理根目录标识

### 跨平台处理

- Windows: 使用 `\` 作为分隔符
- Linux/macOS: 使用 `/` 作为分隔符
- 运行时根据 `process.platform` 自动选择
