# crypto 模块 - 加密功能

## 概述

`crypto` 模块提供了加密相关的功能，包括哈希计算、HMAC、随机数生成、密钥派生等。

## 类型定义

```typescript
type HashAlgorithm = 'md5' | 'sha1' | 'sha256' | 'sha512';

interface Hash {
  update(data: string | Buffer): Hash;
  digest(encoding?: string): string | Buffer;
}

interface Hmac extends Hash {}

type BinaryEncoding = 'hex' | 'base64' | 'binary';
```

## API 列表

### 哈希函数

#### `crypto.createHash(algorithm)`

创建哈希对象。

**参数**:
- `algorithm`: 哈希算法（'md5', 'sha1', 'sha256', 'sha512'）

**返回值**: `Hash` 对象

**示例**:
```typescript
import * as crypto from 'crypto';

const hash = crypto.createHash('sha256');
hash.update('Hello World');
const result = hash.digest('hex');
console.log(result);
```

---

#### `crypto.md5(data)`

计算 MD5 哈希。

**参数**:
- `data`: 要计算的数据

**返回值**: `string` - 十六进制哈希值

**示例**:
```typescript
const hash = crypto.md5('Hello World');
console.log(hash); // "b10a8db164e0754105b7a99be72e3fe5"
```

---

#### `crypto.sha1(data)`

计算 SHA-1 哈希。

**参数**:
- `data`: 要计算的数据

**返回值**: `string` - 十六进制哈希值

---

#### `crypto.sha256(data)`

计算 SHA-256 哈希。

**参数**:
- `data`: 要计算的数据

**返回值**: `string` - 十六进制哈希值

**示例**:
```typescript
const hash = crypto.sha256('Hello World');
console.log(hash); // "a591a6d40bf420404a011733cfb7b190d62c65bf0bcda32b57b277d9ad9f146e"
```

---

#### `crypto.sha512(data)`

计算 SHA-512 哈希。

**参数**:
- `data`: 要计算的数据

**返回值**: `string` - 十六进制哈希值

---

### HMAC 函数

#### `crypto.createHmac(algorithm, key)`

创建 HMAC 对象。

**参数**:
- `algorithm`: 哈希算法
- `key`: 密钥

**返回值**: `Hmac` 对象

**示例**:
```typescript
const hmac = crypto.createHmac('sha256', 'secret-key');
hmac.update('Hello World');
const result = hmac.digest('hex');
console.log(result);
```

---

#### `crypto.hmac_sha256(key, data)`

计算 HMAC-SHA256。

**参数**:
- `key`: 密钥
- `data`: 要计算的数据

**返回值**: `string` - 十六进制哈希值

**示例**:
```typescript
const result = crypto.hmac_sha256('secret', 'message');
console.log(result);
```

---

### 随机数生成

#### `crypto.randomBytes(size)`

生成指定长度的随机字节。

**参数**:
- `size`: 字节数

**返回值**: `Buffer` - 随机字节

**示例**:
```typescript
const random = crypto.randomBytes(16);
console.log(random.toString('hex'));
```

---

#### `crypto.randomUUID()`

生成随机 UUID。

**返回值**: `string` - UUID 字符串

**示例**:
```typescript
const uuid = crypto.randomUUID();
console.log(uuid); // "f47ac10b-58cc-4372-a567-0e02b2c3d479"
```

---

### 密钥派生

#### `crypto.pbkdf2Sync(password, salt, iterations, keylen, digest)`

同步 PBKDF2 密钥派生。

**参数**:
- `password`: 密码
- `salt`: 盐值
- `iterations`: 迭代次数
- `keylen`: 密钥长度
- `digest`: 摘要算法

**返回值**: `Buffer` - 派生的密钥

**示例**:
```typescript
const key = crypto.pbkdf2Sync('password', 'salt', 100000, 32, 'sha256');
console.log(key.toString('hex'));
```

---

#### `crypto.pbkdf2(password, salt, iterations, keylen, digest, callback)`

异步 PBKDF2 密钥派生。

**参数**:
- `password`: 密码
- `salt`: 盐值
- `iterations`: 迭代次数
- `keylen`: 密钥长度
- `digest`: 摘要算法
- `callback`: 回调函数

**返回值**: `void`

---

#### `crypto.scryptSync(password, salt, keylen)`

同步 scrypt 密钥派生。

**参数**:
- `password`: 密码
- `salt`: 盐值
- `keylen`: 密钥长度

**返回值**: `Buffer` - 派生的密钥

**示例**:
```typescript
const key = crypto.scryptSync('password', 'salt', 32);
console.log(key.toString('hex'));
```

---

## 使用示例

### 密码哈希

```typescript
import * as crypto from 'crypto';

// 简单哈希（不推荐用于密码）
function simpleHash(password: string): string {
  return crypto.sha256(password);
}

// 加盐哈希
function hashPassword(password: string, salt?: string): { hash: string; salt: string } {
  if (!salt) {
    salt = crypto.randomBytes(16).toString('hex');
  }
  
  const hash = crypto.pbkdf2Sync(password, salt, 100000, 64, 'sha512');
  return {
    hash: hash.toString('hex'),
    salt
  };
}

// 验证密码
function verifyPassword(password: string, storedHash: string, salt: string): boolean {
  const { hash } = hashPassword(password, salt);
  return hash === storedHash;
}

// 使用示例
const password = 'my-secret-password';
const { hash, salt } = hashPassword(password);
console.log('Hash:', hash);
console.log('Salt:', salt);
console.log('Verify:', verifyPassword(password, hash, salt));
```

### 数据完整性校验

```typescript
import * as crypto from 'crypto';

// 计算文件哈希
function hashData(data: string): string {
  return crypto.sha256(data);
}

// 生成校验和
function generateChecksum(data: string): string {
  return crypto.md5(data);
}

// 验证数据完整性
function verifyChecksum(data: string, expectedChecksum: string): boolean {
  const actualChecksum = generateChecksum(data);
  return actualChecksum === expectedChecksum;
}

// 使用示例
const data = 'Important data';
const checksum = generateChecksum(data);
console.log('Checksum:', checksum);
console.log('Valid:', verifyChecksum(data, checksum));
```

### API 请求签名

```typescript
import * as crypto from 'crypto';

function signRequest(
  method: string,
  path: string,
  body: string,
  secret: string,
  timestamp: number
): string {
  const message = `${method}\n${path}\n${body}\n${timestamp}`;
  return crypto.hmac_sha256(secret, message);
}

function verifySignature(
  method: string,
  path: string,
  body: string,
  secret: string,
  timestamp: number,
  signature: string
): boolean {
  const expected = signRequest(method, path, body, secret, timestamp);
  return expected === signature;
}

// 使用示例
const secret = 'my-api-secret';
const method = 'POST';
const path = '/api/data';
const body = '{"key": "value"}';
const timestamp = Date.now();

const signature = signRequest(method, path, body, secret, timestamp);
console.log('Signature:', signature);

// 验证
const isValid = verifySignature(method, path, body, secret, timestamp, signature);
console.log('Valid:', isValid);
```

### 随机令牌生成

```typescript
import * as crypto from 'crypto';

function generateToken(length: number = 32): string {
  return crypto.randomBytes(length).toString('hex');
}

function generateApiKey(): string {
  return `sk_${generateToken(24)}`;
}

function generateSessionId(): string {
  return generateToken(16);
}

// 使用示例
console.log('Token:', generateToken());
console.log('API Key:', generateApiKey());
console.log('Session ID:', generateSessionId());
```

### 加密解密

```typescript
import * as crypto from 'crypto';

// 简单的对称加密
function encrypt(text: string, key: string): string {
  const cipher = crypto.createHash('sha256');
  cipher.update(key);
  const keyHash = cipher.digest('hex').slice(0, 32);
  
  // 简单的 XOR 加密（仅用于演示，实际应用请使用 AES）
  let result = '';
  for (let i = 0; i < text.length; i++) {
    result += String.fromCharCode(
      text.charCodeAt(i) ^ keyHash.charCodeAt(i % keyHash.length)
    );
  }
  
  return Buffer.from(result).toString('base64');
}

function decrypt(encrypted: string, key: string): string {
  const cipher = crypto.createHash('sha256');
  cipher.update(key);
  const keyHash = cipher.digest('hex').slice(0, 32);
  
  const text = Buffer.from(encrypted, 'base64').toString();
  
  let result = '';
  for (let i = 0; i < text.length; i++) {
    result += String.fromCharCode(
      text.charCodeAt(i) ^ keyHash.charCodeAt(i % keyHash.length)
    );
  }
  
  return result;
}

// 使用示例
const secret = 'my-secret-key';
const message = 'Hello, World!';

const encrypted = encrypt(message, secret);
console.log('Encrypted:', encrypted);

const decrypted = decrypt(encrypted, secret);
console.log('Decrypted:', decrypted);
console.log('Match:', message === decrypted);
```

## 支持的算法

### 哈希算法

| 算法 | 输出长度 | 安全性 | 推荐 |
|------|----------|--------|------|
| MD5 | 128 位 | 低 | 否 |
| SHA-1 | 160 位 | 低 | 否 |
| SHA-256 | 256 位 | 高 | 是 |
| SHA-512 | 512 位 | 高 | 是 |

### HMAC 算法

| 算法 | 用途 |
|------|------|
| HMAC-MD5 | 已弃用 |
| HMAC-SHA1 | 需要兼容性时 |
| HMAC-SHA256 | 推荐 |
| HMAC-SHA512 | 高安全需求 |

### 密钥派生算法

| 算法 | 用途 |
|------|------|
| PBKDF2 | 密码哈希、密钥派生 |
| scrypt | 密码哈希、密钥派生 |

## 安全建议

1. **密码存储**: 使用 PBKDF2 或 scrypt，不要使用简单哈希
2. **盐值**: 始终使用随机盐值
3. **迭代次数**: PBKDF2 至少 100000 次
4. **密钥长度**: SHA-256 至少 32 字节
5. **避免 MD5/SHA-1**: 用于安全目的时避免使用这些算法

## 性能考虑

1. **同步 vs 异步**: 大数据量使用异步版本
2. **内存使用**: 哈希大文件时分块处理
3. **缓存**: 重复计算的结果可以缓存
