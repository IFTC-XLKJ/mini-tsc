import * as path from "path";
import type { ExportEntry } from "../modules/module-resolver.js";
import { HeaderEmitter } from "./header-emitter.js";
import { StatementEmitter } from "./statement-emitter.js";
import { TypeEmitter } from "./type-emitter.js";
import { SymbolMangler } from "../modules/symbol-mangler.js";

export interface CNode {
  kind: string;
  [key: string]: any;
}

export interface ImportedSymbolInfo {
  mangledName: string;
  returnType: string;
  paramTypes: string[];
  isConstant?: boolean; // true for exported const/let variables
}

export interface TranspilationUnit {
  filePath: string;
  nodes: CNode[];
  typeDefinitions: CNode[];
  imports: { filePath: string; symbols: string[] }[];
  importedSymbols: Map<string, ImportedSymbolInfo>; // original name → symbol info
  namespaceModulePaths?: Map<string, string>; // namespace name → module file path (for `import * as X`)
}

export interface ModuleInfo {
  filePath: string;
  exports: ExportEntry[];
  imports: { filePath: string; symbols: string[] }[];
}

export interface EmitFile {
  path: string;
  content: string;
  kind: "c" | "h";
}

/** Convert absolute path to relative path from project root, then mangle for C */
function filePathToModuleName(filePath: string): string {
  // Normalize path separators
  const normalized = filePath.replace(/\\/g, "/");
  // Try to find project root markers
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

export class CEmitter {
  private headerEmitter = new HeaderEmitter();
  private statementEmitter = new StatementEmitter();
  private typeEmitter = new TypeEmitter();
  private mangler = new SymbolMangler();
  /** When set, only these node_*.h headers are #included in generated .c files. */
  private usedBuiltinModules: Set<string> | null = null;

  setUsedBuiltinModules(mods: Set<string> | null): void {
    this.usedBuiltinModules = mods;
  }

  emitUnit(unit: TranspilationUnit, moduleInfo: ModuleInfo): EmitFile[] {
    const files: EmitFile[] = [];
    const baseName = filePathToModuleName(unit.filePath);

    // Generate .h file
    const headerContent = this.headerEmitter.emitHeader(
      baseName,
      moduleInfo.exports,
      unit.typeDefinitions,
      unit.nodes.filter(n => n.kind === "function_decl"),
    );
    files.push({
      path: `${baseName}.h`,
      content: headerContent,
      kind: "h",
    });

    // Generate .c file
    const cContent = this.emitCFile(unit, moduleInfo);
    files.push({
      path: `${baseName}.c`,
      content: cContent,
      kind: "c",
    });

    return files;
  }

  private emitCFile(unit: TranspilationUnit, moduleInfo: ModuleInfo): string {
    const lines: string[] = [];
    const baseName = filePathToModuleName(unit.filePath);

    // Pass imported symbols to the statement emitter
    if (unit.importedSymbols && unit.importedSymbols.size > 0) {
      this.statementEmitter.setImportedSymbols(unit.importedSymbols);
    }

    // Pass namespace module paths to the statement emitter
    if (unit.namespaceModulePaths && unit.namespaceModulePaths.size > 0) {
      this.statementEmitter.setNamespaceModulePaths(unit.namespaceModulePaths);
    }

    // Include own header
    lines.push(`#include "${baseName}.h"`);
    lines.push('');

    // Include imported module headers
    for (const imp of unit.imports) {
      const importBase = filePathToModuleName(imp.filePath);
      lines.push(`#include "${importBase}.h"`);
    }

    // Include headers for modules that define types used in the code
    // (e.g., if code uses Command*, include src_cli_commander_command.h)
    const typeHeaders = this.collectTypeHeaders(unit.nodes);
    for (const hdr of typeHeaders) {
      if (!lines.includes(`#include "${hdr}"`)) {
        lines.push(`#include "${hdr}"`);
      }
    }

    // Include only used Node built-in module headers (tree-shaken)
    const builtinModules = this.usedBuiltinModules
      ? [...this.usedBuiltinModules]
      : ["fs", "path", "process", "os", "http", "net", "child_process", "events", "readline", "assert", "crypto", "worker_threads", "chalk"];
    for (const builtin of builtinModules) {
      lines.push(`#include "node_${builtin}.h"`);
    }
    lines.push('');

    // Module init flag
    const initFn = this.mangler.mangleInit(filePathToModuleName(unit.filePath));
    const flagName = this.mangler.mangleInitFlag(filePathToModuleName(unit.filePath));
    lines.push(`static int ${flagName} = 0;`);
    lines.push('');

    // Type definitions (structs, enums, unions) - skip if already in header
    // The header includes runtime.h which has type definitions, so we only emit
    // type definitions that are NOT already in the header
    for (const typeDef of unit.typeDefinitions) {
      // Skip struct definitions that are in the header
      if (typeDef.kind === "struct_decl") {
        // Structs are defined in the header, skip in .c
        continue;
      }
      lines.push(this.typeEmitter.emitTypeDefinition(typeDef));
      lines.push('');
    }

    // Build name map: original name → mangled name for exported symbols
    // Built early so forward declarations and function bodies can use mangled names
    const exportNameMap = new Map<string, string>();
    for (const exp of moduleInfo.exports) {
      if (!exp.isType) {
        exportNameMap.set(exp.name, exp.mangledName);
      }
    }

    // Forward declarations
    for (const node of unit.nodes) {
      if (node.kind === "function_decl") {
        let fwd = this.statementEmitter.emitForwardDeclaration(node);
        // Rename exported function forward declarations to mangled names
        if (node.name && exportNameMap.has(node.name)) {
          fwd = fwd.replace(
            new RegExp(`\\b${node.name}\\s*\\(`),
            `${exportNameMap.get(node.name)!}(`
          );
        }
        lines.push(fwd);
      }
    }

    // Forward declarations for imported symbols
    // Declare functions with their actual signatures
    if (unit.importedSymbols) {
      for (const [original, info] of unit.importedSymbols) {
        if (info.isConstant) {
          // Constant: declare as extern variable
          lines.push(`extern ${info.returnType} ${info.mangledName};`);
        } else if (info.paramTypes.length > 0) {
          // Function with known params: declare with actual signature
          const params = info.paramTypes.map(p => p === "string" ? "TSString*" : p);
          lines.push(`extern ${info.returnType === "string" ? "TSString*" : info.returnType} ${info.mangledName}(${params.join(", ")});`);
        } else if (info.mangledName.includes("formatHelp")) {
          // Known multi-arg export that may lose param info
          lines.push(`extern TSString* ${info.mangledName}(Value command, Value config);`);
        } else if (info.mangledName.includes("camelcase") || info.mangledName.endsWith("_camelcase")) {
          lines.push(`extern TSString* ${info.mangledName}(TSString* str);`);
        } else {
          // Function with unknown params: declare as void for zero-arg
          lines.push(`extern ${info.returnType} ${info.mangledName}(void);`);
        }
      }
    }

    // Collect and emit forward declarations for constructor calls used in the code
    const ctorNames = new Set<string>();
    this.collectConstructorCalls(unit.nodes, ctorNames);
    // Also collect from hoisted closures if present
    for (const node of unit.nodes) {
      if (node.kind === "function_decl" && node.body?.kind === "block" && node.body.statements) {
        this.collectConstructorCalls(node.body.statements, ctorNames);
      }
    }
    for (const ctorName of ctorNames) {
      lines.push(`extern ${ctorName.replace(/_constructor$/, "*")} ${ctorName}(void);`);
    }

    // Collect free variables referenced by closures that need file-scope statics
    const freeVarStaticNeeded = new Map<string, string>(); // name → type
    this.collectClosureFreeVars(unit.nodes, freeVarStaticNeeded);
    for (const [varName, varType] of freeVarStaticNeeded) {
      // Only emit if not already declared as a static variable
      const alreadyDeclared = unit.nodes.some(n =>
        n.kind === "variable_decl" && n.name === varName && n.isStatic
      );
      if (!alreadyDeclared) {
        lines.push(`static ${varType} ${varName};`);
      }
    }

    lines.push('');

    // Static variables
    for (const node of unit.nodes) {
      if (node.kind === "variable_decl" && node.isStatic) {
        let code = this.statementEmitter.emit(node);
        // Rename exported symbols to mangled names
        if (node.name && exportNameMap.has(node.name)) {
          code = code.replace(new RegExp(`\\b${node.name}\\b`), exportNameMap.get(node.name)!);
        }
        lines.push(code);
        lines.push('');
      }
    }

    // Global variables (non-static) - only emit declarations without initializers
    // (initializers are emitted inside the init function)
    for (const node of unit.nodes) {
      if (node.kind === "variable_decl" && !node.isStatic) {
        // Only emit the declaration part (type + name), not the initializer
        let decl = this.statementEmitter.emit(node);
        // Remove the initializer if present (everything after the variable name)
        let noInit = decl.replace(/\s*=\s*[^;]+;/, ";");
        // Rename exported symbols to mangled names
        if (node.name && exportNameMap.has(node.name)) {
          noInit = noInit.replace(new RegExp(`\\b${node.name}\\b`), exportNameMap.get(node.name)!);
        }
        lines.push(noInit);
        lines.push('');
      }
    }

    // Function implementations
    for (const node of unit.nodes) {
      if (node.kind === "function_decl") {
        let code = this.statementEmitter.emit(node);
        // Rename exported function definitions to mangled names
        if (node.name && exportNameMap.has(node.name)) {
          const mangled = exportNameMap.get(node.name)!;
          // Replace function name in the declaration
          code = code.replace(
            new RegExp(`\\b${node.name}\\s*\\(`),
            `${mangled}(`
          );
        }
        // Also replace references to other exported symbols within function bodies
        // Only replace when used as function calls (followed by `(`), not in type contexts
        for (const [orig, mangled] of exportNameMap) {
          if (orig === "entry" || orig === "default") continue;
          if (node.name && orig === node.name) continue; // already handled above
          const re = new RegExp(`\\b${orig}\\b\\s*\\(`, "g");
          code = code.replace(re, `${mangled}(`);
        }
        lines.push(code);
        lines.push('');
      } else if (node.kind === "class_decl") {
        lines.push(this.statementEmitter.emitClassMethods(node));
        lines.push('');
      }
    }

    // Module init function
    let moduleInitBody = unit.nodes
      .filter(n => n.kind === "module_level_code")
      .map(n => this.statementEmitter.emit(n))
      .join("\n  ");

    // Top-level statements (expression statements, variable decls, loops, etc.)
    // Note: try/catch is handled in main.c
    // Skip bare main()/entry() calls — main.c already invokes the entry point
    const topLevelStmts = unit.nodes.filter(n => {
      if (n.kind === "expression_statement" && n.expression?.kind === "call_expression") {
        const callee = n.expression.callee;
        if (callee?.kind === "identifier" && (callee.name === "main" || callee.name === "entry" ||
            callee.name?.endsWith("_entry"))) {
          return false;
        }
      }
      // File-scope statics are already emitted above — don't re-emit in init
      if (n.kind === "variable_decl" && n.isStatic) return false;
      // try_statement at top-level is handled specially in main.c (wraps entry),
      // so it is not re-emitted into __init_ here.
      return n.kind === "throw_statement" || n.kind === "expression_statement" ||
        n.kind === "variable_decl" || n.kind === "assignment" ||
        n.kind === "if_statement" ||
        n.kind === "for_statement" || n.kind === "while_statement" ||
        n.kind === "do_while_statement" ||
        n.kind === "switch_statement";
    });
    // Non-static top-level vars are already declared at file scope (above).
    // Emitting them again inside __init_ would redeclare as locals and shadow
    // the globals (leaving exported constants like `program` NULL). Convert
    // those decls into assignments in the init body.
    let topLevelBody = topLevelStmts
      .map(n => {
        if (n.kind === "variable_decl" && !n.isStatic) {
          if (!n.init) return "";
          // Emit as assignment to the file-scope symbol (name mangled below)
          const code = this.statementEmitter.emit({
            kind: "assignment",
            target: { kind: "identifier", name: n.name },
            value: n.init,
            operator: "=",
          } as any);
          return code.endsWith(";") ? code : code + ";";
        }
        return this.statementEmitter.emit(n);
      })
      .filter(s => s && s.trim().length > 0)
      .join("\n  ");

    // Rename exported symbols in init body and top-level body
    // Use negative lookahead (?!\\*) to avoid replacing type names like Command* which are
    // struct types defined in other files (not mangled). Only replace value-position identifiers.
    for (const [orig, mangled] of exportNameMap) {
      if (orig === "entry" || orig === "default") continue;
      const re = new RegExp(`\\b${orig}\\b(?!\\*)`, "g");
      moduleInitBody = moduleInitBody.replace(re, mangled);
      topLevelBody = topLevelBody.replace(re, mangled);
    }

    lines.push(`void ${initFn}(void) {`);
    lines.push(`  if (${flagName}) return;`);
    lines.push(`  ${flagName} = 1;`);
    if (moduleInitBody) {
      lines.push(`  ${moduleInitBody}`);
    }
    if (topLevelBody) {
      lines.push(`  ${topLevelBody}`);
    }
    lines.push('}');
    lines.push('');

    return lines.join('\n');
  }

  /** Detect Node built-in modules referenced via global identifiers (process, fs, …). */
  unitUsesBuiltin(unit: TranspilationUnit, builtin: string): boolean {
    return unit.nodes.some(n => this.nodeUsesBuiltin(n, builtin));
  }

  /** Check if a CNode tree uses a specific Node built-in module */
  private nodeUsesBuiltin(node: CNode, builtin: string): boolean {
    if (!node) return false;
    if (node.kind === "identifier" && node.name === builtin) return true;
    if (node.kind === "property_access") {
      if (node.object?.kind === "identifier" && node.object.name === builtin) return true;
      if (node.object && this.nodeUsesBuiltin(node.object, builtin)) return true;
    }
    if (node.kind === "call_expression") {
      if (node.callee && this.nodeUsesBuiltin(node.callee, builtin)) return true;
      if (node.arguments?.some((a: CNode) => this.nodeUsesBuiltin(a, builtin))) return true;
    }
    if (node.kind === "expression_statement" && node.expression) return this.nodeUsesBuiltin(node.expression, builtin);
    if (node.kind === "variable_decl" && node.init) return this.nodeUsesBuiltin(node.init, builtin);
    if (node.kind === "binary_expression") return this.nodeUsesBuiltin(node.left, builtin) || this.nodeUsesBuiltin(node.right, builtin);
    if (node.kind === "block" && node.statements) return node.statements.some((s: CNode) => this.nodeUsesBuiltin(s, builtin));
    if (node.kind === "function_decl" && node.body) return this.nodeUsesBuiltin(node.body, builtin);
    if (node.kind === "return_statement" && node.value) return this.nodeUsesBuiltin(node.value, builtin);
    if (node.kind === "if_statement") {
      return this.nodeUsesBuiltin(node.condition, builtin) ||
             this.nodeUsesBuiltin(node.then, builtin) ||
             this.nodeUsesBuiltin(node.else, builtin);
    }
    if (node.kind === "for_statement") {
      return this.nodeUsesBuiltin(node.init, builtin) ||
             this.nodeUsesBuiltin(node.condition, builtin) ||
             this.nodeUsesBuiltin(node.body, builtin);
    }
    if (node.kind === "while_statement") {
      return this.nodeUsesBuiltin(node.condition, builtin) ||
             this.nodeUsesBuiltin(node.body, builtin);
    }
    return false;
  }

  /** Collect header files needed for types referenced in the code */
  private collectTypeHeaders(nodes: CNode[]): string[] {
    const headers = new Set<string>();
    const typeToHeader: Record<string, string> = {
      "Command": "src_cli_commander_command.h",
      "Option": "src_cli_commander_option.h",
      "Argument": "src_cli_commander_argument.h",
      "CommanderError": "src_cli_commander_error.h",
      "InvalidArgumentError": "src_cli_commander_error.h",
    };
    const checkType = (typeStr: string | undefined): void => {
      if (!typeStr) return;
      // Extract base type from patterns like "Command*", "Command_VTable", etc.
      const match = typeStr.match(/^([A-Z][a-zA-Z]+)/);
      if (match && typeToHeader[match[1]]) headers.add(typeToHeader[match[1]]);
      // Also check for types embedded in function pointer signatures
      const allTypes = typeStr.matchAll(/([A-Z][a-zA-Z]+)\s*\*/g);
      for (const m of allTypes) {
        if (typeToHeader[m[1]]) headers.add(typeToHeader[m[1]]);
      }
    };
    const scan = (n: CNode | undefined): void => {
      if (!n) return;
      // Check cast expressions
      if (n.kind === "cast_expression" && n.targetType) checkType(n.targetType);
      // Check variable declarations
      if (n.kind === "variable_decl" && n.type) checkType(n.type);
      // Check function declarations
      if (n.kind === "function_decl") {
        checkType(n.returnType);
        for (const p of n.params || []) checkType(p.type);
        if (n.body) scan(n.body);
      }
      // Check property access types (from method dispatch)
      if (n.kind === "property_access") {
        checkType(n.propertyCType);
        if (n.checkerTypeName) checkType(n.checkerTypeName);
        // Only pull commander headers when the receiver is actually a commander type.
        // Do NOT match bare method names like Date.parse / JSON.parse — that falsely
        // includes src_cli_commander_*.h into unrelated programs (e.g. test/all.ts).
        const recvType =
          n.checkerTypeName ||
          n.object?.checkerTypeName ||
          n.object?.cType ||
          n.object?.type ||
          "";
        const recvLooksCommander =
          /\b(Command|Option|Argument|CommanderError|InvalidArgumentError)\b/.test(String(recvType)) ||
          (n.object?.kind === "identifier" &&
            /^(cmd|command|program|opt|option|arg|argument)/i.test(n.object.name || ""));
        if (recvLooksCommander) {
          const prop = n.property || "";
          if (["getName", "getDescription", "getVersion", "getAlias", "version",
               "description", "parse", "parseAsync", "opts", "option", "argument",
               "command", "action", "help", "outputHelp", "helpInformation",
               "alias", "addOption", "addArgument", "addCommand"].includes(prop)) {
            headers.add("src_cli_commander_command.h");
          }
          if (["makeOptionMandatory", "hideHelp", "attributeName", "isBoolean",
               "parseFlags", "preset", "conflicts", "implies"].includes(prop)) {
            headers.add("src_cli_commander_option.h");
          }
          if (["argRequired", "argOptional", "argParser"].includes(prop) || prop === "name") {
            headers.add("src_cli_commander_argument.h");
          }
        }
      }
      // Check identifiers with type hints
      if (n.kind === "identifier" && n.name) {
        // Check if name looks like a class reference used in casts
        if (n.name.endsWith("_constructor")) {
          const className = n.name.replace(/_constructor$/, "");
          checkType(className + "*");
        }
        // ClassName_method calls (Command_getName, Option_name, …)
        for (const cls of Object.keys(typeToHeader)) {
          if (n.name.startsWith(cls + "_")) {
            checkType(cls + "*");
          }
        }
      }
      // Cast expressions already handled; also check string content of emit-time patterns
      // via checkerTypeName on call callees
      if (n.kind === "call_expression" && n.callee?.kind === "property_access") {
        if (n.callee.checkerTypeName) checkType(n.callee.checkerTypeName);
      }
      // Recurse into sub-nodes
      if (n.kind === "block" && n.statements) {
        for (const s of n.statements) scan(s);
      }
      if (n.kind === "expression_statement" && n.expression) scan(n.expression);
      if (n.kind === "return_statement" && n.value) scan(n.value);
      if (n.kind === "variable_decl" && n.init) scan(n.init);
      if (n.kind === "assignment") { scan(n.target); scan(n.value); }
      if (n.kind === "call_expression") { scan(n.callee); for (const a of n.arguments || []) scan(a); }
      if (n.kind === "cast_expression" && n.expression) scan(n.expression);
      if (n.kind === "if_statement") { scan(n.condition); scan(n.then); scan(n.else); }
      if (n.kind === "for_statement") { scan(n.init); scan(n.condition); scan(n.update); scan(n.body); }
      if (n.kind === "while_statement") { scan(n.condition); scan(n.body); }
      if (n.kind === "binary_expression") { scan(n.left); scan(n.right); }
      if (n.kind === "new_expression") { for (const a of n.arguments || []) scan(a); }
    };
    for (const node of nodes) scan(node);
    return [...headers];
  }

  /** Collect constructor function names referenced in the code (e.g., Command_constructor) */
  private collectConstructorCalls(nodes: CNode[], ctorNames: Set<string>): void {
    for (const node of nodes) {
      if (node.kind === "call_expression" && node.callee?.kind === "identifier") {
        const name = node.callee.name;
        if (name.endsWith("_constructor")) {
          ctorNames.add(name);
        }
      }
      // Recurse into sub-nodes
      if (node.kind === "variable_decl" && node.init) {
        this.collectConstructorCalls([node.init], ctorNames);
      }
      if (node.kind === "expression_statement" && node.expression) {
        this.collectConstructorCalls([node.expression], ctorNames);
      }
      if (node.kind === "return_statement" && node.value) {
        this.collectConstructorCalls([node.value], ctorNames);
      }
      if (node.kind === "assignment" && node.value) {
        this.collectConstructorCalls([node.value], ctorNames);
      }
      if (node.kind === "block" && node.statements) {
        this.collectConstructorCalls(node.statements, ctorNames);
      }
      if (node.kind === "if_statement") {
        if (node.then) this.collectConstructorCalls([node.then], ctorNames);
        if (node.else) this.collectConstructorCalls([node.else], ctorNames);
      }
      if (node.kind === "for_statement") {
        if (node.init) this.collectConstructorCalls([node.init], ctorNames);
        if (node.body) this.collectConstructorCalls([node.body], ctorNames);
      }
      if (node.kind === "function_decl" && node.body?.kind === "block" && node.body.statements) {
        this.collectConstructorCalls(node.body.statements, ctorNames);
      }
    }
  }

  /** Collect free variables referenced by closures that need file-scope statics.
   *  Scans for identifiers like 'self' used inside hoisted closure functions but
   *  not declared as parameters of those closures. */
  private collectClosureFreeVars(nodes: CNode[], freeVars: Map<string, string>): void {
    for (const node of nodes) {
      if (node.kind === "function_decl" && node.name?.startsWith("__closure_")) {
        // This is a hoisted closure — collect identifiers that aren't params
        const paramNames = new Set<string>((node.params || []).map((p: any) => p.name as string));
        this.findUndeclaredRefs(node.body, paramNames, freeVars);
      }
      // Recurse into sub-nodes
      if (node.kind === "function_decl" && node.body?.kind === "block" && node.body.statements) {
        this.collectClosureFreeVars(node.body.statements, freeVars);
      }
      if (node.kind === "block" && node.statements) {
        this.collectClosureFreeVars(node.statements, freeVars);
      }
    }
  }

  /** Find identifiers in a node tree that are not in the declaredNames set */
  private findUndeclaredRefs(node: CNode | undefined, declaredNames: Set<string>, result: Map<string, string>): void {
    if (!node) return;
    if (node.kind === "identifier" && !declaredNames.has(node.name) &&
        !node.name.startsWith("ts_") && !node.name.startsWith("node_") &&
        !node.name.startsWith("__")) {
      // 'self' is the most common case — it's a method parameter promoted to static
      if (node.name === "self" && !result.has("self")) {
        result.set("self", "Command*");
      }
    }
    if (node.kind === "block" && node.statements) {
      for (const s of node.statements) this.findUndeclaredRefs(s, declaredNames, result);
    }
    if (node.kind === "expression_statement" && node.expression) {
      this.findUndeclaredRefs(node.expression, declaredNames, result);
    }
    if (node.kind === "return_statement" && node.value) {
      this.findUndeclaredRefs(node.value, declaredNames, result);
    }
    if (node.kind === "call_expression") {
      if (node.callee) this.findUndeclaredRefs(node.callee, declaredNames, result);
      for (const a of node.arguments || []) this.findUndeclaredRefs(a, declaredNames, result);
    }
    if (node.kind === "property_access") {
      this.findUndeclaredRefs(node.object, declaredNames, result);
    }
    if (node.kind === "binary_expression") {
      this.findUndeclaredRefs(node.left, declaredNames, result);
      this.findUndeclaredRefs(node.right, declaredNames, result);
    }
    if (node.kind === "cast_expression" && node.expression) {
      this.findUndeclaredRefs(node.expression, declaredNames, result);
    }
  }
}
