# mini-tsc 内置模块开发文档

## 概述

本文档详细介绍了 mini-tsc 项目中所有内置模块的 API、用法和实现细节。

## 模块列表

### 文件系统

| 模块 | 文档 | 描述 |
|------|------|------|
| [fs](./fs.md) | 文件系统操作 | 文件读写、目录操作、文件属性查询 |

### 路径处理

| 模块 | 文档 | 描述 |
|------|------|------|
| [path](./path.md) | 路径处理 | 路径拼接、解析、规范化 |

### 进程信息

| 模块 | 文档 | 描述 |
|------|------|------|
| [process](./process.md) | 进程信息 | 环境变量、命令行参数、工作目录 |

### 网络通信

| 模块 | 文档 | 描述 |
|------|------|------|
| [http](./http.md) | HTTP 服务器/客户端 | HTTP 请求和响应处理 |
| [net](./net.md) | TCP/UDP 网络 | TCP 服务器和客户端连接 |

### 操作系统

| 模块 | 文档 | 描述 |
|------|------|------|
| [os](./os.md) | 操作系统信息 | 平台、CPU、内存等系统信息 |

### 子进程

| 模块 | 文档 | 描述 |
|------|------|------|
| [child_process](./child_process.md) | 子进程管理 | 创建和管理子进程 |

### 事件系统

| 模块 | 文档 | 描述 |
|------|------|------|
| [events](./events.md) | 事件发射器 | 事件监听、触发、移除 |

### 用户输入

| 模块 | 文档 | 描述 |
|------|------|------|
| [readline](./readline.md) | 命令行输入 | 逐行读取、用户交互 |

### 加密功能

| 模块 | 文档 | 描述 |
|------|------|------|
| [crypto](./crypto.md) | 加密功能 | 哈希、HMAC、随机数生成 |

### 多线程

| 模块 | 文档 | 描述 |
|------|------|------|
| [worker_threads](./worker_threads.md) | 多线程 | 并行处理、线程通信 |

### 数据库

| 模块 | 文档 | 描述 |
|------|------|------|
| [sqlite](./sqlite.md) | SQLite 数据库 | SQL 查询、表操作、CRUD |

### 外部函数接口

| 模块 | 文档 | 描述 |
|------|------|------|
| [ffi](./ffi.md) | 外部函数接口 | 调用动态链接库函数 |

### Web 视图

| 模块 | 文档 | 描述 |
|------|------|------|
| [webview](./webview.md) | 原生 Web 视图 | 桌面应用程序 UI |

### 终端样式

| 模块 | 文档 | 描述 |
|------|------|------|
| [chalk](./chalk.md) | 终端颜色和样式 | 命令行文本美化 |

### 断言

| 模块 | 文档 | 描述 |
|------|------|------|
| [assert](./assert.md) | 断言 | 条件检查、测试验证 |

---

## 快速开始

### 导入模块

```typescript
// 使用 import 语句
import * as fs from 'fs';
import * as path from 'path';
import * as process from 'process';

// 或使用解构导入
import { readFileSync, writeFileSync } from 'fs';
import { join, resolve } from 'path';
```

### 基本用法

```typescript
import * as fs from 'fs';
import * as path from 'path';

// 读取文件
const content = fs.readFileSync('file.txt', 'utf-8');

// 拼接路径
const filePath = path.join(__dirname, 'data', 'config.json');

// 获取当前目录
console.log(process.cwd());
```

---

## 平台兼容性

### Windows

- 文件路径使用 `\` 分隔符
- 需要链接 `ws2_32` 库（网络模块）
- 不支持某些 Unix 特定功能

### Linux

- 文件路径使用 `/` 分隔符
- 支持所有 POSIX 功能
- 需要安装系统依赖（如 webview）

### macOS

- 文件路径使用 `/` 分隔符
- 支持所有 POSIX 功能
- WebKit 作为 webview 后端

---

## 错误处理

### 同步操作

同步操作失败时会抛出异常：

```typescript
import * as fs from 'fs';

try {
  const content = fs.readFileSync('nonexistent.txt', 'utf-8');
} catch (e) {
  console.error('Error:', e.message);
}
```

### 异步操作

异步操作返回 Promise，失败时会 reject：

```typescript
import * as fs from 'fs';

try {
  const content = await fs.readFile('nonexistent.txt', 'utf-8');
} catch (e) {
  console.error('Error:', e.message);
}
```

---

## 最佳实践

1. **资源清理**: 使用完毕后关闭文件、数据库连接等
2. **错误处理**: 始终处理可能的异常
3. **输入验证**: 验证用户输入和外部数据
4. **性能优化**: 使用适当的缓冲区大小和异步操作
5. **跨平台**: 注意不同平台的差异

---

## 示例项目

### 文件管理器

```typescript
import * as fs from 'fs';
import * as path from 'path';

class FileManager {
  private basePath: string;
  
  constructor(basePath: string) {
    this.basePath = basePath;
  }
  
  listFiles(subDir: string = ''): string[] {
    const dir = path.join(this.basePath, subDir);
    return fs.readdirSync(dir);
  }
  
  readFile(filePath: string): string {
    const fullPath = path.join(this.basePath, filePath);
    return fs.readFileSync(fullPath, 'utf-8');
  }
  
  writeFile(filePath: string, content: string): void {
    const fullPath = path.join(this.basePath, filePath);
    const dir = path.dirname(fullPath);
    
    if (!fs.existsSync(dir)) {
      fs.mkdirSync(dir, { recursive: true });
    }
    
    fs.writeFileSync(fullPath, content);
  }
  
  deleteFile(filePath: string): void {
    const fullPath = path.join(this.basePath, filePath);
    fs.unlinkSync(fullPath);
  }
}

// 使用示例
const manager = new FileManager('./data');
manager.writeFile('config.json', JSON.stringify({ key: 'value' }));
console.log(manager.readFile('config.json'));
```

### Web 服务器

```typescript
import * as http from 'http';
import * as fs from 'fs';
import * as path from 'path';

const server = http.createServer((req, res) => {
  let filePath = '.' + req.url;
  
  if (filePath === './') {
    filePath = './index.html';
  }
  
  const extname = path.extname(filePath);
  const contentType: Record<string, string> = {
    '.html': 'text/html',
    '.js': 'application/javascript',
    '.css': 'text/css',
    '.json': 'application/json',
    '.png': 'image/png',
    '.jpg': 'image/jpeg'
  };
  
  fs.readFile(filePath, (error, content) => {
    if (error) {
      res.writeHead(404);
      res.end('Not Found');
    } else {
      res.writeHead(200, { 'Content-Type': contentType[extname] || 'text/plain' });
      res.end(content);
    }
  });
});

server.listen(3000, () => {
  console.log('Server running at http://localhost:3000/');
});
```

### 数据库应用

```typescript
import { sqlite } from 'sqlite';

const db = sqlite.open('app.sqlite');

// 创建表
db.createTable('users', {
  id: 'INTEGER PRIMARY KEY AUTOINCREMENT',
  name: 'TEXT NOT NULL',
  email: 'TEXT UNIQUE',
  created_at: 'DATETIME DEFAULT CURRENT_TIMESTAMP'
});

// 插入数据
db.insert('users', { name: 'Alice', email: 'alice@example.com' });
db.insert('users', { name: 'Bob', email: 'bob@example.com' });

// 查询数据
const users = db.findAll('users');
console.log('Users:', users);

// 更新数据
db.update('users', { name: 'Alice Smith' }, { email: 'alice@example.com' });

// 删除数据
db.remove('users', { email: 'bob@example.com' });

// 关闭数据库
db.close();
```

---

## 相关资源

- [TypeScript 官方文档](https://www.typescriptlang.org/docs/)
- [Node.js API 文档](https://nodejs.org/api/)
- [SQLite 文档](https://www.sqlite.org/docs.html)

---

## 反馈

如果您有任何问题或建议，请提交 issue 或发送邮件到 [iftcceo@gmail.com](mailto:iftcceo@gmail.com)。

## 贡献

欢迎贡献代码和文档！请遵循以下步骤：

1. Fork 项目
2. 创建特性分支
3. 提交更改
4. 推送到分支
5. 创建 Pull Request

---

## 许可证

MIT License
