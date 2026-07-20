import * as ts from "typescript";
import * as path from "path";
import * as fs from "fs";
import { parseTypeScript } from "../parser/ts-parser.js";
import { TypeMapper } from "../types/type-mapper.js";
import { ModuleResolver, type ModuleGraph, type ModuleInfo } from "../modules/module-resolver.js";
import { ModuleInitializer } from "../modules/module-init.js";
import { SymbolMangler } from "../modules/symbol-mangler.js";
import { AstVisitor, type VisitorContext, type IScopeStack } from "../visitor/ast-visitor.js";
import { CEmitter, type EmitFile, type CNode, type TranspilationUnit } from "../codegen/c-emitter.js";
import { StatementEmitter } from "../codegen/statement-emitter.js";
import { getBuiltinModule, type BuiltinModule } from "../builtins/registry.js";
import {
  analyzeFeatureUsage,
  generateFeaturesHeader,
  wrapFunctionsWithFeatureGuards,
  allBuiltinCNames,
  type FeatureUsage,
} from "./feature-usage.js";

export interface CompilerOptions {
  entry: string;
  output?: string;
  outDir?: string;
  target?: "windows" | "linux";
  runtime?: boolean;
  verbose?: boolean;
  keepC?: boolean;
  clangArgs?: string[];
  projectRoot?: string;
}

export interface CompilerResult {
  success: boolean;
  files: EmitFile[];
  diagnostics: string[];
  verbose: string[];
  outputPath?: string;
}

/** Simple scope stack for variable resolution */
class ScopeStack implements IScopeStack {
  private scopes: Map<string, any>[] = [new Map()];

  push(): void {
    this.scopes.push(new Map());
  }

  pop(): void {
    this.scopes.pop();
  }

  declare(name: string, type: any): void {
    const current = this.scopes[this.scopes.length - 1];
    current.set(name, type);
  }

  lookup(name: string): any {
    for (let i = this.scopes.length - 1; i >= 0; i--) {
      const val = this.scopes[i].get(name);
      if (val !== undefined) return val;
    }
    return undefined;
  }
}

export class CompilerDriver {
  private mangler = new SymbolMangler();

  async compile(options: CompilerOptions): Promise<CompilerResult> {
    const diagnostics: string[] = [];
    const verbose: string[] = [];

    // 1. Parse TypeScript
    const entryPath = path.resolve(options.entry);
    const { program, checker, sourceFiles, diagnostics: parseDiags } =
      parseTypeScript(entryPath);

    for (const diag of parseDiags) {
      diagnostics.push(ts.flattenDiagnosticMessageText(diag.messageText, "\n"));
    }

    if (diagnostics.length > 0) {
      return { success: false, files: [], diagnostics, verbose: [] };
    }

    // 2. Build module graph
    const moduleResolver = new ModuleResolver();
    const graph = moduleResolver.buildGraph(sourceFiles, checker);

    // 3. Initialize components
    const typeMapper = new TypeMapper(checker);
    const moduleInit = new ModuleInitializer();
    const cEmitter = new CEmitter();

    // 4. Visit AST for each source file (emit after usage analysis)
    const allFiles: EmitFile[] = [];
    const allUnits: TranspilationUnit[] = [];
    const usedBuiltins = new Set<string>();
    const unitModuleInfos: { unit: TranspilationUnit; moduleInfo: ModuleInfo }[] = [];

    for (const filePath of graph.sortedOrder) {
      const sourceFile = sourceFiles.find(
        sf => sf.fileName === filePath || sf.fileName.endsWith(filePath)
      );
      if (!sourceFile) continue;

      if (options.verbose) {
        verbose.push(`Transpiling: ${filePath}`);
      }

      // Visit AST
      const visitorCtx: VisitorContext = {
        checker,
        program,
        typeMapper,
        moduleResolver: graph,
        mangler: this.mangler,
        currentFile: filePath,
        scope: new ScopeStack(),
        output: [],
      };

      const visitor = new AstVisitor(visitorCtx);
      const unit = visitor.visitSourceFile(sourceFile);
      allUnits.push(unit);

      // Track used builtins (import-based + global identifier usage like process.stdout)
      const moduleNode = graph.nodes.get(filePath);
      if (moduleNode) {
        for (const builtin of moduleNode.usedBuiltins) {
          usedBuiltins.add(builtin);
        }
      }
      const globalBuiltins = ["fs", "path", "process", "os", "http", "net", "child_process", "events", "readline", "assert"];
      for (const builtin of globalBuiltins) {
        if (cEmitter.unitUsesBuiltin(unit, builtin)) {
          usedBuiltins.add(builtin);
        }
      }

      // Build module info
      const exports = moduleNode?.exports || [];

      // If this is the entry file and has a main function, ensure it's exported
      const isEntryFile = filePath === graph.sortedOrder[graph.sortedOrder.length - 1] ||
                          !graph.sortedOrder.some(f => {
                            const node = graph.nodes.get(f);
                            return node?.imports.includes(filePath);
                          });

      if (isEntryFile) {
        // Check if main function exists and is not already exported
        const hasMainExport = exports.some(e => e.name === "main");
        if (!hasMainExport) {
          // Add entry to exports (renamed from main to avoid conflict with C's main)
          const moduleName = this.filePathToModuleName(filePath);
          exports.push({
            name: "entry",
            isDefault: false,
            isType: false,
            mangledName: `${moduleName}_entry`,
          });
        }
      }

      const moduleInfo: ModuleInfo = {
        filePath,
        exports,
        imports: moduleNode?.imports.map(i => ({
          filePath: i,
          symbols: [],
        })) || [],
      };

      unitModuleInfos.push({ unit, moduleInfo });
    }

    // 4b. Tree-shake: analyze which modules/methods/features are actually used
    // Always include `process` so main() can call node_process_set_argv.
    usedBuiltins.add("process");
    const featureUsage = analyzeFeatureUsage(allUnits, usedBuiltins);
    // Merge import-based modules into usage
    for (const b of usedBuiltins) featureUsage.modules.add(b);

    if (options.verbose) {
      verbose.push(
        `Tree-shake modules: ${[...featureUsage.modules].sort().join(", ") || "(none)"}`,
      );
      verbose.push(
        `Tree-shake features: ${[...featureUsage.features].sort().join(", ") || "(none)"}`,
      );
      verbose.push(
        `Tree-shake methods: ${featureUsage.methods.size} symbol(s)`,
      );
    }

    // Emit C only after we know which builtin headers to include
    cEmitter.setUsedBuiltinModules(featureUsage.modules);
    for (const { unit, moduleInfo } of unitModuleInfos) {
      allFiles.push(...cEmitter.emitUnit(unit, moduleInfo));
    }

    // 5. Generate runtime header + per-program feature flags
    allFiles.push(...this.generateRuntimeFiles(featureUsage));

    // 6. Generate builtin C files for used Node modules only (method-level guards).
    // Special-case: if `process` is only needed for set_argv (main always calls it),
    // emit a 3-line stub instead of the full node_process.c unit.
    const processOnlyArgv =
      featureUsage.modules.has("process") &&
      [...featureUsage.methods].every(
        m => m === "node_process_set_argv" || !m.startsWith("node_process_"),
      ) &&
      featureUsage.methods.has("node_process_set_argv") &&
      ![...featureUsage.methods].some(
        m => m.startsWith("node_process_") && m !== "node_process_set_argv",
      );

    for (const builtinName of featureUsage.modules) {
      if (builtinName === "process" && processOnlyArgv) {
        allFiles.push({
          path: "node_process_stub.c",
          content: [
            '#include "runtime.h"',
            "void node_process_set_argv(int argc, char** argv) { (void)argc; (void)argv; }",
            "",
          ].join("\n"),
          kind: "c",
        });
        continue;
      }
      const builtin = getBuiltinModule(builtinName);
      if (builtin) {
        allFiles.push(...this.generateBuiltinFiles(builtin, featureUsage));
      }
    }

    // 7. Generate module init code
    const initCode = moduleInit.generateInitSequence(graph);
    allFiles.push({
      path: "module_init.c",
      content: initCode,
      kind: "c",
    });

    // 8. Generate main.c
    // Find the entry unit for try/catch detection
    const entryFilePath = graph.sortedOrder[graph.sortedOrder.length - 1];
    const entryUnit = allUnits.find(u => u.filePath === entryFilePath);
    const mainCode = this.generateMainC(graph, options, entryUnit, featureUsage);
    allFiles.push({
      path: "main.c",
      content: mainCode,
      kind: "c",
    });

    // 9. Write files and invoke clang if output path specified
    if (options.output) {
      try {
        await this.writeAndCompile(allFiles, options, featureUsage);
      } catch (e: any) {
        diagnostics.push(`clang error: ${e.message || e}`);
      }
    }

    // Compute output executable path
    let outputPath: string | undefined;
    if (options.output) {
      const outDir = options.outDir || "./out";
      const isWindows = (options.target || process.platform) === "win32" || options.target === "windows";
      const ext = isWindows ? ".exe" : "";
      if (path.isAbsolute(options.output)) {
        outputPath = options.output + ext;
      } else {
        outputPath = path.join(outDir, options.output + ext);
      }
    }

    return { success: diagnostics.length === 0, files: allFiles, diagnostics, verbose, outputPath };
  }

  private generateRuntimeFiles(usage: FeatureUsage): EmitFile[] {
    return [
      {
        path: "runtime.h",
        content: this.getRuntimeHeader(usage),
        kind: "h",
      },
      {
        path: "ts_features.h",
        content: generateFeaturesHeader(usage),
        kind: "h",
      },
    ];
  }

  private getRuntimeHeader(_usage?: FeatureUsage): string {
    // Declarations for optional features stay present (cheap); bodies in
    // runtime.c / builtins.c are gated via TS_NEED_* from ts_features.h.
    return `#ifndef TS_RUNTIME_H
#define TS_RUNTIME_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <setjmp.h>

/* CommonJS module globals (defined in generated main.c) */
extern const char* __ts_dirname;
extern const char* __ts_filename;

/* Value type — tagged union */
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
    void* object;
    struct TSArray* array;
    void* function;
    int symbol;
  } as;
} Value;

/* TSString */
typedef struct TSString {
  int32_t refcount;
  int32_t length;
  char* data;
} TSString;

TSString* ts_string_new(const char* cstr);
TSString* ts_string_new_len(const char* data, int32_t len);
TSString* ts_string_concat(TSString* a, TSString* b);
int ts_string_equals(TSString* a, TSString* b);
TSString* ts_number_to_string(double n);
char ts_string_char_at(TSString* s, int32_t index);
void ts_string_free(TSString* s);
int ts_string_index_of(TSString* haystack, TSString* needle);
TSString* ts_string_substring(TSString* s, int32_t start, int32_t end);
TSString* ts_string_to_lower(TSString* s);
TSString* ts_string_to_upper(TSString* s);
TSString* ts_string_trim(TSString* s);
int ts_string_starts_with(TSString* s, TSString* prefix);
int ts_string_ends_with(TSString* s, TSString* suffix);
int ts_string_includes(TSString* haystack, TSString* needle);
TSString* ts_string_replace(TSString* s, TSString* search, TSString* replacement);

/* TSArray */
typedef struct TSArray {
  int32_t refcount;
  int32_t length;
  int32_t capacity;
  Value* items;
} TSArray;

TSArray* ts_array_new(void);
TSArray* ts_array_from_values(Value* values, int32_t count);
void ts_array_push(TSArray* arr, Value val);
Value ts_array_get(TSArray* arr, int32_t index);
void ts_array_set(TSArray* arr, int32_t index, Value val);
int32_t ts_array_index_of(TSArray* arr, Value val);
void ts_array_free(TSArray* arr);
TSArray* ts_array_filter(TSArray* arr, int (*predicate)(Value));
TSArray* ts_array_map(TSArray* arr, Value (*fn)(Value));
TSString* ts_array_join(TSArray* arr, TSString* separator);
Value ts_array_reduce(TSArray* arr, Value (*fn)(Value, Value), Value init);
void ts_array_foreach(TSArray* arr, void (*callback)(Value));
int ts_array_some(TSArray* arr, int (*predicate)(Value));
int ts_array_every(TSArray* arr, int (*predicate)(Value));
Value ts_array_find(TSArray* arr, int (*predicate)(Value));
TSArray* ts_string_split(TSString* s, TSString* separator);

/* TSHashMap */
typedef struct HashEntry {
  TSString* key;
  Value value;
  int occupied;
} HashEntry;

struct TSHashMap {
  int32_t refcount;
  int32_t size;
  int32_t capacity;
  HashEntry* entries;
};

typedef struct TSHashMap TSHashMap;
TSHashMap* ts_hashmap_new(void);
void ts_hashmap_set(TSHashMap* map, TSString* key, Value val);
Value ts_hashmap_get(TSHashMap* map, TSString* key);
int ts_hashmap_has(TSHashMap* map, TSString* key);
TSString* ts_hashmap_to_string(TSHashMap* map);
void ts_hashmap_for_each(TSHashMap* map, void (*callback)(TSString* key, Value value, void* ctx), void* ctx);
int32_t ts_hashmap_count(TSHashMap* map);
void ts_hashmap_free(TSHashMap* map);

/* Closure */
typedef struct Closure {
  void* function_ptr;
  Value* captured_vars;
  int32_t captured_count;
} Closure;

Closure* ts_closure_new(void* fn, Value* captures, int32_t count);
Value ts_closure_call(Closure* closure, Value* args, int32_t arg_count);
void ts_closure_free(Closure* closure);

/* Garbage collector */
void ts_gc_init(void);
void* ts_gc_alloc(size_t size);
void ts_gc_collect(void);

/* Value constructors */
Value ts_value_number(double n);
Value ts_value_string(TSString* s);
Value ts_value_boolean(int b);
Value ts_value_null(void);
Value ts_value_undefined(void);
Value ts_value_array(TSArray* arr);
Value ts_value_object(void* obj);
Value ts_value_function(void* fn);

/* Type coercion */
double ts_to_number(Value val);
TSString* ts_to_string(Value val);
int ts_to_boolean(Value val);
TSString* ts_inspect(Value val);

/* Builtin functions */
void ts_console_log(Value val);
void ts_console_info(Value val);
void ts_console_warn(Value val);
void ts_console_error(Value val);
void ts_console_time(TSString* label);
void ts_console_time_end(TSString* label);

/* Timers */
double ts_set_timeout(Value callback, Value delayMs, Value* args, int argc);
double ts_set_interval(Value callback, Value delayMs, Value* args, int argc);
void ts_clear_timeout(Value id);
void ts_clear_interval(Value id);
void ts_timers_run(void);
int ts_timers_pending(void);

/* Browser-like dialogs */
void ts_alert(Value message);
int ts_confirm(Value message);
Value ts_prompt(Value message);

Value ts_typeof(Value val);
void ts_throw(Value val);

/* Error */
Value ts_error_new(TSString* message);

/* Promise (stub - full implementation pending) */
Value Promise_constructor(Value executor);

/* Math builtins */
Value ts_math_random(void);
double ts_math_floor(double x);
double ts_math_ceil(double x);
double ts_math_round(double x);
double ts_math_abs(double x);
double ts_math_sqrt(double x);
double ts_math_pow(double base, double exp);
double ts_math_max(double a, double b);
double ts_math_min(double a, double b);
double ts_math_log(double x);
double ts_math_log2(double x);
double ts_math_log10(double x);
double ts_math_sin(double x);
double ts_math_cos(double x);
double ts_math_tan(double x);
double ts_math_asin(double x);
double ts_math_acos(double x);
double ts_math_atan(double x);
double ts_math_atan2(double y, double x);

/* Date */
typedef struct {
  double timestamp;
} Date;

double date_now_ts(void);

/* Buffer */
typedef struct {
  int32_t type_tag;  /* 0x42554646 = 'BUFF' */
  uint8_t* data;
  int32_t length;
  int32_t capacity;
} Buffer;

#define BUFFER_TAG 0x42554646

Value ts_buffer_new(int32_t size);
Value ts_buffer_from_string(TSString* str);
Value ts_buffer_from_array(TSArray* arr);
Value ts_buffer_alloc(int32_t size);
Value ts_buffer_allocUnsafe(int32_t size);
Value ts_buffer_concat(Value* buffers, int32_t count);
int32_t ts_buffer_length(Value buf);
uint8_t ts_buffer_readUInt8(Value buf, int32_t offset);
void ts_buffer_writeUInt8(Value buf, int32_t offset, uint8_t value);
Value ts_buffer_slice(Value buf, int32_t start, int32_t end);
TSString* ts_buffer_toString_utf8(Value buf);
TSString* ts_buffer_toString_hex(Value buf);
TSString* ts_buffer_toString_base64(Value buf);
int ts_buffer_isBuffer(Value val);
double date_parse_ts(TSString* str);
int32_t date_getFullYear_ts(double ts);
int32_t date_getMonth_ts(double ts);
int32_t date_getDate_ts(double ts);
int32_t date_getDay_ts(double ts);
int32_t date_getHours_ts(double ts);
int32_t date_getMinutes_ts(double ts);
int32_t date_getSeconds_ts(double ts);
int32_t date_getMilliseconds_ts(double ts);
double date_getTime_ts(double ts);
TSString* date_toISOString_ts(double ts);
TSString* date_toDateString_ts(double ts);
TSString* date_toTimeString_ts(double ts);
TSString* date_toLocaleString_ts(double ts);
double ts_date_now(void);

/* Number parsing */
double ts_parse_int(TSString* str, int radix);
double ts_parse_float(TSString* str);

/* Utility */
int ts_is_nan(double x);
int ts_is_finite(double x);

/* JSON */
Value ts_json_parse(TSString* json);
TSString* ts_json_stringify(Value val);
TSString* ts_json_stringify_indent(Value val, int indent);
int ts_json_is_raw_json(Value val);
Value ts_json_raw_json(TSString* raw);

/* Fetch Request options */
typedef struct {
  TSString* method;
  TSHashMap* headers;
  TSString* body;
} FetchRequest;

/* Fetch Response object */
typedef struct {
  int32_t type_tag;  /* 0x46455443 = 'FETCH' */
  int32_t status;
  TSString* statusText;
  TSString* body;
  TSHashMap* headers;
  TSString* url;
  void* stream;
  int body_complete;
} FetchResponse;

#define FETCH_RESPONSE_TAG 0x46455443
#define FETCH_STREAM_TAG   0x5354524D
#define FETCH_READER_TAG   0x52445252

/* Fetch functions */
Value ts_fetch(TSString* url, Value options);
Value ts_fetch_response(Value response);
Value ts_fetch_clone(Value response);
TSString* ts_fetch_text(Value response);
Value ts_fetch_json(Value response);
double ts_fetch_response_status(Value response);
TSString* ts_fetch_response_statusText(Value response);
TSString* ts_fetch_response_url(Value response);
Value ts_fetch_response_headers(Value response);
Value ts_fetch_response_body(Value response);
Value ts_fetch_body_get_reader(Value body);
Value ts_fetch_reader_read(Value reader);

/* Headers constructor */
Value ts_headers(void);
Value ts_headers_from_object(TSHashMap* obj);
void ts_headers_set(Value headers, TSString* key, TSString* value);

/* Blob */
typedef struct {
  int32_t type_tag;  /* 0x424C4F42 = 'BLOB' */
  TSString* data;
  TSString* type;
} Blob;

#define BLOB_TAG 0x424C4F42

Value ts_blob_new(void);
Value ts_blob_from_string(TSString* data, TSString* type);
TSString* ts_blob_text(Value blob);
double ts_blob_size(Value blob);
TSString* ts_blob_type(Value blob);

/* URL */
typedef struct {
  int32_t type_tag;  /* 0x55524C20 = 'URL ' */
  TSString* href;
  TSString* protocol;
  TSString* host;
  TSString* hostname;
  TSString* port;
  TSString* pathname;
  TSString* search;
  TSString* hash;
  TSString* origin;
} Url;

#define URL_TAG 0x55524C20

Value ts_url_new(TSString* urlStr);
TSString* ts_url_href(Value url);
TSString* ts_url_protocol(Value url);
TSString* ts_url_host(Value url);
TSString* ts_url_hostname(Value url);
TSString* ts_url_port(Value url);
TSString* ts_url_pathname(Value url);
TSString* ts_url_search(Value url);
TSString* ts_url_hash(Value url);
TSString* ts_url_toString(Value url);

/* Type extraction helpers */
#define TS_EXTRACT_STRING(val) ((val).tag == TAG_STRING ? (val).as.string : ts_to_string(val))
#define TS_EXTRACT_NUMBER(val) ((val).tag == TAG_NUMBER ? (val).as.number : ts_to_number(val))
#define TS_EXTRACT_BOOLEAN(val) ((val).tag == TAG_BOOLEAN ? (val).as.boolean : ts_to_boolean(val))

/* Error handling (setjmp/longjmp) */
typedef struct {
  jmp_buf jump_buffer;
  Value error_value;
} TsErrorContext;

extern TsErrorContext _ts_current_error;

#define TS_TRY if (setjmp(_ts_current_error.jump_buffer) == 0)
#define TS_CATCH else
#define TS_THROW(val) do { _ts_current_error.error_value = val; longjmp(_ts_current_error.jump_buffer, 1); } while(0)

#endif /* TS_RUNTIME_H */
`;
  }

  private generateBuiltinFiles(builtin: BuiltinModule, usage: FeatureUsage): EmitFile[] {
    const files: EmitFile[] = [];

    // Read the pre-written C source file
    const srcPath = path.join("runtime", "src", "builtins", builtin.cSourceFile);
    const headerPath = path.join("runtime", "src", "builtins", builtin.headerFile);

    try {
      if (fs.existsSync(srcPath)) {
        let content = fs.readFileSync(srcPath, "utf-8") as string;
        // Ensure ts_features.h is included for method-level guards
        if (!content.includes("ts_features.h")) {
          content = content.replace(
            /#include\s+"node_[^"]+\.h"/,
            (m) => `${m}\n#include "ts_features.h"`,
          );
        }
        // Wrap each known builtin function with #if TS_NEED_<cName>
        content = wrapFunctionsWithFeatureGuards(content, allBuiltinCNames());
        files.push({
          path: builtin.cSourceFile,
          content,
          kind: "c",
        });
      }
      if (fs.existsSync(headerPath)) {
        // Header: only declare methods that are used (smaller + clearer)
        const original = fs.readFileSync(headerPath, "utf-8") as string;
        files.push({
          path: builtin.headerFile,
          content: this.filterBuiltinHeader(original, builtin, usage),
          kind: "h",
        });
      }
    } catch {
      // If files don't exist, generate stubs for used methods only
      files.push({
        path: builtin.cSourceFile,
        content: this.generateBuiltinStub(builtin, usage),
        kind: "c",
      });
      files.push({
        path: builtin.headerFile,
        content: this.generateBuiltinHeaderStub(builtin, usage),
        kind: "h",
      });
    }

    return files;
  }

  /** Keep only used function declarations in a node_*.h header. */
  private filterBuiltinHeader(header: string, builtin: BuiltinModule, usage: FeatureUsage): string {
    const lines = header.split(/\r?\n/);
    const out: string[] = [];
    for (const line of lines) {
      // Match declarations like: Value node_fs_readFileSync(...);
      const m = line.match(/\b(node_[a-zA-Z0-9_]+)\s*\(/);
      if (m) {
        const cName = m[1];
        // Always keep if method is needed; also keep if we fell back to whole module
        if (usage.methods.has(cName)) {
          out.push(line);
        }
        // Keep sync counterpart when async wrapper is used (async calls sync internally)
        else if (cName.endsWith("Sync")) {
          const asyncName = cName.replace(/Sync$/, "");
          if (usage.methods.has(asyncName)) {
            out.push(line);
          }
        }
        // drop unused declaration
        continue;
      }
      out.push(line);
    }
    return out.join("\n");
  }

  private generateBuiltinStub(builtin: BuiltinModule, usage?: FeatureUsage): string {
    const lines: string[] = [`#include "${builtin.headerFile}"`, `#include "ts_features.h"`];
    for (const fn of builtin.functions) {
      if (usage && !usage.methods.has(fn.cName)) continue;
      lines.push('');
      lines.push(`${fn.signature} {`);
      lines.push(`  /* Stub: ${builtin.name}.${fn.tsName} */`);
      lines.push(`  return ts_value_undefined();`);
      lines.push(`}`);
    }
    return lines.join('\n');
  }

  private generateBuiltinHeaderStub(builtin: BuiltinModule, usage?: FeatureUsage): string {
    const guard = builtin.headerFile.toUpperCase().replace(/[^A-Z0-9]/g, "_");
    const lines: string[] = [
      `#ifndef ${guard}`,
      `#define ${guard}`,
      '',
      '#include "runtime.h"',
      '',
    ];
    for (const fn of builtin.functions) {
      if (usage && !usage.methods.has(fn.cName)) continue;
      lines.push(`${fn.signature};`);
    }
    lines.push('');
    lines.push(`#endif /* ${guard} */`);
    return lines.join('\n');
  }

  /** Convert absolute path to relative path from project root, then mangle for C */
  private filePathToModuleName(filePath: string): string {
    if (!filePath) return "unknown";
    const normalized = filePath.replace(/\\/g, "/");
    const markers = ["test/fixtures/", "src/", "test/"];
    let relative = normalized;
    for (const marker of markers) {
      const idx = normalized.lastIndexOf(marker);
      if (idx >= 0) {
        relative = normalized.substring(idx);
        break;
      }
    }
    // Remove extension, replace path separators AND all non-alphanumeric chars with underscore
    return relative
      .replace(/\.(ts|tsx|js|jsx)$/, "")
      .replace(/[/\\]/g, "_")
      .replace(/[^a-zA-Z0-9_]/g, "_");
  }

  private generateMainC(
    graph: ModuleGraph,
    options: CompilerOptions,
    entryUnit?: TranspilationUnit,
    usage?: FeatureUsage,
  ): string {
    const lines: string[] = [];
    lines.push('#include "runtime.h"');
    lines.push('');

    // Include all module headers
    for (const filePath of graph.sortedOrder) {
      const baseName = this.filePathToModuleName(filePath);
      lines.push(`#include "${baseName}.h"`);
    }
    lines.push('');

    // Declare init functions
    for (const filePath of graph.sortedOrder) {
      const moduleName = this.filePathToModuleName(filePath);
      const initFn = this.mangler.mangleInit(moduleName);
      lines.push(`extern void ${initFn}(void);`);
    }
    lines.push('');

    // Find entry point (the file that's not imported by anyone)
    const imported = new Set<string>();
    for (const node of graph.nodes.values()) {
      for (const imp of node.imports) {
        // Normalize path for comparison
        imported.add(imp.replace(/\\/g, "/"));
      }
    }

    let entryFile = graph.sortedOrder[graph.sortedOrder.length - 1];
    for (const filePath of graph.sortedOrder) {
      const normalizedPath = filePath.replace(/\\/g, "/");
      if (!imported.has(normalizedPath)) {
        entryFile = filePath;
        break;
      }
    }

    const entryBase = this.filePathToModuleName(entryFile);

    // Top-level try/catch in the entry unit (outside main()) — wrap entry call
    // and emit the real catch body when present.
    const topLevelTry =
      entryUnit?.nodes.find(n => n.kind === "try_statement") as
        | { kind: string; tryBlock?: any; catchClause?: { errorVar?: string; body?: any[] } }
        | undefined;

    lines.push('/* process.argv capture (Unix/Android); no-op storage on Windows */');
    lines.push('extern void node_process_set_argv(int argc, char** argv);');
    lines.push('');
    // CommonJS __dirname / __filename — absolute path of the entry source file
    const entryAbs = path.isAbsolute(entryFile)
      ? entryFile
      : path.resolve(options.projectRoot || process.cwd(), entryFile);
    const entryDir = path.dirname(entryAbs);
    // Escape for C string literals (Windows backslashes)
    const escC = (s: string) => s.replace(/\\/g, "\\\\").replace(/"/g, '\\"');
    lines.push(`/* CommonJS module globals for entry: ${escC(entryAbs)} */`);
    lines.push(`const char* __ts_dirname = "${escC(entryDir)}";`);
    lines.push(`const char* __ts_filename = "${escC(entryAbs)}";`);
    lines.push('');
    lines.push('int main(int argc, char* argv[]) {');
    lines.push('  node_process_set_argv(argc, argv);');
    lines.push('  /* Initialize modules */');
    for (const filePath of graph.sortedOrder) {
      const moduleName = this.filePathToModuleName(filePath);
      const initFn = this.mangler.mangleInit(moduleName);
      lines.push(`  ${initFn}();`);
    }
    lines.push('');
    lines.push('  /* Run entry point */');
    if (topLevelTry) {
      const errorVar = topLevelTry.catchClause?.errorVar || "error";
      // Emit catch body from the IR when available (console.log(error.message), etc.)
      let catchBodyC = "";
      if (topLevelTry.catchClause?.body && topLevelTry.catchClause.body.length > 0) {
        const se = new StatementEmitter();
        // Register error var as Value so property access / console.log wrap correctly
        se.declareVar(errorVar, "Value");
        catchBodyC = topLevelTry.catchClause.body
          .map((s: any) => se.emit(s))
          .filter((s: string) => s && s.trim())
          .map((s: string) => "    " + s)
          .join("\n");
      }
      lines.push('  TS_TRY {');
      lines.push(`    ${entryBase}_entry();`);
      lines.push('  } TS_CATCH {');
      lines.push(`    Value ${errorVar} = _ts_current_error.error_value;`);
      if (catchBodyC) {
        lines.push(catchBodyC);
      } else {
        lines.push(`    ts_console_log(${errorVar});`);
      }
      lines.push('  }');
    } else {
      lines.push(`  ${entryBase}_entry();`);
    }
    lines.push('');
    // Drain setTimeout / setInterval queue if the program uses timers
    if (usage?.features.has("timers")) {
      lines.push('  /* Event loop: run pending timers until idle */');
      lines.push('  ts_timers_run();');
      lines.push('');
    }
    lines.push('  return 0;');
    lines.push('}');

    return lines.join('\n');
  }

  private async writeAndCompile(
    files: EmitFile[],
    options: CompilerOptions,
    usage?: FeatureUsage,
  ): Promise<void> {
    const outDir = options.outDir || "./out";
    const outputFile = options.output || "output";
    const projectRoot = options.projectRoot || process.cwd();

    // Handle absolute output path - extract directory and filename
    let outputDir = outDir;
    let outputName = outputFile;
    if (path.isAbsolute(outputFile)) {
      outputDir = path.dirname(outputFile);
      outputName = path.basename(outputFile);
    }

    // Write all C files to outDir
    if (!fs.existsSync(outDir)) {
      fs.mkdirSync(outDir, { recursive: true });
    }

    for (const file of files) {
      const filePath = path.join(outDir, file.path);
      const dir = path.dirname(filePath);
      if (!fs.existsSync(dir)) {
        fs.mkdirSync(dir, { recursive: true });
      }
      fs.writeFileSync(filePath, file.content, "utf-8");
    }

    // Ensure output directory exists
    if (!fs.existsSync(outputDir)) {
      fs.mkdirSync(outputDir, { recursive: true });
    }

    // Core runtime always linked; optional units only when feature analysis needs them.
    // Feature-heavy code in runtime.c / builtins.c is also gated via TS_NEED_*.
    const runtimeSrcFiles: string[] = [];
    const runtimeDir = path.join(projectRoot, "runtime/src");
    // Scan ALL emitted C (user + node_* builtins) for runtime deps.
    // Feature analysis can miss internal calls inside node_fs.c etc.
    const allC = files.filter(f => f.kind === "c").map(f => f.content).join("\n");
    const needArray =
      (usage?.features.has("array") ?? false) ||
      /\bts_array_/.test(allC) ||
      /\bts_string_split\b/.test(allC);
    const needHashmap =
      (usage?.features.has("hashmap") ?? false) ||
      /\bts_hashmap_/.test(allC) ||
      /\bts_object_to_string\b/.test(allC);
    const needClosure =
      (usage?.features.has("closure") ?? false) ||
      /\bts_closure_/.test(allC);
    const needBuiltins = !!(
      usage?.features.has("math") ||
      usage?.features.has("date") ||
      usage?.features.has("parse") ||
      usage?.features.has("console_time") ||
      usage?.features.has("timers") ||
      usage?.features.has("dialogs") ||
      /\bts_math_|\bts_date_now\b|\bts_parse_|\bts_set_timeout\b|\bts_set_interval\b|\bts_alert\b|\bts_confirm\b|\bts_prompt\b/.test(allC)
    );
    // Pure console.log("literal") → puts() programs need zero runtime objects.
    const needsStringOps = /\bts_string_/.test(allC);
    const needsTsRuntime =
      /\bts_/.test(allC) ||
      (usage?.features.size ?? 0) > 0 ||
      needArray || needHashmap || needClosure || needBuiltins ||
      needsStringOps ||
      [...(usage?.methods ?? [])].some(m => m !== "node_process_set_argv");

    const coreRuntime: string[] = [];
    if (needsTsRuntime) {
      // string_ops holds core strings + indexOf/substring/replace/… (GC drops unused)
      coreRuntime.push("runtime.c", "string_ops.c");
      if (needBuiltins) coreRuntime.push("builtins.c");
      if (needArray) coreRuntime.push("array_ops.c");
      if (needHashmap) coreRuntime.push("hashmap.c");
      if (needClosure) coreRuntime.push("closure.c");
    }
    for (const entry of coreRuntime) {
      const p = path.join(runtimeDir, entry);
      if (fs.existsSync(p)) runtimeSrcFiles.push(p);
    }

    // Build clang command
    const cFiles = files
      .filter(f => f.kind === "c")
      .map(f => path.join(outDir, f.path));

    const includeDir = path.resolve(outDir);
    const runtimeInclude = path.join(projectRoot, "runtime/include");
    const builtinInclude = path.join(projectRoot, "runtime/src/builtins");

    const isWindows = (options.target || process.platform) === "win32" || options.target === "windows";
    // Android/Termux reports process.platform === "android" (or linux) — not win32
    const isUnix = !isWindows;

    const needFetch = usage?.features.has("fetch") ?? false;
    const needNet = !!(usage?.modules.has("http") || usage?.modules.has("net") || needFetch);
    // shell32 only for process.argv (CommandLineToArgvW) on Windows
    const needShell32 = !!(usage?.methods.has("node_process_argv"));
    const needOs = usage?.modules.has("os") ?? false;
    const needMathLib = usage?.features.has("math") ?? false;

    // Size-oriented flags: optimize for size; COMDAT sections + GC drop unused funcs.
    // -Oz is more aggressive than -Os; LTO (when lld available) enables cross-TU DCE.
    // NOTE: do NOT use -fno-asynchronous-unwind-tables on Windows MSVC ABI —
    // it breaks setjmp/longjmp (TS_TRY/TS_THROW) and crashes on throw.
    const sizeFlags = [
      "-Oz",
      "-ffunction-sections",
      "-fdata-sections",
      "-fmerge-all-constants",
    ];
    // MSVC/lld-link: /OPT:REF (+ ICF); GNU: --gc-sections
    const gcFlags = isWindows
      ? ["-Wl,/OPT:REF", "-Wl,/OPT:ICF"]
      : ["-Wl,--gc-sections", "-Wl,--as-needed"];

    const outExe = path.join(outputDir, outputName + (isWindows ? ".exe" : ""));
    const { execSync, execFileSync } = await import("child_process");

    // Prefer lld when available (better /OPT:REF + ICF on Windows)
    let useLld = false;
    if (CompilerDriver._lldCached !== undefined) {
      useLld = CompilerDriver._lldCached;
    } else {
      try {
        execFileSync("lld-link", ["--version"], { stdio: "pipe" });
        useLld = true;
      } catch {
        try {
          execFileSync("ld.lld", ["--version"], { stdio: "pipe" });
          useLld = true;
        } catch {
          useLld = false;
        }
      }
      CompilerDriver._lldCached = useLld;
    }
    // LTO needs lld on this toolchain; enables cross-file dead-code elimination.
    const lldFlags = useLld ? ["-fuse-ld=lld", "-flto"] : [];

    const cmd = [
      "clang",
      ...sizeFlags,
      ...lldFlags,
      ...cFiles,
      ...runtimeSrcFiles,
      `-I${includeDir}`,
      `-I${runtimeInclude}`,
      `-I${builtinInclude}`,
      `-o${outExe}`,
      // Math/pthread only when needed on Unix
      ...(isUnix && needMathLib ? ["-lm"] : []),
      ...(isUnix ? ["-lpthread"] : []),
      ...(isUnix && needFetch ? ["-lcurl"] : []),
      ...(isWindows && needNet ? ["-lws2_32"] : []),
      ...(isWindows && needShell32 ? ["-lshell32"] : []),
      ...(isWindows && needOs ? ["-ladvapi32"] : []),
      ...(isWindows && needFetch ? ["-lwinhttp"] : []),
      ...gcFlags,
      "-Wno-implicit-function-declaration",
      "-Wno-deprecated-non-prototype",
      ...(options.clangArgs || []),
    ].join(" ");

    try {
      execSync(cmd, { stdio: "pipe", cwd: process.cwd() });
    } catch (e: any) {
      throw new Error(`clang failed: ${e.stderr?.toString() || e.message}`);
    }

    // Strip unneeded symbols when a strip tool is available
    for (const tool of ["llvm-strip", "strip"]) {
      try {
        execFileSync(tool, ["--strip-unneeded", outExe], { stdio: "pipe" });
        break;
      } catch {
        // try next
      }
    }
  }

  private static _lldCached: boolean | undefined;
}
