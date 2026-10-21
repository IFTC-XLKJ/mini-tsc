# quickjs 模块 - 嵌入式 JavaScript 引擎

## 概述

`quickjs` 模块基于 [QuickJS](https://bellard.org/quickjs/) 引擎，允许在 mini-tsc 编译出的原生程序中执行 JavaScript 代码。每个 `Context` 是一个相互隔离的 JS 运行时，TS 与 JS 之间的值会自动转换。

值的转换规则：

| JavaScript | TypeScript |
|---|---|
| `number` | `number`（double） |
| `string` | `string` |
| `boolean` | `boolean` |
| `null` / `undefined` | `undefined` |
| `Array` | 数组 |
| 普通对象 | 对象 |
| 函数及其他特殊值 | `undefined` |

## API 列表

### `quickjs.createContext()`

创建一个新的、相互隔离的 JavaScript 上下文。

**返回值**: `Context`（不透明句柄）

---

### `quickjs.eval(ctx, code)`

在指定上下文中执行一段脚本字符串，返回其完成值。

**参数**:
- `ctx`: 上下文
- `code`: JavaScript 源码

**返回值**: 脚本的完成值（已转换为 TS 值）

**示例**:
```typescript
import * as quickjs from 'quickjs';

const ctx = quickjs.createContext();
console.log(quickjs.eval(ctx, '1 + 2 * 3'));       // 7
console.log(quickjs.eval(ctx, "'hello ' + 'world'")); // "hello world"
```

---

### `quickjs.evalFile(ctx, path)`

读取磁盘上的脚本文件并执行，返回其完成值。

**参数**:
- `ctx`: 上下文
- `path`: 脚本文件路径

---

### `quickjs.setGlobal(ctx, name, value)`

在上下文中定义或覆盖一个全局绑定。

**示例**:
```typescript
quickjs.setGlobal(ctx, 'answer', 42);
console.log(quickjs.eval(ctx, 'answer * 2')); // 84
```

---

### `quickjs.getGlobal(ctx, name)`

读取上下文中的全局绑定。

**示例**:
```typescript
quickjs.eval(ctx, "var greeting = 'hi from js';");
console.log(quickjs.getGlobal(ctx, 'greeting')); // "hi from js"
```

---

### `quickjs.dispose(ctx)`

释放上下文及其运行时。可重复调用。

---

## 完整示例

```typescript
import * as quickjs from 'quickjs';

function main(): void {
  const ctx = quickjs.createContext();

  // 数字
  console.log(quickjs.eval(ctx, '1 + 2 * 3')); // 7

  // 数组
  const arr = quickjs.eval(ctx, '[1, 2, 3, 4]');
  console.log(arr.length); // 4
  console.log(arr[0]);     // 1

  // 对象
  const obj = quickjs.eval(ctx, "({ name: 'Alice', age: 30 })");
  console.log(obj.name);   // "Alice"

  // 注入全局并回读
  quickjs.setGlobal(ctx, 'answer', 42);
  console.log(quickjs.eval(ctx, 'answer * 2')); // 84

  // 错误以异常形式抛出
  try {
    quickjs.eval(ctx, "throw new Error('boom');");
  } catch (e: any) {
    console.log('caught:', e.message); // "boom"
  }

  quickjs.dispose(ctx);
}

main();
```

---

## 实现说明

- 引擎源码位于 `runtime/src/quickjs/`（QuickJS 上游发布版，MIT 许可）。
- 包装层为 `runtime/src/builtins/node_quickjs.c`。
- 使用 `quickjs` 模块时，编译器自动链接 QuickJS 引擎源码并定义 `CONFIG_VERSION`。
- Linux 下需要 `-lm -ldl -lpthread`（编译器自动添加）；Windows 下需支持 pthread 的工具链（MinGW）。

## 许可证

QuickJS 采用 MIT 许可证，见 `runtime/src/quickjs/LICENSE`。