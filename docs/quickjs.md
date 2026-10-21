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
| 函数 | 可调用的不透明句柄（见 `quickjs.call`） |
| 其他特殊值 | `undefined` |

反向（TS → JS）时，传给 `setGlobal` 的函数会成为 JS 中可调用的函数；其余值按上表反向转换。

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

如果 `value` 是一个函数（箭头函数、函数表达式），它会被暴露为 JS 中可调用的函数。
JS 调用它时会转回 TS 执行（最多 4 个参数，无 `this`）：

```typescript
quickjs.setGlobal(ctx, 'tag', (s: any) => 'ts:' + s);
console.log(quickjs.eval(ctx, "tag('a')"));                       // "ts:a"
console.log(quickjs.eval(ctx, "[1,2,3].map(function(n){ return tag('n'+n); }).join(',')"));
// "ts:n1,ts:n2,ts:n3"
```

---

### `quickjs.getGlobal(ctx, name)`

读取上下文中的全局绑定。

**示例**:
```typescript
quickjs.eval(ctx, "var greeting = 'hi from js';");
console.log(quickjs.getGlobal(ctx, 'greeting')); // "hi from js"
```

若该绑定是一个 JS 函数，返回的是可调用句柄，可直接传给 `quickjs.call`。

---

### `quickjs.call(ctx, fn, args?)`

调用一个 JS 函数句柄。`fn` 来自 `eval` / `evalFile` / `getGlobal` 的函数结果；
`args` 是可选的可调用参数数组（省略或空数组表示不传参）。JS 的返回值会转回 TS 值。

**参数**:
- `ctx`: 上下文
- `fn`: JS 函数句柄
- `args`: 参数数组（可选，最多 8 个）

**示例**:
```typescript
const double = quickjs.eval(ctx, '(x) => x * 2');
console.log(quickjs.call(ctx, double, [21])); // 42

quickjs.eval(ctx, 'function triple(x) { return x * 3; }');
const triple = quickjs.getGlobal(ctx, 'triple');
console.log(quickjs.call(ctx, triple, [5]));  // 15
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

  // 函数句柄与调用
  const double = quickjs.eval(ctx, '(x) => x * 2');
  console.log(quickjs.call(ctx, double, [21])); // 42

  // TS 函数注入 JS 并在 JS 中调用
  quickjs.setGlobal(ctx, 'tag', (s: any) => 'ts:' + s);
  console.log(quickjs.eval(ctx, "tag('a')"));   // "ts:a"

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
- 使用 `quickjs` 模块时，编译器自动链接 QuickJS 引擎源码并定义 `CONFIG_VERSION`（读取 `runtime/src/quickjs/VERSION`）。
- Windows 下引擎通过 `runtime/src/quickjs/compat/` 的 POSIX 兼容头（`sys/time.h`、`pthread.h`）在 MSVC 目标 clang 上构建，无需 MinGW 工具链；链接时由编译器自动补上 `clang_rt.builtins-*.lib` 与 `compat` 头路径。Linux 下使用系统头并自动加 `-lm -ldl -lpthread`。
- `closure_type` 位域在新版 MSVC 目标 clang 下需为 `unsigned int`（见 `quickjs.c` 中的注释），否则闭包分派会 `abort()`。

## 许可证

QuickJS 采用 MIT 许可证，见 `runtime/src/quickjs/LICENSE`。