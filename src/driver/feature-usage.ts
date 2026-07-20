import type { CNode, TranspilationUnit } from "../codegen/c-emitter.js";
import { BUILTIN_MODULES } from "../builtins/registry.js";

/** Node built-in module names we can tree-shake. */
export const BUILTIN_MODULE_NAMES = [
  "fs", "path", "process", "os", "http", "net", "child_process", "events", "readline", "assert", "crypto",
] as const;

export type BuiltinModuleName = (typeof BUILTIN_MODULE_NAMES)[number];

/**
 * Heavy runtime features living in runtime.c / builtins.c.
 * Only compiled when the corresponding TS source uses them.
 */
export const RUNTIME_FEATURES = [
  "fetch",
  "json",
  "date",
  "blob",
  "url",
  "buffer",
  "console_time",
  "math",
  "parse",
  "gc",
  "timers",
  "dialogs",
  /** Link array_ops.c (arrays / string.split). */
  "array",
  /** Link hashmap.c (objects / Map-like). */
  "hashmap",
  /** Link closure.c. */
  "closure",
  /** Extra string helpers beyond new/concat/free (split/repeat/…). */
  "string_extra",
] as const;

export type RuntimeFeature = (typeof RUNTIME_FEATURES)[number];

export interface FeatureUsage {
  /** Built-in modules referenced (import or global). */
  modules: Set<string>;
  /**
   * C function names that must be compiled, e.g.
   *   node_fs_readFileSync, node_child_process_spawn, node_process_set_argv
   */
  methods: Set<string>;
  /** Runtime feature flags (fetch/json/date/…). */
  features: Set<string>;
}

const GLOBAL_CTOR_FEATURES: Record<string, RuntimeFeature> = {
  Date: "date",
  JSON: "json",
  Blob: "blob",
  URL: "url",
  Buffer: "buffer",
  fetch: "fetch",
};

/** Map process property/method names → C symbols. */
const PROCESS_PROPS: Record<string, string> = {
  env: "node_process_env",
  argv: "node_process_argv",
  cwd: "node_process_cwd",
  chdir: "node_process_chdir",
  exit: "node_process_exit",
  pid: "node_process_pid",
  stdin: "node_process_stdin",
  stdout: "node_process_stdout",
  stderr: "node_process_stderr",
  platform: "node_process_platform",
  version: "node_process_version",
  versions: "node_process_versions",
  arch: "node_process_arch",
  title: "node_process_title",
  hrtime: "node_process_hrtime",
  memoryUsage: "node_process_memoryUsage",
  uptime: "node_process_uptime",
  nextTick: "node_process_nextTick",
};

const PROCESS_STREAM_METHODS: Record<string, string> = {
  write: "write",
  on: "on",
  cursorTo: "cursorTo",
  moveCursor: "moveCursor",
  clearScreenDown: "clearScreenDown",
  clearLine: "clearLine",
};

function walk(node: CNode | null | undefined, visit: (n: CNode) => void): void {
  if (!node || typeof node !== "object") return;
  visit(node);
  for (const key of Object.keys(node)) {
    if (key === "kind") continue;
    const v = node[key];
    if (Array.isArray(v)) {
      for (const item of v) {
        if (item && typeof item === "object") {
          if (item.kind) {
            walk(item as CNode, visit);
          } else {
            // Container objects (e.g. template spans) without 'kind'
            // may hold CNode children — recurse into their properties.
            walkContainer(item, visit);
          }
        }
      }
    } else if (v && typeof v === "object" && (v as CNode).kind) {
      walk(v as CNode, visit);
    }
  }
}

function walkContainer(obj: Record<string, unknown>, visit: (n: CNode) => void): void {
  for (const key of Object.keys(obj)) {
    const v = obj[key];
    if (!v || typeof v !== "object") continue;
    if (Array.isArray(v)) {
      for (const item of v) {
        if (item && typeof item === "object") {
          if (item.kind) walk(item as CNode, visit);
          else walkContainer(item, visit);
        }
      }
    } else if ((v as CNode).kind) {
      walk(v as CNode, visit);
    }
  }
}

function addMethod(usage: FeatureUsage, cName: string): void {
  usage.methods.add(cName);
}

function resolveBuiltinMethod(module: string, prop: string): string | null {
  const mod = BUILTIN_MODULES.get(module);
  if (!mod) return null;
  const fn = mod.functions.find(f => f.tsName === prop);
  return fn ? fn.cName : `node_${module}_${prop}`;
}

/**
 * Analyze transpilation units + import-based module set for tree-shaking.
 */
/** Scan free-form C snippets / emitted text for runtime symbol use. */
function scanCText(usage: FeatureUsage, text: string): void {
  if (!text || typeof text !== "string") return;
  if (/\bts_array_/.test(text) || /\bts_string_split\b/.test(text)) {
    usage.features.add("array");
  }
  if (/\bts_hashmap_/.test(text) || /\bts_object_to_string\b/.test(text)) {
    usage.features.add("hashmap");
  }
  if (/\bts_closure_/.test(text)) usage.features.add("closure");
  if (/\bts_buffer_/.test(text) || /\bBUFFER_TAG\b/.test(text)) {
    usage.features.add("buffer");
  }
  if (/\bts_json_/.test(text)) usage.features.add("json");
  if (/\bts_fetch_|\bts_headers\b/.test(text)) usage.features.add("fetch");
  if (/\bts_blob_/.test(text)) usage.features.add("blob");
  if (/\bts_url_/.test(text)) usage.features.add("url");
  if (/\bts_math_|\bdate_/.test(text) || /\bts_date_now\b/.test(text)) {
    if (/\bts_math_/.test(text)) usage.features.add("math");
    if (/\bdate_/.test(text) || /\bts_date_now\b/.test(text)) usage.features.add("date");
  }
  if (/\bts_string_split\b|\bts_string_repeat\b/.test(text)) {
    usage.features.add("string_extra");
    usage.features.add("array");
  }
  if (/\bts_gc_/.test(text)) usage.features.add("gc");
  if (/\bts_set_timeout\b|\bts_set_interval\b|\bts_timers_/.test(text)) {
    usage.features.add("timers");
  }
}

function walkScanText(node: CNode | null | undefined, usage: FeatureUsage): void {
  if (!node || typeof node !== "object") return;
  for (const key of Object.keys(node)) {
    const v = (node as Record<string, unknown>)[key];
    if (typeof v === "string") scanCText(usage, v);
    else if (Array.isArray(v)) {
      for (const item of v) {
        if (typeof item === "string") scanCText(usage, item);
        else if (item && typeof item === "object") walkScanText(item as CNode, usage);
      }
    } else if (v && typeof v === "object") {
      walkScanText(v as CNode, usage);
    }
  }
}

export function analyzeFeatureUsage(
  units: TranspilationUnit[],
  importedBuiltins: Set<string>,
): FeatureUsage {
  const usage: FeatureUsage = {
    modules: new Set(importedBuiltins),
    methods: new Set(),
    features: new Set(),
  };

  // Always need argv capture in main (tiny).
  usage.modules.add("process");
  addMethod(usage, "node_process_set_argv");

  for (const unit of units) {
    for (const root of unit.nodes) {
      walkScanText(root, usage);
      walk(root, (node) => {
        // Global constructors / free functions
        if (node.kind === "identifier" && node.name) {
          const feat = GLOBAL_CTOR_FEATURES[node.name];
          if (feat) usage.features.add(feat);
          if (node.name === "fetch") {
            usage.features.add("fetch");
          }
        }
        // Also check string_literal nodes (constructors like Buffer, Date, etc. are converted to string_literal)
        if (node.kind === "string_literal" && node.value) {
          const feat = GLOBAL_CTOR_FEATURES[node.value];
          if (feat) usage.features.add(feat);
        }

        // call_expression patterns
        if (node.kind === "call_expression" && node.callee) {
          const cal = node.callee as CNode;

          // fetch(...)
          if (cal.kind === "identifier" && cal.name === "fetch") {
            usage.features.add("fetch");
          }

          // JSON.parse / JSON.stringify
          if (cal.kind === "property_access" &&
              ((cal.object?.kind === "identifier" && cal.object.name === "JSON") ||
               (cal.object?.kind === "string_literal" && cal.object.value === "JSON"))) {
            usage.features.add("json");
          }

          // Date.now / new Date handled via property / new_expression
          if (cal.kind === "property_access" &&
              ((cal.object?.kind === "identifier" && cal.object.name === "Date") ||
               (cal.object?.kind === "string_literal" && cal.object.value === "Date"))) {
            usage.features.add("date");
          }

          // Buffer.from / Buffer.alloc / etc.
          if (cal.kind === "property_access" &&
              ((cal.object?.kind === "identifier" && cal.object.name === "Buffer") ||
               (cal.object?.kind === "string_literal" && cal.object.value === "Buffer"))) {
            usage.features.add("buffer");
          }

          // Math.*
          if (cal.kind === "property_access" &&
              cal.object?.kind === "identifier" &&
              cal.object.name === "Math") {
            usage.features.add("math");
            if (cal.property) {
              addMethod(usage, `ts_math_${cal.property}`);
            }
          }

          // console.time / timeEnd
          if (cal.kind === "property_access" &&
              cal.object?.kind === "identifier" &&
              cal.object.name === "console" &&
              (cal.property === "time" || cal.property === "timeEnd")) {
            usage.features.add("console_time");
          }

          // parseInt / parseFloat
          if (cal.kind === "identifier" &&
              (cal.name === "parseInt" || cal.name === "parseFloat")) {
            usage.features.add("parse");
          }

          // setTimeout / setInterval / clearTimeout / clearInterval
          if (cal.kind === "identifier" &&
              (cal.name === "setTimeout" || cal.name === "setInterval" ||
               cal.name === "clearTimeout" || cal.name === "clearInterval")) {
            usage.features.add("timers");
          }

          // alert / confirm / prompt
          if (cal.kind === "identifier" &&
              (cal.name === "alert" || cal.name === "confirm" || cal.name === "prompt")) {
            usage.features.add("dialogs");
          }

          // module.method(...)  e.g. fs.readFileSync, child_process.spawn
          if (cal.kind === "property_access" &&
              cal.object?.kind === "identifier" &&
              BUILTIN_MODULE_NAMES.includes(cal.object.name as BuiltinModuleName)) {
            const mod = cal.object.name as string;
            usage.modules.add(mod);
            const cName = resolveBuiltinMethod(mod, cal.property);
            if (cName) addMethod(usage, cName);
          }

          // process.stdout.write / process.stdin.on
          if (cal.kind === "property_access" &&
              cal.object?.kind === "property_access" &&
              cal.object.object?.kind === "identifier" &&
              cal.object.object.name === "process") {
            usage.modules.add("process");
            const stream = cal.object.property as string;
            const method = cal.property as string;
            if (stream === "stdin" || stream === "stdout" || stream === "stderr") {
              addMethod(usage, `node_process_${stream}`);
              const m = PROCESS_STREAM_METHODS[method] || method;
              addMethod(usage, `node_process_${stream}_${m}`);
            }
          }

          // child.stdout.on / child.on / child.send → helper C APIs
          // Exclude process.stdin/stdout/stderr (those are node_process_* APIs).
          if (cal.kind === "property_access") {
            if (cal.property === "on" &&
                cal.object?.kind === "property_access" &&
                (cal.object.property === "stdout" ||
                 cal.object.property === "stderr" ||
                 cal.object.property === "stdin") &&
                !(cal.object.object?.kind === "identifier" &&
                  cal.object.object.name === "process")) {
              usage.modules.add("child_process");
              addMethod(usage, "node_child_process_stream_on");
            }
            if (cal.property === "on" && cal.object?.kind === "identifier") {
              // May be child_process child or http server — mark both helpers lightly
              // Only pull child_process_on if child_process module is already in use
              // or variable name looks like a child.
              const name = cal.object.name as string;
              if (/child|spawn|fork|dir|proc|cp/i.test(name) || usage.modules.has("child_process")) {
                usage.modules.add("child_process");
                addMethod(usage, "node_child_process_on");
              }
            }
            if (cal.property === "send" && cal.object?.kind === "identifier") {
              const name = cal.object.name as string;
              if (/fork|child/i.test(name) || usage.modules.has("child_process")) {
                usage.modules.add("child_process");
                addMethod(usage, "node_child_process_send");
              }
            }
            // http server.listen
            if (cal.property === "listen") {
              usage.modules.add("http");
              addMethod(usage, "node_http_server_listen");
            }
          }
        }

        // new Date / new Blob / new URL / new EventEmitter / new events.EventEmitter
        if (node.kind === "new_expression") {
          const className = (node as any).className as string | undefined;
          const name = node.callee?.kind === "identifier"
            ? node.callee.name
            : node.callee?.kind === "string_literal"
            ? node.callee.value
            : className;
          if (name && GLOBAL_CTOR_FEATURES[name]) {
            usage.features.add(GLOBAL_CTOR_FEATURES[name]);
          }
          if (name === "EventEmitter" ||
              (typeof className === "string" && className.includes("EventEmitter"))) {
            usage.modules.add("events");
            addMethod(usage, "node_events_EventEmitter");
          }
        }

        // ee.on / ee.emit / … instance methods (when events module in use or name matches)
        if (node.kind === "call_expression" &&
            node.callee?.kind === "property_access" &&
            node.callee.object?.kind === "identifier") {
          const m = node.callee.property as string;
          const obj = node.callee.object.name as string;
          const eeMethods = [
            "on", "addListener", "once", "off", "removeListener",
            "prependListener", "prependOnceListener", "emit",
            "removeAllListeners", "listenerCount", "listeners", "rawListeners",
            "eventNames", "setMaxListeners", "getMaxListeners",
          ];
          if (eeMethods.includes(m) && /ee|emitter|event/i.test(obj) && !/^(rl|readline)$/i.test(obj)) {
            usage.modules.add("events");
            addMethod(usage, "node_events_EventEmitter");
            addMethod(usage, `node_events_${m}`);
          }
          // readline.Interface methods
          const rlMethods = [
            "question", "close", "on", "prompt", "setPrompt", "getPrompt",
            "write", "pause", "resume",
          ];
          if (rlMethods.includes(m) && /^(rl|readline|interface)$/i.test(obj)) {
            usage.modules.add("readline");
            addMethod(usage, "node_readline_createInterface");
            addMethod(usage, `node_readline_${m}`);
          }
          // crypto hash methods: hash.update(data), hash.digest(encoding)
          if ((m === "update" || m === "digest") &&
              (/hash|hmac/i.test(obj) || /^h\d*$/.test(obj))) {
            usage.modules.add("crypto");
            addMethod(usage, "node_crypto_hashUpdate");
            addMethod(usage, "node_crypto_hashDigest");
          }
        }

        // process.argv / process.env property access (zero-arg getters)
        if (node.kind === "property_access" &&
            node.object?.kind === "identifier" &&
            node.object.name === "process") {
          usage.modules.add("process");
          const prop = node.property as string;
          const cName = PROCESS_PROPS[prop];
          if (cName) addMethod(usage, cName);
        }

        // os.EOL etc.
        if (node.kind === "property_access" &&
            node.object?.kind === "identifier" &&
            node.object.name === "os") {
          usage.modules.add("os");
          const cName = resolveBuiltinMethod("os", node.property);
          if (cName) addMethod(usage, cName);
        }

        // events.EventEmitter / events.defaultMaxListeners as property access
        if (node.kind === "property_access" &&
            node.object?.kind === "identifier" &&
            node.object.name === "events") {
          usage.modules.add("events");
          const prop = node.property as string;
          const cName = resolveBuiltinMethod("events", prop);
          if (cName) addMethod(usage, cName);
        }

        // date_* helpers may appear after emission; also mark date feature on date_ methods in identifiers
        if (node.kind === "identifier" && typeof node.name === "string") {
          if (node.name.startsWith("date_") || node.name === "ts_date_now") {
            usage.features.add("date");
          }
          if (node.name.startsWith("ts_json_")) usage.features.add("json");
          if (node.name.startsWith("ts_fetch") || node.name.startsWith("ts_headers")) {
            usage.features.add("fetch");
          }
          if (node.name.startsWith("ts_blob_")) usage.features.add("blob");
          if (node.name.startsWith("ts_url_")) usage.features.add("url");
          // Buffer only when Buffer APIs are actually used (not every ts_to_string call)
          if (node.name.startsWith("ts_buffer_")) usage.features.add("buffer");
          if (node.name.startsWith("ts_math_")) usage.features.add("math");
          if (node.name.startsWith("ts_array_")) usage.features.add("array");
          if (node.name.startsWith("ts_hashmap_")) usage.features.add("hashmap");
          if (node.name.startsWith("ts_closure_")) usage.features.add("closure");
          if (node.name === "ts_string_split" || node.name === "ts_string_repeat") {
            usage.features.add("string_extra");
            usage.features.add("array");
          }
        }

        // String/Array method calls that map to extra runtime helpers
        if (node.kind === "call_expression" &&
            node.callee?.kind === "property_access") {
          const prop = node.callee.property as string;
          const arrayMethods = [
            "push", "pop", "indexOf", "splice", "filter", "map", "join",
            "some", "every", "find", "reduce", "forEach", "slice", "includes",
            "concat",
          ];
          if (arrayMethods.includes(prop)) {
            usage.features.add("array");
          }
          if (prop === "split" || prop === "repeat") {
            usage.features.add("string_extra");
            usage.features.add("array");
          }
          const mapMethods = ["get", "set", "has", "delete", "clear", "forEach"];
          // Only when object looks like a Map/object ops — also detect via ts_hashmap_* ids
          void mapMethods;
        }

        // Array / object literals in IR
        if (node.kind === "array_literal" || node.kind === "array_expression") {
          usage.features.add("array");
        }
        if (node.kind === "object_literal" || node.kind === "object_expression") {
          usage.features.add("hashmap");
        }
      });
    }
  }

  // If fs module is used, enable buffer feature (fs.readFile returns Buffer)
  if (usage.modules.has("fs")) {
    usage.features.add("buffer");
  }

  // Modules that construct objects/arrays pull in hashmap/array runtime units.
  // fs/path read options via ts_hashmap_get; nearly every node_* unit needs both.
  if (usage.modules.has("process") &&
      [...usage.methods].some(m =>
        m === "node_process_argv" || m === "node_process_env" ||
        m === "node_process_versions" || m.includes("memoryUsage"))) {
    usage.features.add("array");
    usage.features.add("hashmap");
  }
  const modulesNeedingHashArray = [
    "fs", "path", "http", "net", "events", "child_process",
    "crypto", "assert", "readline", "os",
  ];
  for (const m of modulesNeedingHashArray) {
    if (usage.modules.has(m)) {
      usage.features.add("hashmap");
      usage.features.add("array");
      break;
    }
  }
  if (usage.features.has("json") || usage.features.has("fetch") ||
      usage.features.has("url") || usage.features.has("blob")) {
    usage.features.add("hashmap");
    usage.features.add("array");
  }
  if (usage.features.has("string_extra")) {
    usage.features.add("array");
  }

  // If a module is used but no methods were recorded, keep ALL of its registry functions
  // (safe fallback for dynamic/indirect use).
  for (const modName of usage.modules) {
    const mod = BUILTIN_MODULES.get(modName);
    if (!mod) continue;
    const hasAny = mod.functions.some(f => usage.methods.has(f.cName));
    // process always has set_argv; if nothing else, only keep set_argv (+ common getters if any)
    if (!hasAny && modName !== "process") {
      for (const f of mod.functions) addMethod(usage, f.cName);
    } else if (!hasAny && modName === "process") {
      addMethod(usage, "node_process_set_argv");
    }
  }

  // child_process helpers: if any of spawn/exec/fork used, keep on/stream_on/send when referenced;
  // already handled. Ensure internal helpers for used entry points:
  if (usage.methods.has("node_child_process_spawn") ||
      usage.methods.has("node_child_process_fork") ||
      usage.methods.has("node_child_process_exec") ||
      usage.methods.has("node_child_process_execFile")) {
    // stream_on / on / send only if already detected; OK
  }

  // fetch.json() needs the JSON parser.
  if (usage.features.has("fetch")) {
    usage.features.add("json");
  }

  // events: if any events method is needed, always keep the constructor.
  if (usage.modules.has("events")) {
    addMethod(usage, "node_events_EventEmitter");
  }

  // readline: if module used, always keep createInterface
  if (usage.modules.has("readline")) {
    addMethod(usage, "node_readline_createInterface");
  }

  // Keep sync counterparts when async wrappers are used (async calls sync internally)
  // e.g., node_fs_readFile → node_fs_readFileSync
  for (const m of [...usage.methods]) {
    if (m.startsWith("node_") && !m.endsWith("Sync")) {
      const syncName = m + "Sync";
      for (const mod of BUILTIN_MODULES.values()) {
        if (mod.functions.some(f => f.cName === syncName)) {
          addMethod(usage, syncName);
          break;
        }
      }
    }
  }

  return usage;
}

/** Emit ts_features.h content from usage. */
export function generateFeaturesHeader(usage: FeatureUsage): string {
  const lines: string[] = [
    "#ifndef TS_FEATURES_H",
    "#define TS_FEATURES_H",
    "",
    "/* Auto-generated by mini-tsc — do not edit. */",
    "/* Only features/methods referenced by the compiled program are enabled. */",
    "",
  ];

  for (const mod of [...usage.modules].sort()) {
    lines.push(`#define TS_NEED_MODULE_${mod.toUpperCase()} 1`);
  }
  lines.push("");

  for (const feat of [...usage.features].sort()) {
    lines.push(`#define TS_NEED_${feat.toUpperCase()} 1`);
  }
  lines.push("");

  for (const m of [...usage.methods].sort()) {
    // Sanitize to valid macro: node_fs_readFileSync → TS_NEED_node_fs_readFileSync
    lines.push(`#define TS_NEED_${m} 1`);
  }

  lines.push("");
  lines.push("#endif /* TS_FEATURES_H */");
  return lines.join("\n");
}

/**
 * Wrap each top-level function definition whose name is in `allKnownMethods`
 * with `#if defined(TS_NEED_<name>)` … `#endif`, so unused methods are not compiled.
 *
 * Heuristic: a line matching `^ReturnType name(` at column 0 starts a function;
 * the matching closing `}` at column 0 ends it.
 */
export function wrapFunctionsWithFeatureGuards(
  source: string,
  knownCNames: string[],
): string {
  if (knownCNames.length === 0) return source;

  const nameSet = new Set(knownCNames);
  // Match: optional storage, return type tokens, then name(
  // e.g. "Value node_fs_readFileSync(Value path, Value options) {"
  //      "void node_process_set_argv(int argc, char** argv) {"
  //      "int node_fs_existsSync(Value path) {"
  const fnStart = /^((?:static\s+)?(?:const\s+)?[\w\s\*]+?)\b([a-zA-Z_][a-zA-Z0-9_]*)\s*\([^;]*\)\s*\{?\s*$/;

  const lines = source.split(/\r?\n/);
  const out: string[] = [];
  let i = 0;
  let braceDepth = 0;
  let inGuardedFn = false;
  let pendingGuard: string | null = null;

  while (i < lines.length) {
    const line = lines[i];

    if (!inGuardedFn && braceDepth === 0) {
      const m = line.match(fnStart);
      if (m) {
        const name = m[2];
        if (nameSet.has(name) && !line.includes(";")) {
          // multi-line signature: keep reading until we see '{' or ';'
          let sig = line;
          let j = i;
          let opened = line.includes("{");
          if (!opened) {
            while (j + 1 < lines.length && !opened) {
              j++;
              sig += "\n" + lines[j];
              if (lines[j].includes("{")) opened = true;
              if (lines[j].trim().endsWith(";")) break;
            }
          }
          if (opened && !sig.trim().endsWith(";")) {
            out.push(`#if defined(TS_NEED_${name})`);
            // emit signature lines
            for (let k = i; k <= j; k++) out.push(lines[k]);
            // count braces on emitted chunk
            const chunk = lines.slice(i, j + 1).join("\n");
            braceDepth = 0;
            for (const ch of chunk) {
              if (ch === "{") braceDepth++;
              else if (ch === "}") braceDepth--;
            }
            inGuardedFn = true;
            pendingGuard = name;
            i = j + 1;
            if (braceDepth <= 0) {
              out.push(`#endif /* TS_NEED_${name} */`);
              inGuardedFn = false;
              pendingGuard = null;
              braceDepth = 0;
            }
            continue;
          }
        }
      }
    }

    if (inGuardedFn) {
      out.push(line);
      for (const ch of line) {
        if (ch === "{") braceDepth++;
        else if (ch === "}") braceDepth--;
      }
      if (braceDepth <= 0) {
        out.push(`#endif /* TS_NEED_${pendingGuard} */`);
        inGuardedFn = false;
        pendingGuard = null;
        braceDepth = 0;
      }
      i++;
      continue;
    }

    out.push(line);
    i++;
  }

  return out.join("\n");
}

/** All known C function names across builtin modules (for wrapping). */
export function allBuiltinCNames(): string[] {
  const names: string[] = [];
  for (const mod of BUILTIN_MODULES.values()) {
    for (const f of mod.functions) names.push(f.cName);
  }
  // process helpers not all in registry
  names.push(
    "node_process_set_argv",
    "node_process_stdin_on",
    "node_process_stdout_write",
    "node_process_stderr_write",
    "node_process_stdout_cursorTo",
    "node_process_stdout_moveCursor",
    "node_process_stdout_clearScreenDown",
    "node_process_stdout_clearLine",
    "node_process_stderr_cursorTo",
    "node_process_stderr_moveCursor",
    "node_process_stderr_clearScreenDown",
    "node_process_stderr_clearLine",
    "node_http_server_listen",
    "node_http_get",
    "node_http_request",
  );
  return [...new Set(names)];
}
