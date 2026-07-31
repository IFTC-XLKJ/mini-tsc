# http 模块 - HTTP 服务器和客户端

## 概述

`http` 模块提供了 HTTP 服务器和客户端的功能，支持创建 HTTP 服务器、发送 HTTP 请求等。

## 类型定义

```typescript
interface ServerOptions {
  // 服务器配置选项
}

interface RequestOptions {
  hostname?: string;
  port?: number;
  path?: string;
  method?: string;
  headers?: Record<string, string>;
}

interface IncomingMessage {
  method: string;
  url: string;
  headers: Record<string, string>;
  // 请求体通过事件获取
}

interface ServerResponse {
  statusCode: number;
  statusMessage: string;
  headers: Record<string, string>;
  
  writeHead(statusCode: number, headers?: Record<string, string>): void;
  write(data: string | Buffer): void;
  end(data?: string | Buffer): void;
  setHeader(name: string, value: string): void;
}
```

## API 列表

### `http.createServer(callback?)`

创建 HTTP 服务器。

**参数**:
- `callback`: 请求处理函数 `(req, res) => void`

**返回值**: `Server` 对象

**示例**:
```typescript
import * as http from 'http';

const server = http.createServer((req, res) => {
  res.writeHead(200, { 'Content-Type': 'text/plain' });
  res.end('Hello World');
});
```

---

### `server.listen(port, callback?)`

开始监听指定端口。

**参数**:
- `port`: 监听端口号
- `callback`: 可选，监听成功后的回调

**返回值**: `void`

**示例**:
```typescript
server.listen(3000, () => {
  console.log('Server running on port 3000');
});
```

---

### `http.request(options, callback?)`

发送 HTTP 请求。

**参数**:
- `options`: 请求配置
- `callback`: 响应处理函数

**返回值**: `ClientRequest` 对象

**示例**:
```typescript
const req = http.request({
  hostname: 'example.com',
  port: 80,
  path: '/api/data',
  method: 'GET'
}, (res) => {
  let data = '';
  res.on('data', (chunk) => data += chunk);
  res.on('end', () => console.log(data));
});
req.end();
```

---

### `http.get(url, callback?)`

发送 GET 请求的快捷方法。

**参数**:
- `url`: 请求 URL
- `callback`: 响应处理函数

**返回值**: `ClientRequest` 对象

**示例**:
```typescript
http.get('http://example.com/api/data', (res) => {
  let data = '';
  res.on('data', (chunk) => data += chunk);
  res.on('end', () => console.log(data));
});
```

---

## 使用示例

### 简单 HTTP 服务器

```typescript
import * as http from 'http';

const server = http.createServer((req, res) => {
  console.log(`${req.method} ${req.url}`);
  
  // 设置响应头
  res.writeHead(200, {
    'Content-Type': 'text/html; charset=utf-8'
  });
  
  // 发送响应
  res.end(`
    <!DOCTYPE html>
    <html>
    <head><title>My Server</title></head>
    <body>
      <h1>Hello from mini-tsc!</h1>
      <p>Time: ${new Date().toISOString()}</p>
    </body>
    </html>
  `);
});

server.listen(3000, () => {
  console.log('Server running at http://localhost:3000/');
});
```

### REST API 服务器

```typescript
import * as http from 'http';

interface User {
  id: number;
  name: string;
  email: string;
}

const users: User[] = [
  { id: 1, name: 'Alice', email: 'alice@example.com' },
  { id: 2, name: 'Bob', email: 'bob@example.com' },
];

function parseBody(req: any): Promise<string> {
  return new Promise((resolve) => {
    let body = '';
    req.on('data', (chunk: string) => body += chunk);
    req.on('end', () => resolve(body));
  });
}

const server = http.createServer(async (req, res) => {
  const { method, url } = req;
  
  // CORS 头
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE');
  
  // 路由
  if (method === 'GET' && url === '/api/users') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(users));
  }
  else if (method === 'POST' && url === '/api/users') {
    const body = await parseBody(req);
    const newUser = JSON.parse(body);
    newUser.id = users.length + 1;
    users.push(newUser);
    
    res.writeHead(201, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(newUser));
  }
  else if (method === 'GET' && url?.startsWith('/api/users/')) {
    const id = parseInt(url.split('/')[3]);
    const user = users.find(u => u.id === id);
    
    if (user) {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify(user));
    } else {
      res.writeHead(404, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ error: 'User not found' }));
    }
  }
  else {
    res.writeHead(404, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ error: 'Not found' }));
  }
});

server.listen(8080, () => {
  console.log('API server running at http://localhost:8080/');
});
```

### HTTP 客户端

```typescript
import * as http from 'http';

// GET 请求
function fetch(url: string): Promise<string> {
  return new Promise((resolve, reject) => {
    http.get(url, (res) => {
      let data = '';
      res.on('data', (chunk) => data += chunk);
      res.on('end', () => resolve(data));
      res.on('error', reject);
    }).on('error', reject);
  });
}

// POST 请求
async function postData(url: string, data: any): Promise<string> {
  return new Promise((resolve, reject) => {
    const postData = JSON.stringify(data);
    
    const req = http.request(url, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Content-Length': postData.length.toString()
      }
    }, (res) => {
      let result = '';
      res.on('data', (chunk) => result += chunk);
      res.on('end', () => resolve(result));
      res.on('error', reject);
    });
    
    req.on('error', reject);
    req.write(postData);
    req.end();
  });
}

// 使用示例
async function main() {
  const users = await fetch('http://api.example.com/users');
  console.log('Users:', users);
  
  const newUser = await postData('http://api.example.com/users', {
    name: 'Charlie',
    email: 'charlie@example.com'
  });
  console.log('Created:', newUser);
}
```

## 实现细节

### 服务器实现

HTTP 服务器基于 TCP 套接字实现：

1. 创建 TCP 服务器监听指定端口
2. 接受客户端连接
3. 解析 HTTP 请求
4. 调用请求处理函数
5. 发送 HTTP 响应

### 客户端实现

HTTP 客户端支持：
- 连接到远程服务器
- 发送 HTTP 请求
- 接收 HTTP 响应
- 支持 chunked transfer encoding

### 连接管理

- 服务器维护活跃连接列表
- 支持 keep-alive 连接
- 自动处理并发请求

## 平台差异

| 功能 | Windows | Linux/macOS |
|------|---------|-------------|
| Socket API | Winsock2 | POSIX sockets |
| 需要链接的库 | ws2_32 | 无（默认支持） |

## 性能优化

1. **连接池**: 客户端维护连接复用
2. **缓冲区管理**: 使用预分配缓冲区减少内存分配
3. **非阻塞 I/O**: 使用非阻塞套接字提高并发性能

## 限制

1. 不支持 HTTPS（需要额外的 TLS/SSL 库）
2. 不支持 WebSocket（需要单独的模块）
3. 不支持 HTTP/2
