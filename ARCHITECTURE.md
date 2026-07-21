# mini-tsc Architecture

TypeScript-to-C transpiler: parse TS → generate C → compile with clang → native executable.

## Project Structure

```
mini-tsc/
├── src/
│   ├── cli/
│   │   └── index.ts              # CLI entry point (commander/yargs)
│   ├── driver/
│   │   └── compiler.ts           # Orchestrates the full pipeline
│   ├── parser/
│   │   └── ts-parser.ts          # Wraps TS Compiler API, creates Program
│   ├── visitor/
│   │   ├── ast-visitor.ts         # Recursive AST traversal dispatcher
│   │   └── node-handlers.ts       # Per-Kind handler functions
│   ├── types/
│   │   ├── type-mapper.ts         # TS type → C type mapping
│   │   └── type-info.ts           # Resolved type IR (intermediate representation)
│   ├── modules/
│   │   ├── module-resolver.ts     # Import/export analysis + dependency graph
│   │   ├── symbol-mangler.ts      # TS symbol → C symbol name mangling
│   │   └── module-init.ts         # Topological sort + lazy init ordering
│   ├── codegen/
│   │   ├── c-emitter.ts           # High-level: walks IR, emits .c files
│   │   ├── header-emitter.ts      # Generates .h declaration files
│   │   ├── statement-emitter.ts   # Transpiles statements to C
│   │   ├── expression-emitter.ts  # Transpiles expressions to C
│   │   └── type-emitter.ts        # Emits C type declarations (structs, enums, unions)
│   ├── builtins/
│   │   ├── node-fs.ts             # fs module → C implementation
│   │   ├── node-path.ts           # path module → C implementation
│   │   ├── node-process.ts        # process module → C implementation
│   │   ├── node-http.ts           # http module → C implementation (libcurl)
│   │   ├── node-net.ts            # net module → C implementation (sockets)
│   │   ├── node-os.ts             # os module → C implementation
│   │   ├── node-child-process.ts  # child_process → C implementation (fork/exec)
│   │   └── registry.ts            # Maps "fs" → fsCImpl, etc.
│   └── runtime/
│       ├── runtime.c              # Core runtime (TSString, TSArray, Value, HashMap)
│       ├── runtime.h              # Runtime API header
│       ├── value.c                # Tagged union Value type operations
│       ├── string.c               # TSString implementation
│       ├── array.c                # TSArray implementation
│       ├── hashmap.c              # HashMap implementation
│       ├── closure.c              # Closure / function pointer support
│       ├── vtable.c               # Virtual dispatch table
│       ├── gc.c                   # Simple mark-sweep garbage collector
│       └── builtins.c             # console.log, Math.*, Date, JSON.*
├── runtime/
│   ├── include/
│   │   └── ts_runtime.h           # Public API header for runtime
│   └── src/
│       ├── value.c
│       ├── string_ops.c
│       ├── array_ops.c
│       ├── hashmap.c
│       ├── closure.c
│       ├── vtable.c
│       ├── gc.c
│       └── builtins.c
├── test/
│   ├── fixtures/                  # .ts input files
│   ├── expected/                  # Expected .c output
│   └── integration/              # Full compile + run tests
├── ARCHITECTURE.md
├── package.json
├── tsconfig.json
└── tsconfig.build.json
```

## Pipeline Overview

```
┌──────────┐    ┌───────────┐    ┌──────────────┐    ┌────────────┐    ┌───────┐
│ TS Files  │───▶│  Parser   │───▶│   Visitor +   │───▶│  Module    │───▶│  C    │
│ (input)   │    │ (ts API)  │    │  Type Mapper  │    │  Resolver  │    │ CodeGen│
└──────────┘    └───────────┘    └──────────────┘    └────────────┘    └───┬───┘
                                                                          │
                                                                  ┌───────▼───────┐
                                                                  │ .c + .h files │
                                                                  └───────┬───────┘
                                                                          │
                                                                  ┌───────▼───────┐
                                                                  │    clang      │
                                                                  │  (compile)    │
                                                                  └───────┬───────┘
                                                                          │
                                                                  ┌───────▼───────┐
                                                                  │  executable   │
                                                                  └───────────────┘
```

---

## Component Design

### 1. CLI (`src/cli/index.ts`)

**Dependencies**: 内置 commander 模块 (`src/cli/commander/`), driver/compiler

```ts
interface CliOptions {
  entry: string;           // Entry .ts file
  output?: string;         // Output executable name
  outDir?: string;         // Output directory for .c/.h files
  target?: "windows" | "linux";  // Cross-compilation target
  runtime?: boolean;       // Include runtime library (default: true)
  verbose?: boolean;       // Print intermediate C code
  keepC?: boolean;         // Keep generated .c/.h files
  clangArgs?: string[];    // Extra args to pass to clang
}
```

**Approach**: 使用内置 commander 模块 (`src/cli/commander/`) 解析参数，构造 `CliOptions`，传递给 `CompilerDriver.compile()`。

### 2. Compiler Driver (`src/driver/compiler.ts`)

**Dependencies**: parser, visitor, modules, codegen, builtins

```ts
class CompilerDriver {
  async compile(options: CliOptions): Promise<void> {
    // 1. Parse TS → create Program + TypeChecker
    // 2. Analyze modules: build dependency graph
    // 3. Topological sort → determine compilation order
    // 4. For each TS source file:
    //    a. Visit AST → build intermediate IR
    //    b. Map types → C types
    //    c. Emit .c + .h files
    // 5. Emit runtime library .c/.h (or copy pre-compiled)
    // 6. Emit builtin shim .c/.h files for used Node modules
    // 7. Invoke clang to compile all .c files → link → executable
  }
}
```

**Key decisions**:
- Single-pass per file (no cross-file AST transforms)
- Each file independently transpiled; cross-file linking handled by module resolver + symbol mangling
- clang invoked as child process with appropriate flags

### 3. Parser (`src/parser/ts-parser.ts`)

**Dependencies**: typescript

```ts
interface ParseResult {
  program: ts.Program;
  checker: ts.TypeChecker;
  sourceFiles: ts.SourceFile[];
  diagnostics: ts.Diagnostic[];
}

class TsParser {
  parse(entryFile: string, options?: ts.CompilerOptions): ParseResult;
}

function defaultCompilerOptions(): ts.CompilerOptions {
  return {
    target: ts.ScriptTarget.ES2020,
    module: ts.ModuleKind.CommonJS,
    strict: true,
    esModuleInterop: true,
    skipLibCheck: true,
    declaration: false,
    outDir: "./out",
    rootDir: ".",
    moduleResolution: ts.ModuleResolutionKind.NodeJs,
  };
}
```

**Approach**: Use `ts.findConfigFile` to locate tsconfig.json if present; fall back to defaults. Create `ts.createProgram`, extract type checker, collect all non-declaration source files.

### 4. AST Visitor (`src/visitor/`)

**Dependencies**: parser, types, modules

```ts
// ast-visitor.ts
class AstVisitor {
  constructor(
    private checker: ts.TypeChecker,
    private typeMapper: TypeMapper,
    private moduleResolver: ModuleResolver,
  ) {}

  visitSourceFile(sourceFile: ts.SourceFile): TranspilationUnit;
}

// node-handlers.ts — one handler per TS SyntaxKind
type NodeHandler = (
  node: ts.Node,
  context: VisitorContext,
) => CNode | null;

interface VisitorContext {
  checker: ts.TypeChecker;
  typeMapper: TypeMapper;
  moduleResolver: ModuleResolver;
  currentFile: string;
  scope: ScopeStack;          // Tracks variable declarations in scope
  output: CNode[];            // Accumulated C output nodes
}

// Scope stack for name resolution
interface ScopeStack {
  push(): void;
  pop(): void;
  declare(name: string, type: ResolvedType): void;
  lookup(name: string): ResolvedType | undefined;
}
```

**Handled node kinds** (initial set):
- `SourceFile`, `FunctionDeclaration`, `ClassDeclaration`, `InterfaceDeclaration`, `EnumDeclaration`
- `VariableStatement`, `VariableDeclaration`, `VariableDeclarationList`
- `ExpressionStatement`, `ReturnStatement`, `IfStatement`, `WhileStatement`, `ForStatement`, `ForOfStatement`, `ForInStatement`
- `Block`, `BlockScopedVariableDeclaration`
- `CallExpression`, `PropertyAccessExpression`, `ElementAccessExpression`
- `BinaryExpression`, `UnaryExpression`, `PrefixUnaryExpression`, `PostfixUnaryExpression`
- `ArrowFunction`, `FunctionExpression`
- `ObjectLiteralExpression`, `ArrayLiteralExpression`
- `TemplateExpression`, `TaggedTemplateExpression`
- `AsExpression`, `TypeAssertionExpression`
- `ImportDeclaration`, `ExportDeclaration`, `ExportAssignment`
- `ModuleDeclaration`, `ModuleBlock`
- `TypeAliasDeclaration`, `TypeReference`, `FunctionType`, `ConstructorType`
- `Parameter`, `PropertySignature`, `PropertyDeclaration`, `MethodDeclaration`, `GetAccessor`, `SetAccessor`

### 5. Type Mapper (`src/types/type-mapper.ts`)

**Dependencies**: typescript (for TypeChecker)

```ts
// type-info.ts — IR for resolved types
interface ResolvedType {
  kind: TypeKind;
  cType: string;            // The C type string
  cHeader?: string;         // Required header for this type
}

type TypeKind =
  | "number"       // → double
  | "string"       // → TSString*
  | "boolean"      // → int (0/1)
  | "void"         // → void
  | "null"         // → void* (NULL)
  | "undefined"    // → void* (NULL)
  | "never"        // → _Noreturn or void
  | "any"          // → Value (tagged union)
  | "unknown"      // → Value (tagged union)
  | "object"       // → struct or Value
  | "interface"    // → struct
  | "class"        // → struct + vtable pointer
  | "enum"         // → C enum or int
  | "union"        // → tagged union struct
  | "intersection" // → merged struct
  | "tuple"        // → struct with positional fields
  | "array"        // → TSArray*
  | "map"          // → TSHashMap*
  | "function"     // → function pointer + closure struct
  | "generic"      // → specialized per type args
  | "optional"     // → T* (pointer, NULL = absent)
  | "literal"      // → C literal constant
  | "symbol"       // → int (symbol registry)
  | "promise"      // → TBD (callback-based)
  | "custom";      // → user-defined struct

interface ResolvedInterface {
  name: string;
  fields: { name: string; type: ResolvedType }[];
}

interface ResolvedClass {
  name: string;
  fields: { name: string; type: ResolvedType }[];
  vtable: VTableEntry[];
  extends?: string;
  implements?: string[];
}

interface VTableEntry {
  methodName: string;
  cFunctionName: string;
  signature: string;
}

class TypeMapper {
  mapType(tsType: ts.Type, location?: ts.Node): ResolvedType;
  mapInterface(declaration: ts.InterfaceDeclaration): ResolvedInterface;
  mapClass(declaration: ts.ClassDeclaration): ResolvedClass;
  mapUnion(types: ts.Type[]): ResolvedType;
  mapTuple(elements: ts.TypeNode[]): ResolvedType;
  mapFunctionType(sigature: ts.Signature): ResolvedType;
  getGenericSpecialization(typeArgs: ts.Type[]): string; // generates unique suffix
}
```

**Type mapping detail**:

| TypeScript | C Type | Notes |
|---|---|---|
| `number` | `double` | IEEE 754 double precision |
| `string` | `TSString*` | Heap-allocated, ref-counted |
| `boolean` | `int` | 0 = false, 1 = true |
| `void` | `void` | |
| `null` / `undefined` | `void*` | NULL sentinel |
| `never` | `void` | Unreachable |
| `any` / `unknown` | `Value` | Tagged union at runtime |
| `interface Foo` | `struct Foo` | Fields as members |
| `class Foo` | `struct Foo` + vtable | First field = vtable ptr |
| `enum E` | `enum E { ... }` | or `int` if const enum |
| `union A \| B` | `struct { int tag; union { A a; B b; } }` | Tagged union |
| `tuple [A, B]` | `struct { A _0; B _1; }` | Positional fields |
| `T[]` | `TSArray*` | Dynamic array |
| `Record<K,V>` | `TSHashMap*` | Hash map |
| `(a: A) => R` | `R (*fn)(Value closure, A)` | Closure struct + fn ptr |
| `T?` (optional) | `T*` | NULL = absent |
| `Promise<T>` | `TSString*` (sync fallback) | Or callback-based |

### 6. Module Resolver (`src/modules/`)

```ts
// symbol-mangler.ts
class SymbolMangler {
  // utils/math.ts + export "add" → "utils_math_add"
  mangle(filePath: string, exportName: string): string;

  // Default export → "module_name__default"
  mangleDefault(filePath: string): string;

  // Class method: "ClassName_methodName"
  mangleMethod(className: string, methodName: string): string;

  // Re-export: just forward the original mangled name
  mangleReExport(sourceFile: string, originalName: string): string;
}

// module-resolver.ts
interface ModuleGraph {
  nodes: ModuleNode[];
  edges: ModuleEdge[];
  cycles: string[][];           // Detected circular dependencies
  sortedOrder: string[];        // Topological order for init
}

interface ModuleNode {
  filePath: string;
  imports: string[];            // Resolved file paths
  exports: ExportEntry[];
}

interface ExportEntry {
  name: string;
  isDefault: boolean;
  isType: boolean;
  mangledName: string;
}

class ModuleResolver {
  buildGraph(sourceFiles: ts.SourceFile[], checker: ts.TypeChecker): ModuleGraph;
  topoSort(graph: ModuleGraph): string[];
  detectCycles(graph: ModuleGraph): string[][];
}

// module-init.ts
class ModuleInitializer {
  // For DAG (no cycles): generate __init_<moduleName>() called in topological order
  generateDagInit(graph: ModuleGraph): string;

  // For cycles: generate lazy-init with function pointers
  // static int _init_done = 0;
  // static Value (*_lazy_fn)(void) = NULL;
  // Value get_module_export(void) {
  //   if (!_init_done) { _lazy_fn(); _init_done = 1; }
  //   return cached_value;
  // }
  generateLazyInit(cycleGroup: string[]): string;
}
```

**Symbol mangling rules**:
- Path segments joined with `_`: `src/utils/math.ts` → `src_utils_math`
- Export name appended: `src_utils_math_add`
- Default export: `src_utils_math__default`
- Class + method: `MyClass_constructor`, `MyClass_doWork`
- Private: `_MyClass_privateField`
- Generic specialization: `add_i32` (type suffix)
- Escape: `$` for reserved C words (`if` → `$if`)

### 7. C Code Generator (`src/codegen/`)

```ts
// c-emitter.ts
interface EmitResult {
  files: EmitFile[];
  diagnostics: EmitDiagnostic[];
}

interface EmitFile {
  path: string;      // e.g., "out/src/utils/math.c"
  content: string;
  kind: "c" | "h";
}

class CEmitter {
  emitUnit(unit: TranspilationUnit, moduleInfo: ModuleInfo): EmitResult;
}

// header-emitter.ts
class HeaderEmitter {
  emitHeader(
    moduleName: string,
    exports: ExportEntry[],
    typeDefinitions: CNode[],
  ): string;
  // Generates:
  // #ifndef SRC_UTILS_MATH_H
  // #define SRC_UTILS_MATH_H
  // #include "ts_runtime.h"
  // struct Point { double x; double y; };
  // double utils_math_add(double a, double b);
  // #endif
}

// statement-emitter.ts
class StatementEmitter {
  emit(node: CNode): string;
  emitFunctionDecl(node: CFunctionDecl): string;
  emitClassDecl(node: CClassDecl): string;
  emitIfStatement(node: CIfStatement): string;
  emitForStatement(node: CForStatement): string;
  emitReturnStatement(node: CReturnStatement): string;
  emitVariableDecl(node: CVariableDecl): string;
  emitAssignment(node: CAssignment): string;
}

// expression-emitter.ts
class ExpressionEmitter {
  emit(node: CNode): string;
  emitCall(node: CCallExpression): string;
  emitBinary(node: CBinaryExpression): string;
  emitUnary(node: CUnaryExpression): string;
  emitPropertyAccess(node: CPropertyAccess): string;
  emitLiteral(node: CLiteral): string;
  emitIdentifier(node: CIdentifier): string;
  emitCast(node: CCastExpression): string;   // TS type assertions → C casts
  emitNewExpression(node: CNewExpression): string;
  emitArrayLiteral(node: CArrayLiteral): string;
  emitObjectLiteral(node: CObjectLiteral): string;
}

// type-emitter.ts
class TypeEmitter {
  emitStruct(name: string, fields: { name: string; type: string }[]): string;
  emitEnum(name: string, values: { name: string; value?: number }[]): string;
  emitUnion(name: string, variants: { tag: number; type: string }[]): string;
  emitTypedef(original: string, alias: string): string;
  emitFunctionPtr(name: string, params: string[], returnType: string): string;
}
```

### 8. Runtime Library (`runtime/`)

**C files compiled separately, linked into final binary**.

```c
// runtime.h — Public API
#ifndef TS_RUNTIME_H
#define TS_RUNTIME_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// Tagged union Value type
typedef enum {
  TAG_NUMBER, TAG_STRING, TAG_BOOLEAN, TAG_NULL,
  TAG_OBJECT, TAG_ARRAY, TAG_FUNCTION, TAG_SYMBOL
} ValueTag;

typedef struct Value {
  ValueTag tag;
  union {
    double number;
    int boolean;
    struct TSString* string;
    struct TSObject* object;
    struct TSArray* array;
    void* function;      // Closure*
    int symbol;
  } as;
} Value;

// String type (UTF-16 like JS, or UTF-8 for simplicity)
typedef struct TSString {
  int32_t refcount;
  int32_t length;         // byte length
  char* data;             // UTF-8 encoded
} TSString;

TSString* ts_string_new(const char* cstr);
TSString* ts_string_concat(TSString* a, TSString* b);
int ts_string_equals(TSString* a, TSString* b);
void ts_string_free(TSString* s);

// Array type
typedef struct TSArray {
  int32_t refcount;
  int32_t length;
  int32_t capacity;
  Value* items;
} TSArray;

TSArray* ts_array_new(void);
void ts_array_push(TSArray* arr, Value val);
Value ts_array_get(TSArray* arr, int32_t index);
void ts_array_set(TSArray* arr, int32_t index, Value val);

// HashMap
typedef struct TSHashMap {
  int32_t refcount;
  int32_t size;
  // ... internal bucket array
} TSHashMap;

TSHashMap* ts_hashmap_new(void);
void ts_hashmap_set(TSHashMap* map, TSString* key, Value val);
Value ts_hashmap_get(TSHashMap* map, TSString* key);
int ts_hashmap_has(TSHashMap* map, TSString* key);

// Closure
typedef struct Closure {
  void* function_ptr;
  Value* captured_vars;   // captured environment
  int32_t captured_count;
} Closure;

Closure* ts_closure_new(void* fn, Value* captures, int32_t count);
Value ts_closure_call(Closure* closure, Value* args, int32_t arg_count);

// VTable for class dispatch
typedef struct VTable {
  void* methods[];        // function pointers
} VTable;

// Garbage collector (runtime/src/gc.c) — automatic mark-sweep
// Triggers: allocation threshold, event-loop idle, explicit gc() / ts_gc_collect()
// Roots: explicit list + conservative stack scan + main executable data sections
void ts_gc_init(void);
void ts_gc_set_stack_bottom(void* bottom);
void* ts_gc_alloc(size_t size);
void* ts_gc_alloc_kind(size_t size, GcKind kind);
void ts_gc_collect(void);
void ts_gc_maybe_collect(void);
void ts_gc_maybe_collect_idle(void);

// console.log, Math, Date, JSON
void ts_console_log(Value val);
Value ts_math_random(void);
double ts_math_floor(double x);
double ts_math_ceil(double x);
double ts_math_round(double x);
Value ts_json_parse(TSString* json);
Value ts_json_stringify(Value val);

// Type coercion helpers
double ts_to_number(Value val);
TSString* ts_to_string(Value val);
int ts_to_boolean(Value val);

// null/undefined
Value ts_undefined(void);
Value ts_null(void);

#endif
```

### 9. Node Builtins (`src/builtins/`)

```ts
// registry.ts
interface BuiltinModule {
  name: string;
  headerFile: string;       // e.g., "node_fs.h"
  cSourceFile: string;      // e.g., "node_fs.c"
  functions: BuiltinFunction[];
}

interface BuiltinFunction {
  tsName: string;           // e.g., "readFileSync"
  cName: string;            // e.g., "node_fs_readFileSync"
  signature: string;        // e.g., "Value node_fs_readFileSync(Value path, Value options)"
}

const BUILTIN_REGISTRY: Map<string, BuiltinModule> = new Map([
  ["fs", {
    name: "fs",
    headerFile: "node_fs.h",
    cSourceFile: "node_fs.c",
    functions: [
      { tsName: "readFileSync", cName: "node_fs_readFileSync", signature: "..." },
      { tsName: "writeFileSync", cName: "node_fs_writeFileSync", signature: "..." },
      { tsName: "existsSync", cName: "node_fs_existsSync", signature: "..." },
      { tsName: "mkdirSync", cName: "node_fs_mkdirSync", signature: "..." },
      // ... more fs functions
    ],
  }],
  ["path", {
    name: "path",
    headerFile: "node_path.h",
    cSourceFile: "node_path.c",
    functions: [
      { tsName: "join", cName: "node_path_join", signature: "..." },
      { tsName: "resolve", cName: "node_path_resolve", signature: "..." },
      { tsName: "basename", cName: "node_path_basename", signature: "..." },
      { tsName: "extname", cName: "node_path_extname", signature: "..." },
    ],
  }],
  // http, net, process, os, child_process...
]);
```

**Implementation approach per module**:

| Module | C Implementation |
|---|---|
| `fs` | POSIX `fopen/fread/fwrite/fclose` + `stat/mkdir/unlink` |
| `path` | String manipulation (path separators differ by OS) |
| `process` | `getenv`, `argc/argv`, exit codes |
| `http` | libcurl (cross-platform HTTP) |
| `net` | POSIX sockets (`socket/bind/listen/accept/connect`) |
| `os` | `sysinfo`, `uname`, platform detection |
| `child_process` | `fork()` (Linux) / `CreateProcess` (Windows) |

### 10. Cross-Platform Strategy (`src/codegen/c-emitter.ts`)

```ts
interface PlatformConfig {
  platform: "windows" | "linux";
  pathSeparator: string;      // "\\" vs "/"
  executableExtension: string; // ".exe" vs ""
  socketHeader: string;       // <winsock2.h> vs <sys/socket.h>
  processApi: string;         // CreateProcess vs fork/exec
  clangFlags: string[];       // Platform-specific clang args
}

function getPlatformConfig(target: string): PlatformConfig;
```

**clang invocation**:
```ts
// Linux
clang -o output out/*.c runtime/src/*.c -I runtime/include -lm -lpthread

// Windows (via MinGW cross-compile or native clang)
clang -o output.exe out/*.c runtime/src/*.c -I runtime/include -lws2_32
```

---

## Generic Specialization

Generics are handled via **monomorphization** — each unique type argument combination generates a specialized C function.

```ts
// TypeScript
function identity<T>(x: T): T { return x; }
identity<number>(42);
identity<string>("hello");

// Generated C
double identity_double(double x) { return x; }
TSString* identity_TSString(TSString* x) { return x; }
```

**Strategy**:
1. During AST visit, when encountering a generic call `fn<TypeArgs>()`, record the specialization
2. TypeMapper generates a unique suffix per type combination
3. CodeGen emits one C function per specialization
4. Generic interface/class → one struct per specialization

```ts
interface GenericSpecialization {
  originalName: string;
  typeArgs: ResolvedType[];
  mangledName: string;        // e.g., "identity_double", "identity_TSString"
}
```

---

## Closure Implementation

Closures require capturing the lexical environment:

```ts
// TypeScript
function outer() {
  let x = 10;
  return function inner() { return x; };
}

// Generated C
typedef struct {
  double x;                   // captured variable
} outer_inner_env;

double inner_fn(void* _env) {
  outer_inner_env* env = (outer_inner_env*)_env;
  return env->x;
}

Value outer(void) {
  double x = 10;
  outer_inner_env* env = malloc(sizeof(outer_inner_env));
  env->x = x;
  Closure* c = ts_closure_new(inner_fn, env, sizeof(outer_inner_env));
  return (Value){ .tag = TAG_FUNCTION, .as.function = c };
}
```

---

## Error Handling

TS exceptions → C `setjmp/longjmp`:

```c
#include <setjmp.h>

typedef struct {
  jmp_buf jump_buffer;
  Value error_value;
} TsErrorContext;

extern TsErrorContext _ts_current_error;

#define TS_TRY if (setjmp(_ts_current_error.jump_buffer) == 0)
#define TS_CATCH else
#define TS_THROW(val) do { _ts_current_error.error_value = val; longjmp(_ts_current_error.jump_buffer, 1); } while(0)
```

---

## tsconfig.json

```json
{
  "name": "mini-tsc",
  "compilerOptions": {
    "target": "ES2022",
    "module": "NodeNext",
    "moduleResolution": "NodeNext",
    "outDir": "./dist",
    "rootDir": "./src",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "forceConsistentCasingInFileNames": true,
    "resolveJsonModule": true,
    "declaration": true,
    "declarationMap": true,
    "sourceMap": true
  },
  "include": ["src/**/*"],
  "exclude": ["node_modules", "dist", "test", "runtime"]
}
```

## package.json (dependencies)

```json
{
  "name": "mini-tsc",
  "version": "0.1.0",
  "type": "module",
  "main": "dist/cli/index.js",
  "bin": {
    "mini-tsc": "dist/cli/index.js"
  },
  "scripts": {
    "build": "tsc -p tsconfig.build.json",
    "dev": "tsx src/cli/index.ts",
    "test": "node --test dist/**/*.test.js",
    "start": "node dist/cli/index.js"
  },
  "dependencies": {
    "typescript": "^5.4.0",
    "commander": "^12.0.0"
  },
  "devDependencies": {
    "@types/node": "^20.0.0",
    "@types/commander": "^2.12.0",
    "tsx": "^4.7.0",
    "typescript": "^5.4.0"
  }
}
```

## Build / Run Workflow

```bash
# 1. Install dependencies
npm install

# 2. Build the transpiler itself
npm run build

# 3. Transpile a TS file
node dist/cli/index.js input.ts -o output --verbose

# 4. This produces:
#    out/input.c + out/input.h      (generated C code)
#    runtime/include/ts_runtime.h   (runtime header)
#    runtime/src/*.c                (runtime source)
#    out/node_fs.c, out/node_path.c (builtin shims, if used)

# 5. clang compiles everything:
#    clang -o output out/*.c runtime/src/*.c -I runtime/include -lm -lpthread

# 6. Run the native executable
./output
```

---

## Implementation Order (Recommended)

| Phase | Components | Goal |
|---|---|---|
| **P0** | Parser, Driver, CLI skeleton | Parse TS, dump AST |
| **P1** | Type Mapper (primitives), CodeGen (expressions, statements) | Transpile simple functions |
| **P2** | Runtime (Value, TSString, TSArray) | Basic runtime support |
| **P3** | Module Resolver + Symbol Mangler | Multi-file compilation |
| **P4** | CodeGen (classes, interfaces, enums) | OOP features |
| **P5** | Closures + Generics (monomorphization) | Advanced type features |
| **P6** | Node Builtins (fs, path, process) | Useful programs |
| **P7** | Error handling (setjmp/longjmp) | try/catch support |
| **P8** | Cross-platform (Windows support) | Full portability |
| **P9** | GC, Promises, advanced builtins | Completeness |
