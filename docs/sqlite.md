# sqlite 模块 - SQLite 数据库

## 概述

`sqlite` 模块提供了 SQLite 数据库的访问功能，支持 SQL 查询、表操作、CRUD 操作等。

## 类型定义

```typescript
interface Database {
  exec(sql: string): void;
  prepare(sql: string): Statement;
  close(): void;
  pragma(source: string): any;
  
  // 便捷 CRUD 方法
  createTable(table: string, columns: ColumnDefinition): void;
  dropTable(table: string): void;
  insert(table: string, row: Record<string, any>): void;
  find(table: string, where?: Record<string, any>, options?: FindOptions): any[];
  findAll(table: string, where?: Record<string, any>, options?: FindOptions): any[];
  findAndCount(table: string, where?: Record<string, any>, options?: FindOptions): { rows: any[]; total: number };
  update(table: string, set: Record<string, any>, where?: Record<string, any>): void;
  remove(table: string, where?: Record<string, any>): void;
  count(table: string, where?: Record<string, any>): number;
}

interface Statement {
  run(params?: any[]): RunResult;
  get(params?: any[]): any | undefined;
  all(params?: any[]): any[];
  iterate(params?: any[]): IterableIterator<any>;
  finalize(): void;
}

interface RunResult {
  changes: number;
  lastInsertRowid: number;
}

interface ColumnDefinition {
  [columnName: string]: string;  // 列名 -> 类型定义
}

interface FindOptions {
  limit?: number;
  offset?: number;
  orderBy?: string;
  order?: 'ASC' | 'DESC';
}
```

## API 列表

### 数据库操作

#### `sqlite.open(filename)`

打开或创建数据库。

**参数**:
- `filename`: 数据库文件路径

**返回值**: `Database` 对象

**示例**:
```typescript
import { sqlite } from 'sqlite';

const db = sqlite.open('mydb.sqlite');
```

---

#### `sqlite.Database(filename)`

创建数据库实例（构造函数形式）。

**参数**:
- `filename`: 数据库文件路径

**返回值**: `Database` 对象

---

#### `db.exec(sql)`

执行 SQL 语句。

**参数**:
- `sql`: SQL 语句

**返回值**: `void`

**示例**:
```typescript
db.exec(`
  CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT UNIQUE
  )
`);
```

---

#### `db.prepare(sql)`

预编译 SQL 语句。

**参数**:
- `sql`: SQL 语句，可以包含 `?` 占位符

**返回值**: `Statement` 对象

**示例**:
```typescript
const stmt = db.prepare('INSERT INTO users (name, email) VALUES (?, ?)');
stmt.run(['Alice', 'alice@example.com']);
stmt.run(['Bob', 'bob@example.com']);
```

---

#### `db.close()`

关闭数据库连接。

**返回值**: `void`

---

#### `db.pragma(source)`

执行 PRAGMA 命令。

**参数**:
- `source`: PRAGMA 语句

**返回值**: `any`

**示例**:
```typescript
db.pragma('journal_mode = WAL');
db.pragma('foreign_keys = ON');
```

---

### Statement 方法

#### `stmt.run(params?)`

执行语句并返回结果。

**参数**:
- `params`: 参数数组

**返回值**: `RunResult`

**示例**:
```typescript
const result = stmt.run(['Alice', 'alice@example.com']);
console.log('Changes:', result.changes);
console.log('Last ID:', result.lastInsertRowid);
```

---

#### `stmt.get(params?)`

查询单行结果。

**参数**:
- `params`: 参数数组

**返回值**: `any` | `undefined`

**示例**:
```typescript
const stmt = db.prepare('SELECT * FROM users WHERE id = ?');
const user = stmt.get([1]);
console.log(user); // { id: 1, name: 'Alice', email: 'alice@example.com' }
```

---

#### `stmt.all(params?)`

查询所有结果。

**参数**:
- `params`: 参数数组

**返回值**: `any[]`

**示例**:
```typescript
const stmt = db.prepare('SELECT * FROM users WHERE name LIKE ?');
const users = stmt.all(['%Alice%']);
console.log(users); // [{ id: 1, name: 'Alice', ... }]
```

---

#### `stmt.iterate(params?)`

返回结果的迭代器。

**参数**:
- `params`: 参数数组

**返回值**: `IterableIterator<any>`

**示例**:
```typescript
const stmt = db.prepare('SELECT * FROM users');
for (const user of stmt.iterate()) {
  console.log(user);
}
```

---

#### `stmt.finalize()`

释放语句资源。

**返回值**: `void`

---

### 便捷 CRUD 方法

#### `db.createTable(table, columns)`

创建表。

**参数**:
- `table`: 表名
- `columns`: 列定义

**返回值**: `void`

**示例**:
```typescript
db.createTable('users', {
  id: 'INTEGER PRIMARY KEY AUTOINCREMENT',
  name: 'TEXT NOT NULL',
  email: 'TEXT UNIQUE',
  created_at: 'DATETIME DEFAULT CURRENT_TIMESTAMP'
});
```

---

#### `db.dropTable(table)`

删除表。

**参数**:
- `table`: 表名

**返回值**: `void`

---

#### `db.insert(table, row)`

插入数据。

**参数**:
- `table`: 表名
- `row`: 要插入的数据

**返回值**: `void`

**示例**:
```typescript
db.insert('users', {
  name: 'Charlie',
  email: 'charlie@example.com'
});
```

---

#### `db.find(table, where?, options?)`

查询单行数据。

**参数**:
- `table`: 表名
- `where`: 条件对象
- `options`: 查询选项

**返回值**: `any` | `undefined`

**示例**:
```typescript
const user = db.find('users', { id: 1 });
console.log(user);

const userByEmail = db.find('users', { email: 'alice@example.com' });
console.log(userByEmail);
```

---

#### `db.findAll(table, where?, options?)`

查询多行数据。

**参数**:
- `table`: 表名
- `where`: 条件对象
- `options`: 查询选项

**返回值**: `any[]`

**示例**:
```typescript
const users = db.findAll('users');
console.log(users);

const limitedUsers = db.findAll('users', {}, { limit: 10, offset: 0 });
console.log(limitedUsers);

const sortedUsers = db.findAll('users', {}, { orderBy: 'name', order: 'ASC' });
console.log(sortedUsers);
```

---

#### `db.findAndCount(table, where?, options?)`

查询数据并返回总数。

**参数**:
- `table`: 表名
- `where`: 条件对象
- `options`: 查询选项

**返回值**: `{ rows: any[]; total: number }`

**示例**:
```typescript
const { rows, total } = db.findAndCount('users', {}, { limit: 10, offset: 0 });
console.log(`Found ${rows.length} of ${total} users`);
```

---

#### `db.update(table, set, where?)`

更新数据。

**参数**:
- `table`: 表名
- `set`: 要更新的字段
- `where`: 条件对象

**返回值**: `void`

**示例**:
```typescript
db.update('users', { name: 'Alice Smith' }, { id: 1 });
```

---

#### `db.remove(table, where?)`

删除数据。

**参数**:
- `table`: 表名
- `where`: 条件对象

**返回值**: `void`

**示例**:
```typescript
db.remove('users', { id: 1 });
```

---

#### `db.count(table, where?)`

统计数据行数。

**参数**:
- `table`: 表名
- `where`: 条件对象

**返回值**: `number`

**示例**:
```typescript
const count = db.count('users');
console.log(`Total users: ${count}`);
```

---

## 使用示例

### 基本 CRUD 操作

```typescript
import { sqlite } from 'sqlite';

// 创建数据库
const db = sqlite.open('app.sqlite');

// 创建表
db.createTable('products', {
  id: 'INTEGER PRIMARY KEY AUTOINCREMENT',
  name: 'TEXT NOT NULL',
  price: 'REAL',
  stock: 'INTEGER DEFAULT 0'
});

// 插入数据
db.insert('products', { name: 'iPhone', price: 999, stock: 100 });
db.insert('products', { name: 'iPad', price: 599, stock: 50 });
db.insert('products', { name: 'MacBook', price: 1999, stock: 30 });

// 查询数据
const product = db.find('products', { name: 'iPhone' });
console.log('Found product:', product);

// 查询所有
const allProducts = db.findAll('products');
console.log('All products:', allProducts);

// 更新数据
db.update('products', { stock: 95 }, { name: 'iPhone' });

// 删除数据
db.remove('products', { name: 'iPad' });

// 统计
const count = db.count('products');
console.log('Product count:', count);

// 关闭数据库
db.close();
```

### 分页查询

```typescript
import { sqlite } from 'sqlite';

interface PaginatedResult<T> {
  data: T[];
  total: number;
  page: number;
  pageSize: number;
  totalPages: number;
}

function paginate<T>(
  db: any,
  table: string,
  page: number,
  pageSize: number,
  where?: Record<string, any>
): PaginatedResult<T> {
  const offset = (page - 1) * pageSize;
  const { rows, total } = db.findAndCount(table, where, {
    limit: pageSize,
    offset
  });
  
  return {
    data: rows,
    total,
    page,
    pageSize,
    totalPages: Math.ceil(total / pageSize)
  };
}

// 使用示例
const db = sqlite.open('app.sqlite');

const result = paginate(db, 'users', 1, 10);
console.log(`Page ${result.page} of ${result.totalPages}`);
console.log(`Showing ${result.data.length} of ${result.total} users`);
```

### 事务处理

```typescript
import { sqlite } from 'sqlite';

function transferFunds(
  db: any,
  fromId: number,
  toId: number,
  amount: number
): void {
  // 使用 SQL 事务
  db.exec('BEGIN TRANSACTION');
  
  try {
    // 扣除转出账户
    const fromAccount = db.find('accounts', { id: fromId });
    if (fromAccount.balance < amount) {
      throw new Error('Insufficient funds');
    }
    
    db.update('accounts', { balance: fromAccount.balance - amount }, { id: fromId });
    
    // 增加转入账户
    const toAccount = db.find('accounts', { id: toId });
    db.update('accounts', { balance: toAccount.balance + amount }, { id: toId });
    
    db.exec('COMMIT');
  } catch (error) {
    db.exec('ROLLBACK');
    throw error;
  }
}

// 使用示例
const db = sqlite.open('bank.sqlite');

db.createTable('accounts', {
  id: 'INTEGER PRIMARY KEY AUTOINCREMENT',
  name: 'TEXT NOT NULL',
  balance: 'REAL DEFAULT 0'
});

db.insert('accounts', { name: 'Alice', balance: 1000 });
db.insert('accounts', { name: 'Bob', balance: 500 });

transferFunds(db, 1, 2, 200);

const alice = db.find('accounts', { id: 1 });
const bob = db.find('accounts', { id: 2 });
console.log('Alice balance:', alice.balance);  // 800
console.log('Bob balance:', bob.balance);      // 700
```

### 数据验证

```typescript
import { sqlite } from 'sqlite';

interface User {
  id?: number;
  name: string;
  email: string;
  age?: number;
}

class UserRepository {
  private db: any;
  
  constructor(db: any) {
    this.db = db;
  }
  
  private validate(user: Partial<User>): void {
    if (!user.name || user.name.trim() === '') {
      throw new Error('Name is required');
    }
    
    if (!user.email || !user.email.includes('@')) {
      throw new Error('Valid email is required');
    }
    
    if (user.age !== undefined && (user.age < 0 || user.age > 150)) {
      throw new Error('Invalid age');
    }
  }
  
  create(user: User): number {
    this.validate(user);
    
    this.db.insert('users', {
      name: user.name,
      email: user.email,
      age: user.age || null
    });
    
    const created = this.db.find('users', { email: user.email });
    return created.id;
  }
  
  update(id: number, updates: Partial<User>): void {
    this.validate(updates);
    this.db.update('users', updates, { id });
  }
  
  findById(id: number): User | undefined {
    return this.db.find('users', { id });
  }
  
  findByEmail(email: string): User | undefined {
    return this.db.find('users', { email });
  }
  
  list(options?: { limit?: number; offset?: number }): User[] {
    return this.db.findAll('users', {}, options);
  }
  
  delete(id: number): void {
    this.db.remove('users', { id });
  }
}

// 使用示例
const db = sqlite.open('users.sqlite');

db.createTable('users', {
  id: 'INTEGER PRIMARY KEY AUTOINCREMENT',
  name: 'TEXT NOT NULL',
  email: 'TEXT UNIQUE',
  age: 'INTEGER'
});

const repo = new UserRepository(db);

const userId = repo.create({
  name: 'Alice',
  email: 'alice@example.com',
  age: 30
});

console.log('Created user ID:', userId);

const user = repo.findById(userId);
console.log('Found user:', user);
```

## 实现细节

### SQL 生成

便捷方法内部生成 SQL 语句：
- `find()` → `SELECT * FROM table WHERE key = ? LIMIT 1`
- `findAll()` → `SELECT * FROM table WHERE ... ORDER BY ... LIMIT ? OFFSET ?`
- `insert()` → `INSERT INTO table (columns) VALUES (?)`
- `update()` → `UPDATE table SET ... WHERE ...`
- `remove()` → `DELETE FROM table WHERE ...`

### 参数绑定

使用 `?` 占位符进行参数绑定，防止 SQL 注入：

```typescript
// 安全 - 使用参数绑定
const stmt = db.prepare('SELECT * FROM users WHERE name = ?');
stmt.run(['Alice']);

// 不安全 - 避免字符串拼接
// `SELECT * FROM users WHERE name = '${name}'`  // SQL 注入风险
```

## 性能优化

1. **预编译语句**: 重复执行的 SQL 使用 `prepare()`
2. **索引**: 为常用查询字段创建索引
3. **事务**: 批量操作使用事务
4. **WAL 模式**: 启用 WAL 提高并发性能

```typescript
// 创建索引
db.exec('CREATE INDEX idx_users_email ON users (email)');

// 启用 WAL 模式
db.pragma('journal_mode = WAL');
```
