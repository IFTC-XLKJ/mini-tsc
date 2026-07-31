# assert 模块 - 断言

## 概述

`assert` 模块提供了断言功能，用于在代码中检查条件是否满足，常用于测试和调试。

## API 列表

### `assert(value, message?)`

断言值为真。

**参数**:
- `value`: 要检查的值
- `message`: 可选，失败时的错误消息

**返回值**: `void`

**示例**:
```typescript
import assert from 'assert';

assert(true);  // 通过
assert(1);     // 通过
assert('hello');  // 通过

assert(false);  // 抛出 AssertionError
assert(0);      // 抛出 AssertionError
assert('');     // 抛出 AssertionError
```

---

### `assert.ok(value, message?)`

与 `assert()` 相同，断言值为真。

**参数**:
- `value`: 要检查的值
- `message`: 可选，失败时的错误消息

**返回值**: `void`

**示例**:
```typescript
assert.ok(user !== null, 'User should not be null');
assert.ok(result.length > 0, 'Result should not be empty');
```

---

### `assert.equal(actual, expected, message?)`

浅比较两个值是否相等（使用 `==`）。

**参数**:
- `actual`: 实际值
- `expected`: 期望值
- `message`: 可选，失败时的错误消息

**返回值**: `void`

**示例**:
```typescript
assert.equal(1 + 1, 2);
assert.equal('hello', 'hello');
assert.equal(true, true);
```

---

### `assert.notEqual(actual, expected, message?)`

浅比较两个值是否不相等（使用 `!=`）。

**参数**:
- `actual`: 实际值
- `expected`: 期望值
- `message`: 可选，失败时的错误消息

**返回值**: `void`

**示例**:
```typescript
assert.notEqual(1, 2);
assert.notEqual('hello', 'world');
```

---

### `assert.strictEqual(actual, expected, message?)`

严格比较两个值是否相等（使用 `===`）。

**参数**:
- `actual`: 实际值
- `expected`: 期望值
- `message`: 可选，失败时的错误消息

**返回值**: `void`

**示例**:
```typescript
assert.strictEqual(1 + 1, 2);
assert.strictEqual('hello', 'hello');
assert.strictEqual(1, '1');  // 失败，类型不同
```

---

### `assert.notStrictEqual(actual, expected, message?)`

严格比较两个值是否不相等（使用 `!==`）。

**参数**:
- `actual`: 实际值
- `expected`: 期望值
- `message`: 可选，失败时的错误消息

**返回值**: `void`

**示例**:
```typescript
assert.notStrictEqual(1, '1');
assert.notStrictEqual(null, undefined);
```

---

### `assert.deepEqual(actual, expected, message?)`

深比较两个值是否相等。

**参数**:
- `actual`: 实际值
- `expected`: 期望值
- `message`: 可选，失败时的错误消息

**返回值**: `void`

**示例**:
```typescript
assert.deepEqual({ a: 1, b: 2 }, { a: 1, b: 2 });
assert.deepEqual([1, 2, 3], [1, 2, 3]);
```

---

### `assert.notDeepEqual(actual, expected, message?)`

深比较两个值是否不相等。

**参数**:
- `actual`: 实际值
- `expected`: 期望值
- `message`: 可选，失败时的错误消息

**返回值**: `void`

**示例**:
```typescript
assert.notDeepEqual({ a: 1 }, { a: 2 });
assert.notDeepEqual([1, 2], [1, 3]);
```

---

### `assert.deepStrictEqual(actual, expected, message?)`

严格深比较两个值是否相等。

**参数**:
- `actual`: 实际值
- `expected`: 期望值
- `message`: 可选，失败时的错误消息

**返回值**: `void`

**示例**:
```typescript
assert.deepStrictEqual({ a: 1, b: 2 }, { a: 1, b: 2 });
assert.deepStrictEqual([1, 2, 3], [1, 2, 3]);
assert.deepStrictEqual({ a: 1 }, { a: '1' });  // 失败
```

---

### `assert.notDeepStrictEqual(actual, expected, message?)`

严格深比较两个值是否不相等。

**参数**:
- `actual`: 实际值
- `expected`: 期望值
- `message`: 可选，失败时的错误消息

**返回值**: `void`

---

### `assert.throws(fn, error?, message?)`

断言函数抛出错误。

**参数**:
- `fn`: 要执行的函数
- `error`: 可选，期望的错误类型或错误对象
- `message`: 可选，失败时的错误消息

**返回值**: `void`

**示例**:
```typescript
// 断言抛出任何错误
assert.throws(() => {
  throw new Error('Something went wrong');
});

// 断言抛出特定类型的错误
assert.throws(() => {
  throw new TypeError('Type error');
}, TypeError);

// 断言抛出包含特定消息的错误
assert.throws(() => {
  throw new Error('File not found');
}, { message: 'File not found' });
```

---

### `assert.doesNotThrow(fn, message?)`

断言函数不抛出错误。

**参数**:
- `fn`: 要执行的函数
- `message`: 可选，失败时的错误消息

**返回值**: `void`

**示例**:
```typescript
assert.doesNotThrow(() => {
  console.log('This should not throw');
});
```

---

### `assert.rejects(asyncFn, error?, message?)`

断言异步函数抛出错误。

**参数**:
- `asyncFn`: 要执行的异步函数
- `error`: 可选，期望的错误类型或错误对象
- `message`: 可选，失败时的错误消息

**返回值**: `Promise<void>`

**示例**:
```typescript
await assert.rejects(async () => {
  throw new Error('Async error');
}, Error);

await assert.rejects(async () => {
  throw new TypeError('Type error');
}, { message: 'Type error' });
```

---

### `assert.doesNotReject(asyncFn, message?)`

断言异步函数不抛出错误。

**参数**:
- `asyncFn`: 要执行的异步函数
- `message`: 可选，失败时的错误消息

**返回值**: `Promise<void>`

---

### `assert.ifError(value)`

如果值为真值则抛出错误。

**参数**:
- `value`: 要检查的值

**返回值**: `void`

**示例**:
```typescript
const err = null;
assert.ifError(err);  // 通过

assert.ifError(new Error('Error'));  // 抛出错误
```

---

### `assert.fail(message?)`

立即抛出 AssertionError。

**参数**:
- `message`: 错误消息

**返回值**: `void`（永远不会返回）

**示例**:
```typescript
function processValue(value: number): void {
  if (value < 0) {
    assert.fail('Value cannot be negative');
  }
  console.log('Value:', value);
}
```

---

## 使用示例

### 单元测试

```typescript
import assert from 'assert';

// 测试函数
function add(a: number, b: number): number {
  return a + b;
}

function multiply(a: number, b: number): number {
  return a * b;
}

// 测试用例
function testMath(): void {
  // 测试加法
  assert.strictEqual(add(1, 2), 3);
  assert.strictEqual(add(-1, 1), 0);
  assert.strictEqual(add(0, 0), 0);
  
  // 测试乘法
  assert.strictEqual(multiply(2, 3), 6);
  assert.strictEqual(multiply(-2, 3), -6);
  assert.strictEqual(multiply(0, 5), 0);
  
  console.log('All tests passed!');
}

testMath();
```

### 数据验证

```typescript
import assert from 'assert';

interface User {
  id: number;
  name: string;
  email: string;
  age: number;
}

function validateUser(user: any): user is User {
  assert.ok(user, 'User is required');
  assert.ok(typeof user.id === 'number', 'User ID must be a number');
  assert.ok(typeof user.name === 'string', 'User name must be a string');
  assert.ok(user.name.length > 0, 'User name cannot be empty');
  assert.ok(typeof user.email === 'string', 'User email must be a string');
  assert.ok(user.email.includes('@'), 'User email must be valid');
  assert.ok(typeof user.age === 'number', 'User age must be a number');
  assert.ok(user.age >= 0 && user.age <= 150, 'User age must be valid');
  
  return true;
}

// 使用示例
const validUser = {
  id: 1,
  name: 'Alice',
  email: 'alice@example.com',
  age: 30
};

const invalidUser = {
  id: '1',  // 错误类型
  name: '',
  email: 'invalid-email',
  age: -5
};

try {
  validateUser(validUser);
  console.log('Valid user');
} catch (e) {
  console.error('Validation failed:', (e as Error).message);
}

try {
  validateUser(invalidUser);
  console.log('Valid user');
} catch (e) {
  console.error('Validation failed:', (e as Error).message);
}
```

### 异步操作测试

```typescript
import assert from 'assert';

// 异步函数
async function fetchData(url: string): Promise<any> {
  // 模拟网络请求
  return new Promise((resolve, reject) => {
    setTimeout(() => {
      if (url.includes('error')) {
        reject(new Error('Network error'));
      } else {
        resolve({ data: 'success' });
      }
    }, 100);
  });
}

// 测试异步函数
async function testFetch(): Promise<void> {
  // 测试成功情况
  const result = await fetchData('https://api.example.com/data');
  assert.deepStrictEqual(result, { data: 'success' });
  
  // 测试失败情况
  await assert.rejects(
    () => fetchData('https://api.example.com/error'),
    { message: 'Network error' }
  );
  
  console.log('Async tests passed!');
}

testFetch();
```

### 自定义错误消息

```typescript
import assert from 'assert';

function divide(a: number, b: number): number {
  assert.notStrictEqual(b, 0, 'Division by zero is not allowed');
  return a / b;
}

try {
  divide(10, 0);
} catch (e) {
  console.error((e as Error).message);  // "Division by zero is not allowed"
}
```

### 条件断言

```typescript
import assert from 'assert';

function processArray(arr: number[]): void {
  assert.ok(Array.isArray(arr), 'Input must be an array');
  assert.ok(arr.length > 0, 'Array must not be empty');
  
  for (const item of arr) {
    assert.ok(typeof item === 'number', 'All items must be numbers');
    assert.ok(!isNaN(item), 'Items must not be NaN');
  }
  
  console.log('Array is valid');
}

processArray([1, 2, 3]);

try {
  processArray([]);
} catch (e) {
  console.error((e as Error).message);
}
```

## AssertionError

当断言失败时，会抛出 `AssertionError`：

```typescript
import assert from 'assert';

try {
  assert.strictEqual(1, 2, 'Numbers should be equal');
} catch (e) {
  if (e instanceof assert.AssertionError) {
    console.error('Assertion failed:', e.message);
    console.error('Actual:', e.actual);
    console.error('Expected:', e.expected);
    console.error('Operator:', e.operator);
  }
}
```

## 性能考虑

1. **生产环境**: 在生产代码中避免使用 assert，或使用条件编译
2. **开销**: 断言检查有性能开销，不要在循环中使用
3. **替代方案**: 对于性能关键的代码，使用条件检查代替断言

```typescript
// 不推荐在生产代码中使用
function criticalFunction(data: any): void {
  assert.ok(data, 'Data is required');
  // ...
}

// 更好的做法
function criticalFunction(data: any): void {
  if (!data) {
    throw new Error('Data is required');
  }
  // ...
}
```
