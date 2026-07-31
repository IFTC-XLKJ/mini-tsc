# worker_threads 模块 - 多线程

## 概述

`worker_threads` 模块提供了多线程支持，允许在独立的线程中运行 JavaScript 代码，实现并行处理。

## 类型定义

```typescript
interface WorkerOptions {
  eval?: boolean;              // 是否作为代码字符串执行
  cwd?: string;                // 工作目录
  argv?: string[];             // 传递给 worker 的参数
  env?: Record<string, string>; // 环境变量
  workerData?: any;            // 传递给 worker 的数据
  name?: string;               // worker 名称
  resourceLimits?: ResourceLimits;
}

interface ResourceLimits {
  maxYoungGenerationSizeMb?: number;
  maxOldGenerationSizeMb?: number;
}

interface Worker {
  threadId: number;
  name: string;
  
  postMessage(value: any, transferList?: any[]): void;
  terminate(): Promise<number>;
  terminate(): void;
  
  on(event: string, callback: Function): this;
  once(event: string, callback: Function): this;
  removeListener(event: string, callback: Function): this;
}

interface MessagePort {
  postMessage(value: any, transferList?: any[]): void;
  close(): void;
  on(event: string, callback: Function): this;
  once(event: string, callback: Function): this;
  ref(): void;
  unref(): void;
}

interface MessageChannel {
  port1: MessagePort;
  port2: MessagePort;
}
```

## API 列表

### 模块级属性

#### `worker_threads.isMainThread`

指示当前是否为主线程。

**返回值**: `boolean`

**示例**:
```typescript
import { isMainThread } from 'worker_threads';

if (isMainThread) {
  console.log('Running in main thread');
} else {
  console.log('Running in worker thread');
}
```

---

#### `worker_threads.parentPort`

获取父线程的通信端口（仅在 worker 线程中可用）。

**返回值**: `MessagePort` | `null`

---

#### `worker_threads.workerData`

获取从主线程传递的数据（仅在 worker 线程中可用）。

**返回值**: `any`

---

#### `worker_threads.threadId`

获取当前线程 ID。

**返回值**: `number`

---

#### `worker_threads.threadName`

获取当前线程名称。

**返回值**: `string`

---

### 构造函数

#### `new Worker(filename, options?)`

创建新的 Worker 线程。

**参数**:
- `filename`: worker 脚本路径
- `options`: 配置选项

**返回值**: `Worker` 对象

**示例**:
```typescript
import { Worker } from 'worker_threads';

const worker = new Worker('./worker.js', {
  workerData: { taskId: 123 }
});
```

---

#### `new MessageChannel()`

创建消息通道。

**返回值**: `MessageChannel` 对象

**示例**:
```typescript
import { MessageChannel } from 'worker_threads';

const { port1, port2 } = new MessageChannel();
```

---

### 实例方法

#### `worker.postMessage(value, transferList?)`

向 worker 发送消息。

**参数**:
- `value`: 要发送的数据
- `transferList`: 可选，要转移所有权的对象列表

**返回值**: `void`

**示例**:
```typescript
worker.postMessage({ type: 'task', data: { id: 1 } });
```

---

#### `worker.terminate()`

终止 worker 线程。

**返回值**: `Promise<number>` - 退出码

**示例**:
```typescript
const exitCode = await worker.terminate();
console.log(`Worker exited with code ${exitCode}`);
```

---

#### `port.postMessage(value, transferList?)`

通过端口发送消息。

**参数**:
- `value`: 要发送的数据
- `transferList`: 可选，要转移所有权的对象列表

**返回值**: `void`

---

#### `port.close()`

关闭端口。

**返回值**: `void`

---

#### `port.ref()`

引用端口，阻止线程退出。

**返回值**: `void`

---

#### `port.unref()`

取消引用端口，允许线程退出。

**返回值**: `void`

---

### 静态方法

#### `worker_threads.getEnvironmentData(key)`

获取环境数据。

**参数**:
- `key`: 数据键

**返回值**: `any`

---

#### `worker_threads.setEnvironmentData(key, value)`

设置环境数据。

**参数**:
- `key`: 数据键
- `value`: 数据值

**返回值**: `void`

---

#### `worker_threads.receiveMessageOnPort(port)`

从端口接收消息。

**参数**:
- `port`: 端口对象

**返回值**: `{ message: any }` | `undefined`

---

#### `worker_threads.markAsUntransferable(object)`

标记对象为不可转移。

**参数**:
- `object`: 要标记的对象

**返回值**: `void`

---

#### `worker_threads.postMessageToThread(threadId, value, transferList?)`

向指定线程发送消息。

**参数**:
- `threadId`: 目标线程 ID
- `value`: 要发送的数据
- `transferList`: 可选，要转移所有权的对象列表

**返回值**: `void`

---

## 使用示例

### 基本 Worker 使用

**main.ts**:
```typescript
import { Worker } from 'worker_threads';

// 创建 worker
const worker = new Worker('./worker.js', {
  workerData: { start: 1, end: 100 }
});

// 监听消息
worker.on('message', (result) => {
  console.log('Result from worker:', result);
});

// 监听错误
worker.on('error', (err) => {
  console.error('Worker error:', err);
});

// 监听退出
worker.on('exit', (code) => {
  console.log(`Worker exited with code ${code}`);
});

// 发送任务
worker.postMessage({ type: 'process', data: [1, 2, 3, 4, 5] });
```

**worker.js**:
```typescript
const { parentPort, workerData } = require('worker_threads');

console.log('Worker started with data:', workerData);

// 接收主线程消息
parentPort.on('message', (msg) => {
  console.log('Received from main:', msg);
  
  // 处理数据
  const result = msg.data.reduce((sum, num) => sum + num, 0);
  
  // 发送结果回主线程
  parentPort.postMessage({ type: 'result', sum: result });
});

// 发送初始消息
parentPort.postMessage({ type: 'ready', workerId: workerData.start });
```

### 并行计算

**main.ts**:
```typescript
import { Worker } from 'worker_threads';

interface Task {
  id: number;
  start: number;
  end: number;
}

function runTask(task: Task): Promise<number> {
  return new Promise((resolve, reject) => {
    const worker = new Worker('./calculator.js', {
      workerData: task
    });
    
    worker.on('message', (result) => {
      resolve(result.sum);
    });
    
    worker.on('error', reject);
  });
}

async function parallelSum(): Promise<void> {
  const tasks: Task[] = [
    { id: 1, start: 1, end: 1000000 },
    { id: 2, start: 1000001, end: 2000000 },
    { id: 3, start: 2000001, end: 3000000 },
    { id: 4, start: 3000001, end: 4000000 },
  ];
  
  const startTime = Date.now();
  
  // 并行执行所有任务
  const results = await Promise.all(tasks.map(runTask));
  
  const total = results.reduce((sum, r) => sum + r, 0);
  const duration = Date.now() - startTime;
  
  console.log(`Total sum: ${total}`);
  console.log(`Duration: ${duration}ms`);
}

parallelSum();
```

**calculator.js**:
```typescript
const { parentPort, workerData } = require('worker_threads');

const { start, end } = workerData;

// 计算范围内的和
let sum = 0;
for (let i = start; i <= end; i++) {
  sum += i;
}

// 发送结果
parentPort.postMessage({ sum });
```

### 线程间通信

**main.ts**:
```typescript
import { Worker, MessageChannel } from 'worker_threads';

// 创建消息通道
const { port1, port2 } = new MessageChannel();

// 创建 worker 并传递端口
const worker = new Worker('./worker.js', {
  workerData: { port: port2 },
  transferList: [port2]
});

// 主线程使用 port1 与 worker 通信
port1.on('message', (msg) => {
  console.log('Main received:', msg);
});

// 发送消息给 worker
port1.postMessage({ type: 'ping', timestamp: Date.now() });
```

**worker.js**:
```typescript
const { parentPort, workerData } = require('worker_threads');

// 获取主线程传递的端口
const port = workerData.port;

// 监听主线程消息
port.on('message', (msg) => {
  console.log('Worker received:', msg);
  
  // 回复主线程
  port.postMessage({
    type: 'pong',
    received: msg,
    timestamp: Date.now()
  });
});

// 发送初始消息给主线程
parentPort.postMessage({ type: 'worker-ready' });
```

### 任务队列

**main.ts**:
```typescript
import { Worker } from 'worker_threads';

interface Task {
  id: number;
  data: any;
}

class TaskQueue {
  private workers: Worker[] = [];
  private queue: Task[] = [];
  private taskMap: Map<number, (result: any) => void> = new Map();
  private nextTaskId = 0;
  
  constructor(workerCount: number) {
    for (let i = 0; i < workerCount; i++) {
      const worker = new Worker('./worker.js');
      
      worker.on('message', (msg) => {
        if (msg.type === 'result') {
          const callback = this.taskMap.get(msg.taskId);
          if (callback) {
            callback(msg.result);
            this.taskMap.delete(msg.taskId);
          }
        }
        
        // 处理下一个任务
        if (this.queue.length > 0) {
          const task = this.queue.shift()!;
          worker.postMessage({ type: 'task', ...task });
        }
      });
      
      this.workers.push(worker);
    }
  }
  
  addTask(data: any): Promise<any> {
    return new Promise((resolve) => {
      const taskId = this.nextTaskId++;
      this.taskMap.set(taskId, resolve);
      
      // 找一个空闲的 worker
      const availableWorker = this.workers[0];
      availableWorker.postMessage({ type: 'task', taskId, data });
    });
  }
  
  async shutdown(): Promise<void> {
    await Promise.all(this.workers.map(w => w.terminate()));
  }
}

// 使用示例
async function main() {
  const queue = new TaskQueue(4);
  
  // 添加任务
  const results = await Promise.all([
    queue.addTask({ operation: 'double', value: 5 }),
    queue.addTask({ operation: 'double', value: 10 }),
    queue.addTask({ operation: 'double', value: 15 }),
  ]);
  
  console.log('Results:', results);
  
  await queue.shutdown();
}

main();
```

**worker.js**:
```typescript
const { parentPort } = require('worker_threads');

parentPort.on('message', (msg) => {
  if (msg.type === 'task') {
    // 模拟处理时间
    setTimeout(() => {
      let result;
      
      switch (msg.data.operation) {
        case 'double':
          result = msg.data.value * 2;
          break;
        case 'square':
          result = msg.data.value * msg.data.value;
          break;
        default:
          result = null;
      }
      
      parentPort.postMessage({
        type: 'result',
        taskId: msg.taskId,
        result
      });
    }, 100);
  }
});
```

## 事件

### Worker 事件

| 事件 | 描述 | 回调参数 |
|------|------|----------|
| `'error'` | 发生错误 | `err: Error` |
| `'exit'` | worker 退出 | `code: number` |
| `'message'` | 收到消息 | `value: any` |
| `'messageerror'` | 消息反序列化失败 | `err: Error` |
| `'online'` | worker 线程启动 | 无 |

### MessagePort 事件

| 事件 | 描述 | 回调参数 |
|------|------|----------|
| `'close'` | 端口关闭 | 无 |
| `'message'` | 收到消息 | `value: any` |
| `'messageerror'` | 消息反序列化失败 | `err: Error` |

## 实现细节

### 线程模型

- 每个 Worker 是一个独立的线程
- 主线程和 Worker 线程通过消息传递通信
- 数据在传递时会被序列化/反序列化

### 数据转移

使用 `transferList` 可以转移所有权，避免复制：
- `ArrayBuffer`
- `MessagePort`
- 其他可转移对象

### 资源限制

可以通过 `resourceLimits` 限制 Worker 的内存使用：
```typescript
const worker = new Worker('./worker.js', {
  resourceLimits: {
    maxYoungGenerationSizeMb: 32,
    maxOldGenerationSizeMb: 256
  }
});
```

## 平台差异

| 功能 | Windows | Linux/macOS |
|------|---------|-------------|
| 线程实现 | Windows Threads | pthreads |
| 内存共享 | 有限支持 | 完整支持 |
| 信号处理 | 不支持 | 支持 |

## 注意事项

1. **数据序列化**: 消息传递时数据会被序列化，避免传递大量数据
2. **资源清理**: 使用完毕后终止 Worker
3. **错误处理**: 监听 `error` 事件处理 Worker 错误
4. **避免共享状态**: 尽量通过消息传递而不是共享内存
