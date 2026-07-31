# net 模块 - TCP/UDP 网络

## 概述

`net` 模块提供了 TCP 和 UDP 网络通信功能，支持创建 TCP 服务器和客户端连接。

## 类型定义

```typescript
interface Socket {
  remoteAddress: string;
  remotePort: number;
  localAddress: string;
  localPort: number;
  
  write(data: string | Buffer): boolean;
  end(data?: string | Buffer): void;
  destroy(): void;
  
  on(event: string, callback: Function): void;
  once(event: string, callback: Function): void;
}

interface Server {
  listen(port: number, host?: string, callback?: Function): void;
  close(callback?: Function): void;
  address(): { address: string; port: number; family: string };
  
  on(event: string, callback: Function): void;
  once(event: string, callback: Function): void;
}
```

## API 列表

### `net.createServer(callback?)`

创建 TCP 服务器。

**参数**:
- `callback`: 连接处理函数 `(socket: Socket) => void`

**返回值**: `Server` 对象

**示例**:
```typescript
import * as net from 'net';

const server = net.createServer((socket) => {
  console.log('Client connected');
  
  socket.on('data', (data) => {
    console.log('Received:', data.toString());
    socket.write('Echo: ' + data);
  });
  
  socket.on('end', () => {
    console.log('Client disconnected');
  });
});
```

---

### `net.createConnection(options, callback?)`

创建 TCP 客户端连接。

**参数**:
- `options`: 连接配置
  - `host`: 服务器地址
  - `port`: 服务器端口
- `callback`: 连接成功后的回调

**返回值**: `Socket` 对象

**示例**:
```typescript
const socket = net.createConnection({ host: 'localhost', port: 3000 }, () => {
  console.log('Connected to server');
  socket.write('Hello Server');
});
```

---

## 使用示例

### TCP 服务器

```typescript
import * as net from 'net';

const server = net.createServer((socket) => {
  console.log(`Client connected: ${socket.remoteAddress}:${socket.remotePort}`);
  
  // 发送欢迎消息
  socket.write('Welcome to the server!\n');
  
  // 处理接收的数据
  socket.on('data', (data) => {
    const message = data.toString().trim();
    console.log(`Received: ${message}`);
    
    // 回显消息
    socket.write(`Echo: ${message}\n`);
    
    // 特殊命令
    if (message === 'quit') {
      socket.write('Goodbye!\n');
      socket.end();
    }
  });
  
  // 处理断开连接
  socket.on('end', () => {
    console.log('Client disconnected');
  });
  
  // 处理错误
  socket.on('error', (err) => {
    console.error('Socket error:', err);
  });
});

server.listen(3000, '0.0.0.0', () => {
  console.log('TCP server listening on port 3000');
});
```

### TCP 客户端

```typescript
import * as net from 'net';

function createClient(port: number, host: string): Promise<Socket> {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ port, host }, () => {
      console.log('Connected to server');
      resolve(socket);
    });
    
    socket.on('error', reject);
  });
}

async function main() {
  const socket = await createClient(3000, 'localhost');
  
  // 发送消息
  socket.write('Hello Server');
  
  // 接收响应
  socket.on('data', (data) => {
    console.log('Server response:', data.toString());
  });
  
  // 关闭连接
  setTimeout(() => {
    socket.write('quit');
    socket.end();
  }, 5000);
}
```

### 简单的聊天服务器

```typescript
import * as net from 'net';

interface Client {
  socket: Socket;
  name: string;
}

const clients: Client[] = [];

function broadcast(message: string, exclude?: Socket): void {
  for (const client of clients) {
    if (client.socket !== exclude) {
      client.socket.write(message);
    }
  }
}

const server = net.createServer((socket) => {
  const client: Client = {
    socket,
    name: `User${clients.length + 1}`
  };
  clients.push(client);
  
  console.log(`${client.name} joined (${clients.length} online)`);
  broadcast(`${client.name} joined the chat\n`, socket);
  socket.write(`Welcome, ${client.name}! Type /name <newname> to change name.\n`);
  
  socket.on('data', (data) => {
    const message = data.toString().trim();
    
    // 命令处理
    if (message.startsWith('/name ')) {
      const newName = message.slice(6);
      const oldName = client.name;
      client.name = newName;
      broadcast(`${oldName} is now ${newName}\n`);
      return;
    }
    
    if (message === '/list') {
      const names = clients.map(c => c.name).join(', ');
      socket.write(`Online users: ${names}\n`);
      return;
    }
    
    // 广播消息
    if (message) {
      broadcast(`${client.name}: ${message}\n`, socket);
    }
  });
  
  socket.on('end', () => {
    const index = clients.indexOf(client);
    if (index !== -1) {
      clients.splice(index, 1);
    }
    console.log(`${client.name} left (${clients.length} online)`);
    broadcast(`${client.name} left the chat\n`);
  });
  
  socket.on('error', (err) => {
    console.error(`${client.name} error:`, err);
  });
});

server.listen(8080, () => {
  console.log('Chat server running on port 8080');
});
```

### HTTP 代理服务器

```typescript
import * as net from 'net';

const proxyServer = net.createServer((clientSocket) => {
  console.log('Client connected');
  
  let serverSocket: Socket | null = null;
  
  clientSocket.once('data', (data) => {
    // 解析 HTTP 请求（简化版）
    const request = data.toString();
    const firstLine = request.split('\n')[0];
    const [method, target] = firstLine.split(' ');
    
    // 解析目标地址
    const url = new URL(target);
    const host = url.hostname;
    const port = parseInt(url.port) || 80;
    
    console.log(`Proxying ${method} ${target}`);
    
    // 连接到目标服务器
    serverSocket = net.createConnection({ host, port }, () => {
      // 转发请求
      serverSocket!.write(data);
      
      // 双向转发数据
      clientSocket.on('data', (chunk) => {
        serverSocket!.write(chunk);
      });
      
      serverSocket!.on('data', (chunk) => {
        clientSocket.write(chunk);
      });
    });
    
    serverSocket.on('error', (err) => {
      console.error('Target server error:', err);
      clientSocket.end();
    });
  });
  
  clientSocket.on('end', () => {
    if (serverSocket) {
      serverSocket.end();
    }
  });
  
  clientSocket.on('error', (err) => {
    console.error('Client error:', err);
    if (serverSocket) {
      serverSocket.end();
    }
  });
});

proxyServer.listen(8080, () => {
  console.log('HTTP proxy running on port 8080');
});
```

## 事件

### Socket 事件

| 事件 | 描述 | 回调参数 |
|------|------|----------|
| `'connect'` | 连接成功 | 无 |
| `'data'` | 收到数据 | `data: Buffer` |
| `'end'` | 连接结束 | 无 |
| `'error'` | 发生错误 | `err: Error` |
| `'close'` | 连接关闭 | `hadError: boolean` |
| `'timeout'` | 连接超时 | 无 |

### Server 事件

| 事件 | 描述 | 回调参数 |
|------|------|----------|
| `'listening'` | 开始监听 | 无 |
| `'connection'` | 新连接 | `socket: Socket` |
| `'error'` | 发生错误 | `err: Error` |
| `'close'` | 服务器关闭 | 无 |

## 实现细节

### TCP 连接

使用 POSIX sockets API：
- `socket()`: 创建套接字
- `connect()`: 连接到服务器
- `bind()`: 绑定地址
- `listen()`: 监听连接
- `accept()`: 接受连接
- `send()/recv()`: 发送/接收数据

### 平台差异

| 功能 | Windows | Linux/macOS |
|------|---------|-------------|
| Socket API | Winsock2 | POSIX sockets |
| 头文件 | `<winsock2.h>` | `<sys/socket.h>` |
| 需要链接的库 | ws2_32 | 无 |

## 性能考虑

1. **缓冲区管理**: 使用合适的缓冲区大小
2. **连接复用**: 使用 keep-alive 连接
3. **非阻塞 I/O**: 使用非阻塞模式提高并发

## 安全建议

1. 验证输入数据
2. 限制连接数量
3. 设置合理的超时时间
4. 处理异常情况
