# events 模块 - 事件发射器

## 概述

`events` 模块提供了事件发射器（EventEmitter）的实现，支持事件监听、触发、移除等操作。这是 Node.js 事件驱动编程的核心模块。

## 类型定义

```typescript
type EventListener = (...args: any[]) => void;

interface EventEmitter {
  on(event: string, listener: EventListener): this;
  addListener(event: string, listener: EventListener): this;
  once(event: string, listener: EventListener): this;
  off(event: string, listener: EventListener): this;
  removeListener(event: string, listener: EventListener): this;
  emit(event: string, ...args: any[]): boolean;
  removeAllListeners(event?: string): this;
  listenerCount(event: string): number;
  listeners(event: string): EventListener[];
  eventNames(): string[];
  setMaxListeners(n: number): this;
  getMaxListeners(): number;
}
```

## API 列表

### 构造函数

#### `new events.EventEmitter()`

创建事件发射器实例。

**示例**:
```typescript
import { EventEmitter } from 'events';

const emitter = new EventEmitter();
```

---

### 实例方法

#### `emitter.on(event, listener)`

添加事件监听器。

**参数**:
- `event`: 事件名称
- `listener`: 监听函数

**返回值**: `this` - 支持链式调用

**示例**:
```typescript
emitter.on('data', (chunk) => {
  console.log('Received:', chunk);
});
```

---

#### `emitter.addListener(event, listener)`

添加事件监听器（`on` 的别名）。

---

#### `emitter.once(event, listener)`

添加一次性事件监听器，触发一次后自动移除。

**参数**:
- `event`: 事件名称
- `listener`: 监听函数

**返回值**: `this`

**示例**:
```typescript
emitter.once('ready', () => {
  console.log('Ready! (this will only log once)');
});
```

---

#### `emitter.off(event, listener)`

移除事件监听器。

**参数**:
- `event`: 事件名称
- `listener`: 要移除的监听函数

**返回值**: `this`

**示例**:
```typescript
function onData(chunk) {
  console.log('Received:', chunk);
}

emitter.on('data', onData);
emitter.off('data', onData);
```

---

#### `emitter.removeListener(event, listener)`

移除事件监听器（`off` 的别名）。

---

#### `emitter.emit(event, ...args)`

触发事件。

**参数**:
- `event`: 事件名称
- `args`: 传递给监听器的参数

**返回值**: `boolean` - 是否有监听器被触发

**示例**:
```typescript
emitter.emit('data', 'hello', 'world');
```

---

#### `emitter.removeAllListeners(event?)`

移除指定事件的所有监听器，或所有事件的所有监听器。

**参数**:
- `event`: 可选，事件名称

**返回值**: `this`

**示例**:
```typescript
// 移除 'data' 事件的所有监听器
emitter.removeAllListeners('data');

// 移除所有事件的所有监听器
emitter.removeAllListeners();
```

---

#### `emitter.listenerCount(event)`

获取指定事件的监听器数量。

**参数**:
- `event`: 事件名称

**返回值**: `number`

**示例**:
```typescript
const count = emitter.listenerCount('data');
console.log(`Data listeners: ${count}`);
```

---

#### `emitter.listeners(event)`

获取指定事件的监听器数组。

**参数**:
- `event`: 事件名称

**返回值**: `EventListener[]`

**示例**:
```typescript
const listeners = emitter.listeners('data');
console.log(`Data has ${listeners.length} listeners`);
```

---

#### `emitter.eventNames()`

获取所有已注册事件的名称。

**返回值**: `string[]`

**示例**:
```typescript
emitter.on('data', () => {});
emitter.on('error', () => {});

console.log(emitter.eventNames()); // ['data', 'error']
```

---

#### `emitter.setMaxListeners(n)`

设置最大监听器数量警告阈值。

**参数**:
- `n`: 最大监听器数量

**返回值**: `this`

**示例**:
```typescript
emitter.setMaxListeners(20);
```

---

#### `emitter.getMaxListeners()`

获取最大监听器数量。

**返回值**: `number`

---

### 静态方法

#### `emitter.prependListener(event, listener)`

在监听器数组开头添加监听器。

**参数**:
- `event`: 事件名称
- `listener`: 监听函数

**返回值**: `this`

**示例**:
```typescript
emitter.on('data', () => console.log('second'));
emitter.prependListener('data', () => console.log('first'));

emitter.emit('data');
// 输出:
// first
// second
```

---

#### `emitter.prependOnceListener(event, listener)`

在监听器数组开头添加一次性监听器。

---

## 使用示例

### 基本事件使用

```typescript
import { EventEmitter } from 'events';

class Button extends EventEmitter {
  private label: string;
  
  constructor(label: string) {
    super();
    this.label = label;
  }
  
  click(): void {
    console.log(`Button "${this.label}" clicked`);
    this.emit('click', this.label);
  }
}

const button = new Button('Submit');

button.on('click', (label) => {
  console.log(`Handling click on: ${label}`);
});

button.click();
// Button "Submit" clicked
// Handling click on: Submit
```

### 数据流处理

```typescript
import { EventEmitter } from 'events';

class DataProcessor extends EventEmitter {
  private buffer: string[] = [];
  
  feed(data: string): void {
    this.buffer.push(data);
    this.emit('data', data);
    
    if (this.buffer.length >= 10) {
      this.flush();
    }
  }
  
  flush(): void {
    const batch = this.buffer.splice(0, 10);
    this.emit('batch', batch);
  }
  
  end(): void {
    if (this.buffer.length > 0) {
      this.flush();
    }
    this.emit('end');
  }
}

const processor = new DataProcessor();

processor.on('data', (data) => {
  console.log('Received:', data);
});

processor.on('batch', (batch) => {
  console.log(`Processing batch of ${batch.length} items`);
});

processor.on('end', () => {
  console.log('Processing complete');
});

// 模拟数据输入
for (let i = 0; i < 25; i++) {
  processor.feed(`item-${i}`);
}
processor.end();
```

### 一次性事件处理

```typescript
import { EventEmitter } from 'events';

class Connection extends EventEmitter {
  private connected = false;
  
  connect(): void {
    console.log('Connecting...');
    
    setTimeout(() => {
      this.connected = true;
      this.emit('connected');
    }, 1000);
  }
  
  disconnect(): void {
    if (this.connected) {
      this.connected = false;
      this.emit('disconnected');
    }
  }
}

const conn = new Connection();

// 只在第一次连接时执行
conn.once('connected', () => {
  console.log('Connected! Setting up listeners...');
  
  // 连接后的操作
  conn.on('data', (data) => {
    console.log('Data:', data);
  });
});

conn.connect();
```

### 错误处理

```typescript
import { EventEmitter } from 'events';

class SafeEmitter extends EventEmitter {
  emit(event: string, ...args: any[]): boolean {
    if (event === 'error' && this.listenerCount('error') === 0) {
      // 没有错误监听器时，抛出异常
      throw args[0] || new Error('Unhandled error event');
    }
    
    return super.emit(event, ...args);
  }
}

const emitter = new SafeEmitter();

// 添加错误监听器
emitter.on('error', (err) => {
  console.error('Error caught:', err.message);
});

// 触发错误
emitter.emit('error', new Error('Something went wrong'));
// Error caught: Something went wrong
```

### 事件转发

```typescript
import { EventEmitter } from 'events';

class Logger extends EventEmitter {
  private prefix: string;
  
  constructor(prefix: string) {
    super();
    this.prefix = prefix;
  }
  
  log(message: string): void {
    const entry = `[${this.prefix}] ${new Date().toISOString()}: ${message}`;
    this.emit('log', entry);
    console.log(entry);
  }
}

class Application {
  private logger: Logger;
  
  constructor() {
    this.logger = new Logger('APP');
    
    // 转发日志事件
    this.logger.on('log', (entry) => {
      // 可以在这里发送到远程日志服务
      this.sendToRemote(entry);
    });
  }
  
  private sendToRemote(entry: string): void {
    // 模拟远程发送
    console.log('Sending to remote:', entry);
  }
  
  start(): void {
    this.logger.log('Application started');
  }
}

const app = new Application();
app.start();
```

## 事件命名约定

推荐的事件命名约定：

1. **动词形式**: `'data'`, `'error'`, `'end'`, `'connect'`
2. **名词形式**: `'message'`, `'response'`, `'request'`
3. **状态变化**: `'open'`, `'close'`, `'ready'`, `'timeout'`
4. **前缀约定**: 使用 `before`/`after` 前缀表示生命周期

```typescript
// 好的命名
emitter.on('connect', () => {});
emitter.on('disconnect', () => {});
emitter.on('data', () => {});
emitter.on('error', () => {});

// 避免的命名
emitter.on('onConnect', () => {});  // 冗余
emitter.on('connectEvent', () => {});  // 冗余
```

## 性能考虑

1. **监听器数量**: 避免添加过多监听器，使用 `setMaxLimits()` 设置警告阈值
2. **内存泄漏**: 及时移除不再需要的监听器
3. **一次性监听器**: 使用 `once()` 处理只需要触发一次的事件

## 调试技巧

```typescript
// 监听器数量警告
emitter.setMaxListeners(10);

// 调试事件触发
const originalEmit = emitter.emit;
emitter.emit = function(event, ...args) {
  console.log(`Event: ${event}`, args);
  return originalEmit.call(this, event, ...args);
};
```
