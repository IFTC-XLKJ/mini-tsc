# 7zip 模块 - 7z 归档解压

## 概述

`7zip` 模块基于内置的 [7-Zip](https://www.7-zip.org/) 解码器（`runtime/src/7zip/`，仅含解压所需的最小源码子集），用于在 mini-tsc 编译出的原生程序中**读取 / 解压 `.7z` 归档**。它是**只读**的：只能列出与解压，不能创建或写入归档。支持**加密归档**（7zAES AES-256-CBC 内容加密，见下文“加密归档”）。

模块提供两层 API：

- **一次性便捷层**：每次调用内部打开并关闭归档，适合简单场景（`list` / `readFile` / `extractToDir` / `info` …）。
- **句柄层**：`open` 一次、多次读取（`handleList` / `handleRead` / `handleExtractAll` …），最后 `close`。

> 句柄层是**函数式**的：句柄作为第一个参数传入（`7zip.handleCount(h)`），而不是 `h.count()`。这是为了避开转译器基于方法名的分发歧义。

所有错误处理都是**优雅降级**的：失败时返回 `null` / `false` / `-1`（不会抛出异常），可通过 `lastError()` 取回原因。

## 加密归档（7zAES 密码）

所有**一次性**函数以及 `open` 都接受一个**可选末位 `password` 参数**：

```typescript
sevenZip.list(path, password?)
sevenZip.readFile(path, name, password?)
sevenZip.extractToDir(path, dir, password?)
sevenZip.open(path, password?)
// … 其余一次性函数同理
```

- 省略该参数（或传 `undefined` / 空串）= 按无密码处理，行为与旧版完全一致（向后兼容）。
- 传入密码即启用 **7zAES（AES-256-CBC）内容解密**：解压前先用密码派生密钥解密打包流，再交给内层解码器。
- 句柄层在 `open` 时确定密码，之后所有 `handle*` 调用复用该密码（句柄层函数不再单独接受密码）。

支持的加密深度：**仅内容加密（7zAES AES-256-CBC 打包流）**。**加密文件头**（文件名被加密、`EncodedHeader`）**不在支持范围**，这类归档会以 `lastError()` 报错并返回 `null`。

错误契约（与无密码路径一致，均**不抛出**）：

| 情况 | 返回 | `lastError()` |
|---|---|---|
| 未提供密码但归档已加密 | `null` | `unsupported compression method` |
| 密码错误 | `null` | `wrong password (CRC check failed)` |
| 密码为空串 | `null` | 同“未提供密码” |

> 7zAES 采用 AES-CBC，本身**没有认证标签**，因此错误密码解出的是**等长乱码**，只能靠归档内存储的 CRC 检出。`lastError()` 因此在“有密码但 CRC 失败”时特意报告 **`wrong password (CRC check failed)`**，以区别于真正的数据损坏。若归档未存 CRC，则错误密码**无法被检测**（会返回乱码内容）——这与 7-Zip 自身的行为一致。

```typescript
import * as sevenZip from '7zip';

const archive = 'secret.7z';
console.log(sevenZip.readFile(archive, 'secret.txt'));               // null
console.log(sevenZip.readFile(archive, 'secret.txt', 'nope'));       // null, lastError() 含 "password"
console.log(sevenZip.readFile(archive, 'secret.txt', 's3cret-pw'));  // "top secret payload"
sevenZip.extractToDir(archive, './out', 's3cret-pw');                // 3

const h = sevenZip.open(archive, 's3cret-pw');
console.log(sevenZip.handleRead(h, 'secret.txt'));
sevenZip.close(h);
```

## API 分组索引

| 分组 | 函数 |
|---|---|
| 一次性读取 | `list`, `readFile`, `readFileBuffer`, `getEntry`, `entryNames`, `size`, `hasEntry`, `info` |
| 一次性解压 | `extractToDir`, `extractMatching`, `extractFile` |
| 检测与错误 | `isArchive`, `lastError` |
| 句柄：打开 / 关闭 | `open`, `close`, `handleIsOpen` |
| 句柄：读取 | `handleList`, `handleNames`, `handleRead`, `handleReadBuffer`, `handleGetEntry`, `handleSize`, `handleHas`, `handleCount`, `handleInfo` |
| 句柄：解压 | `handleExtractAll`, `handleExtractMatching`, `handleExtract` |

### 条目对象 `ArchiveEntry`

`list()` / `getEntry()` / `handleList()` / `handleGetEntry()` 返回的对象字段：

| 字段 | 类型 | 说明 |
|---|---|---|
| `name` | `string` | 归档内的相对路径（`/` 分隔） |
| `size` | `number` | 解压后字节数 |
| `isDir` | `boolean` | 是否为目录条目 |
| `mtime` | `number \| null` | 修改时间（Unix 秒）；归档未存则为 `null` |
| `cTime` | `number \| null` | 创建时间（Unix 秒）；缺失为 `null` |
| `attrib` | `number` | 原始属性字（高 16 位为 DOS `FILE_ATTRIBUTE_*`；置位 31 时低 16 位为 Unix mode）；缺失为 `0` |
| `index` | `number` | 在归档中的序号 |

### 摘要对象 `ArchiveInfo`

`info()` / `handleInfo()` 返回：`{ count, fileCount, dirCount, totalSize }`。

## API 列表

> 下方各**一次性**函数的签名均接受**可选末位 `password` 参数**（如 `list(archivePath, password?)`、`readFile(archivePath, entryName, password?)`），用于解密 7zAES 加密归档；为简洁起见示例中省略。详见上文“加密归档”。

### `7zip.list(archivePath)`

列出归档中的全部条目，不进行解压。

**参数**: `archivePath` — 归档文件路径
**返回值**: `ArchiveEntry[]`

```typescript
import * as sevenZip from '7zip';

const entries = sevenZip.list('app.7z');
for (let i = 0; i < entries.length; i++) {
  console.log(entries[i].name, entries[i].size, entries[i].isDir);
}
```

---

### `7zip.readFile(archivePath, entryName)`

把一个条目读入内存，以 **UTF-8 字符串**返回。

```typescript
console.log(sevenZip.readFile('app.7z', 'config.json'));
```

---

### `7zip.readFileBuffer(archivePath, entryName)`

与 `readFile` 相同，但以**二进制安全的 Buffer**返回原始字节（不会被 `\0` 截断）。

```typescript
const buf = sevenZip.readFileBuffer('app.7z', 'logo.png');
console.log(buf.length);      // 原始字节数
console.log(buf.toString('hex')); // 十六进制
```

---

### `7zip.getEntry(archivePath, entryName)`

返回单个条目的元数据对象，不存在时返回 `null`。

```typescript
const e = sevenZip.getEntry('app.7z', 'data.json');
if (e !== null) console.log(e.size, e.attrib);
```

---

### `7zip.entryNames(archivePath)`

只返回条目名数组（无元数据）。

```typescript
console.log(sevenZip.entryNames('app.7z')); // [ 'sub', 'data.json', 'hello.txt', ... ]
```

---

### `7zip.size(archivePath, entryName)`

返回条目的解压后字节数；不存在时返回 `-1`。

```typescript
console.log(sevenZip.size('app.7z', 'hello.txt')); // 23
console.log(sevenZip.size('app.7z', 'nope.txt'));  // -1
```

---

### `7zip.hasEntry(archivePath, entryName)`

判断归档是否包含指定条目，返回 `boolean`。

```typescript
console.log(sevenZip.hasEntry('app.7z', 'hello.txt')); // true
```

---

### `7zip.info(archivePath)`

返回归档级摘要对象 `ArchiveInfo`。

```typescript
const info = sevenZip.info('app.7z');
console.log(info.count, info.fileCount, info.dirCount, info.totalSize);
```

---

### `7zip.extractToDir(archivePath, destDir)`

把全部条目解压到 `destDir`（不存在则自动创建，含子目录）。返回**写入的文件数**（目录条目不计入）；归档无法打开或解压失败时返回 `null`（原因见 `lastError()`）。

```typescript
const n = sevenZip.extractToDir('app.7z', './out'); // 例如 3
```

> 条目名会被**安全化**后再拼接输出路径：绝对路径、盘符（`C:`）与 `..` 组件都会被中和，确保解压结果不会逃逸出 `destDir`（防 zip-slip / 路径穿越）。路径过长时拒绝解压并返回 `null`，不会静默截断。

---

### `7zip.extractMatching(archivePath, prefix, destDir)`

只解压名字等于 `prefix` 或位于 `prefix/` 之下的条目，适合只取某个子目录。`prefix` 按**完整路径分量**匹配（`"sub"` 匹配 `sub/nested.txt`，但不匹配 `subx/y.txt`）；传空串等同 `extractToDir`。返回写入的文件数，失败返回 `null`。

```typescript
sevenZip.extractMatching('app.7z', 'sub', './out-sub'); // 只解压 sub/ 子树
```

---

### `7zip.extractFile(archivePath, entryName, outPath)`

把单个条目解压到显式输出路径（自动创建父目录）。

```typescript
sevenZip.extractFile('app.7z', 'data.json', './out/one.json');
```

---

### `7zip.isArchive(archivePath)`

判断路径存在且以 7z 签名（`37 7A BC AF 27 1C`）开头。使用原始文件读取，不启动解码器。

```typescript
console.log(sevenZip.isArchive('app.7z'));   // true
console.log(sevenZip.isArchive('main.ts'));  // false
```

---

### `7zip.lastError()`

返回最近一次失败的原因字符串；无失败时为空字符串 `""`。

```typescript
const missing = sevenZip.readFile('app.7z', 'nope.txt'); // null
console.log(sevenZip.lastError()); // "entry not found in archive"
```

---

## 句柄层

`open()` 打开一次归档并返回不透明句柄，随后用 `handle*` 函数反复读取，最后 `close()` 释放。适合需要多次访问同一归档的场景（避免反复解析头部）。

### `7zip.open(archivePath)` / `7zip.close(handle)`

```typescript
const h = sevenZip.open('app.7z');
// ... handle* 调用 ...
sevenZip.close(h);
```

### `7zip.handleIsOpen(handle)`

句柄是否仍指向一个打开的归档（`close` 之后为 `false`）。

### `7zip.handleList(handle)` / `7zip.handleNames(handle)`

全部条目对象 / 全部条目名。

### `7zip.handleRead(handle, entryName)` / `7zip.handleReadBuffer(handle, entryName)`

把条目读入内存（字符串 / Buffer）。

### `7zip.handleGetEntry(handle, entryName)` / `7zip.handleSize(handle, entryName)` / `7zip.handleHas(handle, entryName)`

单个条目的元数据 / 大小（`-1` 表示不存在）/ 是否存在。

### `7zip.handleCount(handle)` / `7zip.handleInfo(handle)`

条目数量 / 归档级摘要。

### `7zip.handleExtractAll(handle, destDir)` / `7zip.handleExtractMatching(handle, prefix, destDir)` / `7zip.handleExtract(handle, entryName, outPath)`

从已打开的归档解压全部 / 指定子树 / 单个条目。`handleExtractAll` 与 `handleExtractMatching` 返回写入的文件数；句柄已关闭或失败时返回 `null`。

```typescript
const h = sevenZip.open('app.7z');
console.log(sevenZip.handleCount(h));            // 4
console.log(sevenZip.handleHas(h, 'hello.txt')); // true
console.log(sevenZip.handleRead(h, 'hello.txt'));
sevenZip.handleExtract(h, 'data.json', './out/data.json');
sevenZip.close(h);
```

---

## 完整示例

```typescript
import * as sevenZip from '7zip';
import * as fs from 'fs';

function main(): void {
  const archive = 'app.7z';

  // 检测
  console.log(sevenZip.isArchive(archive)); // true

  // 列出
  const entries = sevenZip.list(archive);
  for (let i = 0; i < entries.length; i++) {
    const e = entries[i];
    console.log(e.name, e.size, e.isDir, e.mtime);
  }

  // 单条目读取（字符串 / 二进制）
  console.log(sevenZip.readFile(archive, 'config.json'));
  const png = sevenZip.readFileBuffer(archive, 'logo.png');
  console.log(png.length);

  // 元数据
  console.log(sevenZip.size(archive, 'config.json'));   // 例如 17
  console.log(sevenZip.hasEntry(archive, 'missing.txt')); // false
  const info = sevenZip.info(archive);
  console.log(info.count, info.fileCount, info.dirCount, info.totalSize);

  // 解压
  const n = sevenZip.extractToDir(archive, './out');
  console.log('extracted files:', n);
  sevenZip.extractMatching(archive, 'assets', './out-assets'); // 只取 assets/ 子树
  sevenZip.extractFile(archive, 'config.json', './out/config.json');

  // 错误降级
  const missing = sevenZip.readFile(archive, 'nope.txt'); // null
  console.log(missing === null, sevenZip.lastError());

  // 句柄层
  const h = sevenZip.open(archive);
  console.log(sevenZip.handleIsOpen(h));                 // true
  console.log(sevenZip.handleNames(h).length);           // 全部条目名
  sevenZip.handleExtractAll(h, './out-all');
  sevenZip.close(h);
  console.log(sevenZip.handleIsOpen(h));                 // false
}

main();
```

---

## 实现说明

- 引擎源码位于 `runtime/src/7zip/`（7-Zip ANSI-C 发布版，公有领域 / LGPL 混合许可），已按单目录**扁平化**存放；仅链接解压所需的最小 `.c` 子集（`7zAlloc.c 7zArcIn.c 7zBuf.c 7zBuf2.c 7zCrc.c 7zCrcOpt.c 7zDec.c 7zFile.c 7zStream.c CpuArch.c Delta.c Bcj2.c Bra.c Bra86.c BraIA64.c LzmaDec.c Lzma2Dec.c Ppmd7.c Ppmd7Dec.c`），加密归档额外链接 `7zAesDec.c Aes.c AesOpt.c Sha256.c Sha256Opt.c`。编码器相关文件不参与链接。
- 包装层为 `runtime/src/builtins/node_7zip.c`，通过 `registry.ts` 注册为内置模块。
- **7zAES 支持是对 vendored 解码器的受控扩展**（该子集原本是 decode-only 且无密码概念）：
  - `runtime/src/7zip/7zAesDec.c` / `.h`（新增）实现 7zAES 解码：coder 属性解析、SHA-256 KDF、UTF-8→UTF-16LE 口令编码、AES-256-CBC 原地解密。
  - `7zDec.c` 新增 AES 感知的解码包装：识别 folder 中的 7zAES coder（在真实的 7-Zip 归档里它是 **coder[0]**，即打包流的消费者），解密打包流后**移除 AES coder**（`SzFolder_StripCoder`），再把余下的解码器链跑在解密后的内存流（`CZ7MemLook`）上。AES 的声明输出长度即明文长度（CBC 的填充被忽略）。
  - 原有公开函数签名保留为传入 `NULL` 口令的薄包装（`SzFolder_Decode` / `SzArEx_Extract`），使 `Util/7z/7zMain.c` 等调用者仍可编译。这些改动使 vendored 树**刻意偏离**上游，若重新同步上游需重新施加。
- 使用 `7zip` 模块时，编译器自动：
  - 把上述 `.c` 文件加入链接集；
  - 追加 `-I<runtime>/runtime/src/7zip` 与 `-DZ7_PPMD_SUPPORT`；
  - 因 `readFileBuffer` / `handleReadBuffer` 返回 Buffer，自动启用 runtime 的 Buffer 特性。
- 条目名在归档内为 UTF-16LE，包装层负责转换为 UTF-8（含代理对）。
- `mtime` / `cTime` 来自 Windows FILETIME，按 `win/1e7 - 11644473600` 换算为 Unix 秒。
- 错误**不抛出**：这是刻意设计。runtime 的 `TS_THROW` 只在 `TS_TRY` 块内安全，而内置调用无法保证外层总有 `TS_TRY`，因此失败一律返回 `null` / `false` / `-1` 并记录到 `lastError()`。
- 句柄层的 `handle*` 函数在句柄已 `close()` 后也遵循同一原则：返回 `null` / `false` / `-1`，并把 `lastError()` 置为 `"archive is not open"`；重复 `close()` 是安全的空操作。
- 解压前会对每个条目名做**安全化**（去除盘符与前导分隔符、把 `..` 分量替换为 `_`），输出路径拼接后必定位于 `destDir` 内，防止恶意归档借路径穿越写出目标目录。

## 许可证

7-Zip 采用公有领域（大部分源码）与 GNU LGPL（部分）混合许可，详见 `runtime/src/7zip/` 内的许可文件。