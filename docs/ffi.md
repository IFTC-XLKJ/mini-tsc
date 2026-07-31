# ffi 模块 - 外部函数接口

## 概述

`ffi` 模块提供了外部函数接口（Foreign Function Interface），允许调用动态链接库（DLL/so）中的函数。

## 类型定义

```typescript
type FFIType = 'void' | 'int' | 'double' | 'string' | 'pointer';

interface ForeignFunction {
  (...args: any[]): any;
}

interface DynamicLibrary {
  close(): void;
  get(symbol: string): ForeignFunction;
}
```

## API 列表

### `ffi.dlopen(path)`

打开动态链接库。

**参数**:
- `path`: 库文件路径

**返回值**: `DynamicLibrary` 对象

**示例**:
```typescript
import { ffi } from 'ffi';

// Windows
const kernel32 = ffi.dlopen('kernel32.dll');

// Linux
const libc = ffi.dlopen('libc.so.6');

// macOS
const libm = ffi.dlopen('libm.dylib');
```

---

### `ffi.dlsym(handle, symbol)`

获取库中的函数符号。

**参数**:
- `handle`: 库句柄
- `symbol`: 函数名称

**返回值**: `ForeignFunction` 对象

**示例**:
```typescript
const lib = ffi.dlopen('libc.so.6');
const strlen = ffi.dlsym(lib, 'strlen');
console.log(strlen('Hello'));  // 5
```

---

### `ffi.call(funcPtr, returnType, args)`

调用函数指针。

**参数**:
- `funcPtr`: 函数指针
- `returnType`: 返回类型
- `args`: 参数数组

**返回值**: `any` - 函数返回值

**示例**:
```typescript
const lib = ffi.dlopen('libc.so.6');
const strlen = ffi.dlsym(lib, 'strlen');
const result = ffi.call(strlen, 'int', ['Hello World']);
console.log(result);  // 11
```

---

### `ffi.dlclose(handle)`

关闭动态链接库。

**参数**:
- `handle`: 库句柄

**返回值**: `void`

**示例**:
```typescript
const lib = ffi.dlopen('libc.so.6');
// 使用库...
ffi.dlclose(lib);
```

---

## 使用示例

### 调用 C 标准库

```typescript
import { ffi } from 'ffi';

// Linux/macOS: 加载 libc
const libc = ffi.dlopen('libc.so.6');

// 获取 strlen 函数
const strlen = ffi.dlsym(libc, 'strlen');

// 调用函数
const len = ffi.call(strlen, 'int', ['Hello, World!']);
console.log('String length:', len);  // 13

// 关闭库
ffi.dlclose(libc);
```

### Windows API 调用

```typescript
import { ffi } from 'ffi';

// 加载 kernel32.dll
const kernel32 = ffi.dlopen('kernel32.dll');

// 获取 GetTickCount 函数
const GetTickCount = ffi.dlsym(kernel32, 'GetTickCount');

// 调用函数获取系统运行时间
const uptime = ffi.call(GetTickCount, 'int', []);
console.log('System uptime (ms):', uptime);

ffi.dlclose(kernel32);
```

### 调用自定义库

假设有一个 `mathlib.so` 包含以下函数：
```c
// mathlib.c
#include <math.h>

int add(int a, int b) {
    return a + b;
}

double square_root(double x) {
    return sqrt(x);
}

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}
```

编译为共享库：
```bash
# Linux
gcc -shared -o libmathlib.so -fPIC mathlib.c -lm

# Windows
gcc -shared -o mathlib.dll mathlib.c -lm

# macOS
gcc -shared -o libmathlib.dylib -fPIC mathlib.c -lm
```

TypeScript 调用：
```typescript
import { ffi } from 'ffi';

// 加载库
const mathlib = ffi.dlopen('./libmathlib.so');

// 获取函数
const add = ffi.dlsym(mathlib, 'add');
const square_root = ffi.dlsym(mathlib, 'square_root');
const factorial = ffi.dlsym(mathlib, 'factorial');

// 调用函数
console.log('add(5, 3) =', ffi.call(add, 'int', [5, 3]));           // 8
console.log('sqrt(16) =', ffi.call(square_root, 'double', [16]));   // 4
console.log('5! =', ffi.call(factorial, 'int', [5]));               // 120

ffi.dlclose(mathlib);
```

### 封装 FFI 调用

```typescript
import { ffi } from 'ffi';

class MathLib {
  private lib: any;
  private functions: Map<string, any> = new Map();
  
  constructor(path: string) {
    this.lib = ffi.dlopen(path);
  }
  
  private getFunction(name: string, returnType: string, paramTypes: string[]): any {
    if (!this.functions.has(name)) {
      const fn = ffi.dlsym(this.lib, name);
      this.functions.set(name, { fn, returnType, paramTypes });
    }
    return this.functions.get(name);
  }
  
  add(a: number, b: number): number {
    const { fn, returnType, paramTypes } = this.getFunction('add', 'int', ['int', 'int']);
    return ffi.call(fn, returnType, [a, b]);
  }
  
  squareRoot(x: number): number {
    const { fn, returnType } = this.getFunction('square_root', 'double', ['double']);
    return ffi.call(fn, returnType, [x]);
  }
  
  factorial(n: number): number {
    const { fn, returnType } = this.getFunction('factorial', 'int', ['int']);
    return ffi.call(fn, returnType, [n]);
  }
  
  close(): void {
    ffi.dlclose(this.lib);
  }
}

// 使用示例
const math = new MathLib('./libmathlib.so');

console.log('5 + 3 =', math.add(5, 3));
console.log('sqrt(25) =', math.squareRoot(25));
console.log('10! =', math.factorial(10));

math.close();
```

### 调用系统 API 获取信息

```typescript
import { ffi } from 'ffi';

function getSystemInfo(): Record<string, any> {
  const info: Record<string, any> = {};
  
  if (process.platform === 'win32') {
    // Windows API
    const kernel32 = ffi.dlopen('kernel32.dll');
    
    const GetComputerName = ffi.dlsym(kernel32, 'GetComputerNameA');
    const GetUserName = ffi.dlsym(kernel32, 'GetUserNameA');
    
    // 获取计算机名
    const nameBuffer = Buffer.alloc(256);
    const nameSize = Buffer.alloc(4);
    nameSize.writeUInt32LE(256);
    
    ffi.call(GetComputerName, 'int', [nameBuffer, nameSize]);
    info.computerName = nameBuffer.toString('utf-8').replace(/\0/g, '');
    
    ffi.dlclose(kernel32);
  } else {
    // Unix-like 系统
    const libc = ffi.dlopen('libc.so.6');
    
    const gethostname = ffi.dlsym(libc, 'gethostname');
    const hostnameBuffer = Buffer.alloc(256);
    
    ffi.call(gethostname, 'int', [hostnameBuffer, 256]);
    info.hostname = hostnameBuffer.toString('utf-8').replace(/\0/g, '');
    
    ffi.dlclose(libc);
  }
  
  return info;
}

// 使用示例
const sysInfo = getSystemInfo();
console.log('System info:', sysInfo);
```

## 实现细节

### 库加载

- **Windows**: 使用 `LoadLibrary` / `GetProcAddress` / `FreeLibrary`
- **Linux**: 使用 `dlopen` / `dlsym` / `dlclose`
- **macOS**: 使用 `dlopen` / `dlsym` / `dlclose`

### 类型映射

| C 类型 | FFI 类型 | 说明 |
|--------|----------|------|
| `void` | `'void'` | 无返回值 |
| `int`, `long` | `'int'` | 整数 |
| `float`, `double` | `'double'` | 浮点数 |
| `char*`, `const char*` | `'string'` | 字符串 |
| `void*`, 指针 | `'pointer'` | 指针 |

### 内存管理

- 字符串参数会被复制到目标进程
- 返回的字符串需要手动管理内存
- 复杂数据结构可能需要手动分配/释放

## 平台差异

| 功能 | Windows | Linux | macOS |
|------|---------|-------|-------|
| 库扩展名 | `.dll` | `.so` | `.dylib` |
| 库加载 API | `LoadLibrary` | `dlopen` | `dlopen` |
| 符号获取 | `GetProcAddress` | `dlsym` | `dlsym` |
| 库释放 | `FreeLibrary` | `dlclose` | `dlclose` |

## 注意事项

1. **类型安全**: 确保 C 函数的参数和返回类型正确
2. **内存管理**: 注意指针和缓冲区的生命周期
3. **错误处理**: 检查库加载和函数调用是否成功
4. **平台兼容**: 不同平台的库文件不同
5. **ABI 兼容**: 确保编译器和调用约定一致

## 安全建议

1. **验证输入**: 在调用 C 函数前验证所有输入
2. **边界检查**: 确保缓冲区大小足够
3. **空指针检查**: 检查指针是否为 NULL
4. **错误码处理**: 检查 C 函数的返回值
5. **资源释放**: 确保释放所有分配的资源

## 常见错误

### 库加载失败
```
Error: Cannot open library: libc.so.6
```
**解决方案**: 确保库文件存在且可访问

### 符号未找到
```
Error: Symbol not found: nonExistentFunction
```
**解决方案**: 确认函数名称正确，检查库是否导出该符号

### 类型不匹配
```
Error: Invalid argument type
```
**解决方案**: 检查参数类型是否与 C 函数签名匹配
