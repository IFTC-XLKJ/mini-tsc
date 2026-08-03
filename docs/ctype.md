# ctype 模块 - C 语言类型描述符

## 概述

`ctype` 模块提供了 C 语言数据类型的描述功能，主要用于 FFI（外部函数接口）场景。可以查询类型的大小、对齐方式、获取变量内存地址，以及比较值是否相等。

## 类型定义

### CType 类

CType 类用于表示一个 C 语言类型描述符。

```typescript
class CType {
    name: string;      // 类型名称
    size: number;      // 类型大小（字节）
    alignment: number; // 对齐方式（字节）
    signed: boolean;   // 是否有符号
    
    constructor(name: string, size: number, alignment: number, signed: boolean);
}
```

### 类型别名

| 类型 | 说明 | TypeScript 类型 |
|------|------|-----------------|
| `char` | 字符类型 | `CType \| string` |
| `schar` | 有符号字符 | `CType \| string` |
| `uchar` | 无符号字符 | `CType \| string` |
| `short` | 短整型 | `CType \| number` |
| `ushort` | 无符号短整型 | `CType \| number` |
| `int` | 整型 | `CType \| number` |
| `uint` | 无符号整型 | `CType \| number` |
| `long` | 长整型 | `CType \| number` |
| `ulong` | 无符号长整型 | `CType \| number` |
| `longlong` | 长长整型 | `CType \| number` |
| `ulonglong` | 无符号长长整型 | `CType \| number` |
| `float` | 单精度浮点 | `CType \| number` |
| `double` | 双精度浮点 | `CType \| number` |
| `bool` | 布尔类型 | `CType \| boolean` |
| `pointer` | 指针类型 | `CType \| number` |
| `size_t` | 无符号整型（平台相关） | `CType \| number` |
| `ptrdiff_t` | 指针差值类型 | `CType \| number` |

## API 列表

### `sizeof(type)`

获取值的大小（字节）。

**参数**:
- `type`: 要查询的值

**返回值**: `number` - 值的大小

**示例**:
```typescript
import { sizeof } from "ctype";

const char_val: char = "a";
const int_val: int = 42;

console.log(sizeof(char_val));  // 1
console.log(sizeof(int_val));   // 4
```

---

### `alignof(type)`

获取值的对齐方式（字节）。

**参数**:
- `type`: 要查询的值

**返回值**: `number` - 对齐方式

**示例**:
```typescript
import { alignof } from "ctype";

const char_val: char = "a";
const int_val: int = 42;

console.log(alignof(char_val));  // 1
console.log(alignof(int_val));   // 4
```

---

### `getptr(type)`

获取变量的内存地址（十六进制字符串）。

**参数**:
- `type`: 要查询地址的变量

**返回值**: `string` - 内存地址（如 "0x7ffd5c8a"）

**示例**:
```typescript
import { getptr } from "ctype";

const x: int = 100;
const y: int = 200;

console.log(getptr(x));  // 0x000000A0CECFF390
console.log(getptr(y));  // 0x000000A0CECFF370
```

---

### `equal(a, b)`

比较两个值是否相等。

**参数**:
- `a`: 第一个值
- `b`: 第二个值

**返回值**: `boolean` - 是否相等

**示例**:
```typescript
import { equal } from "ctype";

const a: int = 42;
const b: int = 42;
const c: int = 43;

console.log(equal(a, b));  // true
console.log(equal(a, c));  // false

const s1: char = "hello";
const s2: char = "hello";
const s3: char = "world";

console.log(equal(s1, s2));  // true
console.log(equal(s1, s3));  // false
```

---

### `CType` 构造函数

创建一个 C 类型描述符对象。

**参数**:
- `name`: 类型名称
- `size`: 类型大小（字节）
- `alignment`: 对齐方式（字节）
- `signed`: 是否有符号

**返回值**: `CType` 对象

**示例**:
```typescript
import { CType } from "ctype";

// 创建一个 32 位有符号整数类型
const int32 = new CType("int32", 4, 4, true);
console.log(int32.name);       // "int32"
console.log(int32.size);       // 4
console.log(int32.alignment);  // 4
console.log(int32.signed);     // true
```

---

### `CType.name` 属性

获取类型的名称。

**返回值**: `string` - 类型名称

---

### `CType.size` 属性

获取类型的大小（字节）。

**返回值**: `number` - 类型大小

---

### `CType.alignment` 属性

获取类型的对齐方式（字节）。

**返回值**: `number` - 对齐方式

---

### `CType.signed` 属性

获取类型是否有符号。

**返回值**: `boolean` - 是否有符号

---

## 使用示例

### 基本类型查询

```typescript
import { sizeof, alignof, CType } from "ctype";

function printTypeInfo(val: any, name: string): void {
    console.log(`${name}: size=${sizeof(val)}, align=${alignof(val)}`);
}

// 测试各种类型
printTypeInfo("a", "char");        // char: size=1, align=1
printTypeInfo(42, "int");          // int: size=4, align=4
printTypeInfo(true, "bool");       // bool: size=4, align=4
printTypeInfo(3.14, "float");      // float: size=4, align=4
```

### 获取内存地址

```typescript
import { getptr } from "ctype";

const x: int = 100;
const y: int = 200;
const str: char = "hello";

// 查看变量在内存中的位置
console.log(`x 的地址: ${getptr(x)}`);
console.log(`y 的地址: ${getptr(y)}`);
console.log(`str 的地址: ${getptr(str)}`);
```

### 值比较

```typescript
import { equal } from "ctype";

function findInArray(arr: int[], target: int): boolean {
    for (let i = 0; i < arr.length; i++) {
        if (equal(arr[i], target)) {
            return true;
        }
    }
    return false;
}

const numbers: int[] = [1, 2, 3, 4, 5];
console.log(findInArray(numbers, 3));  // true
console.log(findInArray(numbers, 6));  // false
```

### FFI 类型描述

```typescript
import { CType } from "ctype";

// 定义 C 结构体字段类型
const fieldTypes = {
    id: new CType("int", 4, 4, true),
    name: new CType("char*", 4, 4, false),  // 32 位系统指针大小
    value: new CType("double", 8, 8, true),
};

// 计算结构体大小
function calculateStructSize(fields: CType[]): number {
    let size = 0;
    let maxAlign = 1;
    
    for (const field of fields) {
        // 对齐到当前字段的对齐方式
        const align = field.alignment;
        size = Math.ceil(size / align) * align;
        size += field.size;
        if (align > maxAlign) maxAlign = align;
    }
    
    // 整体对齐到最大对齐方式
    size = Math.ceil(size / maxAlign) * maxAlign;
    return size;
}

console.log(calculateStructSize(Object.values(fieldTypes)));  // 16 (4 + 4 + 8)
```

### 内存调试

```typescript
import { getptr, sizeof } from "ctype";

function debugMemory(label: string, val: any): void {
    console.log(`${label}:`);
    console.log(`  值: ${val}`);
    console.log(`  地址: ${getptr(val)}`);
    console.log(`  大小: ${sizeof(val)} bytes`);
}

// 调试变量内存
const num: int = 42;
const str: char = "Hello, World!";
const flag: bool = true;

debugMemory("数字", num);
debugMemory("字符串", str);
debugMemory("布尔", flag);
```

## 注意事项

1. **内存地址**: `getptr()` 返回的是值在运行时栈上的地址，每次运行可能不同
2. **类型大小**: 返回的大小是运行时 Value 结构体的存储大小，不是 C 语言原始类型的大小
3. **equal 比较**: 对于字符串，比较的是字符串内容；对于对象/数组，比较的是引用地址
4. **性能**: 这些函数主要用于调试和 FFI 场景，不应在性能关键的代码中频繁调用
