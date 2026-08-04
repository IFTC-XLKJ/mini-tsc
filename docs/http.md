# http 模块 - HTTP 服务器和客户端

## 概述

`http` 模块提供了 HTTP 服务器和客户端功能。mini-tsc 的实现采用 **Web API 风格**，使用标准的 `Request` 和 `Response` 对象，而非传统的 Node.js `IncomingMessage`/`ServerResponse` 模式。

## 类型定义

```typescript
type RequestListener = (
  req: Request,
) => Response | Promise<Response> | BodyInit | Promise<BodyInit> | any;

interface Server {
  listen(port: number, callback?: () => void): this | void;
  listen(port: number, host: string, callback?: () => void): this | void;
  close?(callback?: () => void): void;
  on?(event: string, listener: (...args: any[]) => void): this;
}

interface RequestOptions {
  hostname?: string;
  host?: string;
  port?: number | string;
  path?: string;
  method?: string;
  headers?: Record<string, string>;
}
```

## API 列表

### `http.createServer(handler?)`

创建 HTTP 服务器。

**参数**:
- `handler`: 请求处理函数，接收 `Request` 对象，返回 `Response` 或 `Promise<Response>`

**返回值**: `Server` 对象

**示例**:
```typescript
import * as http from 'http';

const server = http.createServer((req: Request) => {
  return new Response('Hello World', {
    headers: { 'Content-Type': 'text/plain' }
  });
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

const server = http.createServer((req: Request) => {
  console.log(req.method, req.url);
  
  return new Response(`
    <!DOCTYPE html>
    <html>
    <head><title>My Server</title></head>
    <body>
      <h1>Hello from mini-tsc!</h1>
      <p>Time: ${new Date().toISOString()}</p>
    </body>
    </html>
  `, {
    headers: { 'Content-Type': 'text/html; charset=utf-8' }
  });
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

const server = http.createServer(async (req: Request) => {
  const url = new URL(req.url || '/', 'http://localhost');
  const method = req.method;
  
  // CORS 头
  const headers: Record<string, string> = {
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE',
  };
  
  // 路由
  if (method === 'GET' && url.pathname === '/api/users') {
    return new Response(JSON.stringify(users), {
      headers: { ...headers, 'Content-Type': 'application/json' }
    });
  }
  
  if (method === 'POST' && url.pathname === '/api/users') {
    const body = await req.json();
    const newUser = { id: users.length + 1, ...body as any };
    users.push(newUser);
    
    return new Response(JSON.stringify(newUser), {
      status: 201,
      headers: { ...headers, 'Content-Type': 'application/json' }
    });
  }
  
  if (method === 'GET' && url.pathname.startsWith('/api/users/')) {
    const id = parseInt(url.pathname.split('/')[3]);
    const user = users.find(u => u.id === id);
    
    if (user) {
      return new Response(JSON.stringify(user), {
        headers: { ...headers, 'Content-Type': 'application/json' }
      });
    }
    return new Response(JSON.stringify({ error: 'User not found' }), {
      status: 404,
      headers: { ...headers, 'Content-Type': 'application/json' }
    });
  }
  
  return new Response(JSON.stringify({ error: 'Not found' }), {
    status: 404,
    headers: { ...headers, 'Content-Type': 'application/json' }
  });
});

server.listen(8080, () => {
  console.log('API server running at http://localhost:8080/');
});
```

### 文件服务

```typescript
import * as http from 'http';
import * as fs from 'fs/promises';
import * as path from 'path';

const server = http.createServer(async (req: Request) => {
  const url = new URL(req.url || '/', 'http://localhost');
  
  if (url.pathname === '/download') {
    const file = await fs.readFile(path.join(__dirname, '../data.zip'));
    return new Response(file, {
      headers: {
        'Content-Type': 'application/zip',
        'Content-Disposition': 'attachment; filename=data.zip',
      }
    });
  }
  
  return new Response('Not found', { status: 404 });
});

server.listen(3000);
```

### Server-Sent Events (SSE)

```typescript
import * as http from 'http';

const server = http.createServer((req: Request) => {
  const url = new URL(req.url || '/', 'http://localhost');
  
  if (url.pathname === '/events') {
    const { readable, writable } = new TransformStream();
    const writer = writable.getWriter();
    
    // 模拟发送事件
    for (let i = 1; i <= 5; i++) {
      setTimeout(() => {
        writer.write(`id: ${i}\nevent: tick\ndata: chunk ${i}\n\n`);
        if (i === 5) writer.close();
      }, i * 1000);
    }
    
    return new Response(readable, {
      headers: { 'Content-Type': 'text/event-stream' }
    });
  }
  
  return new Response('Hello');
});

server.listen(3000);
```

### WebSocket 升级

```typescript
import * as http from 'http';

const server = http.createServer((req: Request) => {
  const url = new URL(req.url || '/', 'http://localhost');
  
  if (url.pathname === '/ws') {
    if (req.headers.get('upgrade') !== 'websocket') {
      return new Response(null, { status: 426 });
    }
    
    const wss = new WebSocketServer();
    wss.onmessage = (event) => {
      console.log('Received:', event.data);
      wss.send('Echo: ' + event.data);
    };
    
    return new Response(wss, {
      headers: { 'Sec-WebSocket-Protocol': 'chat' }
    });
  }
  
  return new Response('Hello');
});

server.listen(3000);
```

## 实现细节

### 服务器实现

HTTP 服务器基于以下流程：

1. 创建 TCP 服务器监听指定端口
2. 接受客户端连接
3. 解析 HTTP 请求为 Web `Request` 对象
4. 调用请求处理函数
5. 将返回的 `Response` 对象发送给客户端

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
