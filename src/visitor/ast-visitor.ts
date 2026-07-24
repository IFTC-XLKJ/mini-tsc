import * as ts from "typescript";
import * as path from "path";
import * as fs from "fs";
import type { TypeMapper } from "../types/type-mapper.js";
import type { ModuleGraph } from "../modules/module-resolver.js";
import type { SymbolMangler } from "../modules/symbol-mangler.js";
import type { CNode, TranspilationUnit } from "../codegen/c-emitter.js";
import { getBuiltinModule, BUILTIN_MODULES } from "../builtins/registry.js";

export interface VisitorContext {
  checker: ts.TypeChecker;
  program?: ts.Program;
  typeMapper: TypeMapper;
  moduleResolver: ModuleGraph;
  mangler: SymbolMangler;
  currentFile: string;
  scope: IScopeStack;
  output: CNode[];
}

export interface IScopeStack {
  push(): void;
  pop(): void;
  declare(name: string, type: any): void;
  lookup(name: string): any;
}

export class AstVisitor {
  private ctx: VisitorContext;
  private closureCounter = 0;
  private hoistedClosures: CNode[] = [];
  /** Namespace import name → module file path (for `import * as X from "..."`) */
  private namespaceModulePaths: Map<string, string> = new Map();
  /** Named imports from Node builtins: localName → C symbol (e.g. isMainThread → node_worker_threads_isMainThread) */
  private nodeBuiltinNamedImports: Map<string, string> = new Map();
  /** True when visiting inside a constructor (return self should NOT wrap in Value) */
  private inConstructor = false;
  /** Current function's return type — used to decide if `return this` needs Value wrapping */
  private currentReturnType = "void";
  /** Free-var renames active while visiting a closure body: outerName → __cap_N_name */
  private activeCaptures: Map<string, string> = new Map();

  constructor(ctx: VisitorContext) {
    this.ctx = ctx;
  }

  visitSourceFile(sourceFile: ts.SourceFile): TranspilationUnit {
    this.ctx.output = [];
    this.hoistedClosures = [];
    this.closureCounter = 0;
    this.namespaceModulePaths = new Map();
    this.nodeBuiltinNamedImports = new Map();
    const typeDefinitions: CNode[] = [];
    const imports: { filePath: string; symbols: string[] }[] = [];
    const importedSymbols = new Map<string, { mangledName: string; returnType: string; paramTypes: string[]; isConstant?: boolean }>();
    const namespaceModulePaths = new Map<string, string>();

    ts.forEachChild(sourceFile, (node) => {
      this.visit(node, typeDefinitions, imports);
    });

    // Prepend hoisted file-scope closures so they appear before use
    if (this.hoistedClosures.length > 0) {
      this.ctx.output = [...this.hoistedClosures, ...this.ctx.output];
    }

    // Named Node builtin imports: local → node_<mod>_<export>
    // Use registry to get correct function signatures
    for (const [local, cName] of this.nodeBuiltinNamedImports) {
      // Extract module name and function name from cName (e.g., "node_worker_threads_Worker")
      const parts = cName.split("_");
      let moduleName = "";
      let funcName = "";
      if (parts.length >= 3) {
        // Find module name by checking against known modules
        for (const modName of BUILTIN_MODULES.keys()) {
          if (cName.startsWith(`node_${modName}_`)) {
            moduleName = modName;
            funcName = cName.substring(`node_${modName}_`.length);
            break;
          }
        }
      }

      // Look up the function signature from the registry
      let returnType = "Value";
      let paramTypes: string[] = [];
      let isConstant = false;

      if (moduleName && funcName) {
        const mod = BUILTIN_MODULES.get(moduleName);
        if (mod) {
          const func = mod.functions.find(f => f.cName === cName);
          if (func) {
            // Parse the signature to extract parameter types
            const sigMatch = func.signature.match(/^(\S+)\s+\S+\((.+)\)$/);
            if (sigMatch) {
              returnType = sigMatch[1];
              const paramStr = sigMatch[2].trim();
              if (paramStr && paramStr !== "void") {
                paramTypes = paramStr.split(",").map(p => {
                  const parts = p.trim().split(/\s+/);
                  return parts.length >= 2 ? parts.slice(0, -1).join(" ") : "Value";
                });
              }
            }
            // Check if it's a getter (no params)
            if (paramTypes.length === 0 && func.signature.includes("(void)")) {
              isConstant = true;
            }
          }
        }
      }

      // Fallback to original logic if registry lookup failed
      if (paramTypes.length === 0 && !isConstant) {
        isConstant = /^(isMainThread|parentPort|workerData|threadId|threadName|isInternalThread|SHARE_ENV|resourceLimits|locks|defaultMaxListeners|argv|env|pid|platform|EOL|devNull|stdin|stdout|stderr)$/.test(local) ||
          cName.includes("_isMainThread") || cName.includes("_parentPort") || cName.includes("_workerData") ||
          cName.includes("_threadId") || cName.includes("_threadName") || cName.includes("_SHARE_ENV") ||
          cName.includes("_resourceLimits") || cName.includes("_locks") || cName.includes("_isInternalThread");
      }

      importedSymbols.set(local, {
        mangledName: isConstant ? `${cName}()` : cName,
        returnType,
        paramTypes,
        isConstant,
      });
    }

    // Build imported symbols map from imports
    for (const imp of imports) {
      for (const symbol of imp.symbols) {
        const modulePath = imp.filePath;
        const mangledName = this.ctx.mangler.mangle(modulePath, symbol);

        // Try to find the symbol in the source file to determine its type
        let isConstant = false;
        let returnType = "Value";
        let paramTypes: string[] = [];
        const normalizedModulePath = modulePath.replace(/\\/g, "/");
        const sourceFile = this.ctx.program?.getSourceFiles().find(
          sf => sf.fileName === normalizedModulePath ||
                sf.fileName === normalizedModulePath + ".ts" ||
                sf.fileName.replace(/\\/g, "/") === normalizedModulePath
        );
        if (sourceFile) {
          const visitImportTarget = (node: ts.Node): void => {
            if (ts.isFunctionDeclaration(node) && node.name?.text === symbol) {
              // It's a function - get the actual signature
              const sig = this.ctx.checker.getTypeAtLocation(node).getCallSignatures()[0];
              if (sig) {
                returnType = this.ctx.typeMapper.mapType(
                  this.ctx.checker.getReturnTypeOfSignature(sig)
                ).cType;
                // Sanitize return type for C
                if (!returnType || returnType.includes("<") || returnType.includes("|") ||
                    returnType.startsWith("struct ") || returnType === "any" || returnType === "unknown") {
                  returnType = "Value";
                }
                if (returnType === "string") returnType = "TSString*";
                paramTypes = sig.parameters.map(p => {
                  let pt: string;
                  try {
                    const paramType = this.ctx.checker.getTypeOfSymbolAtLocation(p, p.valueDeclaration!);
                    pt = this.ctx.typeMapper.mapType(paramType).cType;
                  } catch {
                    pt = "Value";
                  }
                  if (!pt || pt.includes("<") || pt.includes("|") || pt.startsWith("struct ") ||
                      pt === "any" || pt === "unknown" || pt.includes("{")) {
                    pt = "Value";
                  }
                  if (pt === "string") pt = "TSString*";
                  return pt;
                });
              } else {
                // Fallback: use AST parameter count
                paramTypes = (node.parameters || []).map(() => "Value");
                returnType = "Value";
              }
            } else if (ts.isVariableStatement(node)) {
              for (const decl of node.declarationList.declarations) {
                if (decl.name.getText() === symbol) {
                  isConstant = true;
                  const varType = this.ctx.checker.getTypeAtLocation(decl);
                  // mapType already handles NumberLiteral → double; also sanitize common aliases
                  returnType = this.ctx.typeMapper.mapType(varType).cType;
                  if (returnType === "string") returnType = "TSString*";
                  if (returnType === "number") returnType = "double";
                  if (returnType === "boolean") returnType = "int";
                  // Literal display edge cases (if mapper ever returns raw literal)
                  if (/^-?\d+(\.\d+)?([eE][+-]?\d+)?$/.test(returnType)) returnType = "double";
                  if (returnType === "true" || returnType === "false") returnType = "int";
                  if (returnType.includes("<") || returnType.includes("|") ||
                      returnType === "any" || returnType === "unknown") {
                    returnType = "Value";
                  }
                }
              }
            } else if (ts.isEnumDeclaration(node) && node.name?.text === symbol) {
              isConstant = true;
              returnType = "int";
            }
          };
          ts.forEachChild(sourceFile, visitImportTarget);
        }
        // If function still has no param info but name looks like a known multi-arg helper
        if (paramTypes.length === 0 && !isConstant && returnType === "Value") {
          // Keep void for true zero-arg; formatHelp etc. get params from AST when file found
        }

        importedSymbols.set(symbol, {
          mangledName,
          returnType,
          paramTypes,
          isConstant,
        });
      }
    }

    return {
      filePath: this.ctx.currentFile,
      nodes: this.ctx.output,
      typeDefinitions,
      imports,
      importedSymbols,
      namespaceModulePaths: this.namespaceModulePaths.size > 0 ? this.namespaceModulePaths : undefined,
    };
  }

  private visit(node: ts.Node, typeDefinitions: CNode[], imports: { filePath: string; symbols: string[] }[]): void {
    switch (node.kind) {
      case ts.SyntaxKind.FunctionDeclaration:
        this.visitFunctionDeclaration(node as ts.FunctionDeclaration);
        break;
      case ts.SyntaxKind.ClassDeclaration:
        this.visitClassDeclaration(node as ts.ClassDeclaration, typeDefinitions);
        break;
      case ts.SyntaxKind.InterfaceDeclaration:
        this.visitInterfaceDeclaration(node as ts.InterfaceDeclaration, typeDefinitions);
        break;
      case ts.SyntaxKind.EnumDeclaration:
        this.visitEnumDeclaration(node as ts.EnumDeclaration, typeDefinitions);
        break;
      case ts.SyntaxKind.VariableStatement:
        this.visitVariableStatement(node as ts.VariableStatement);
        break;
      case ts.SyntaxKind.ExpressionStatement:
        this.visitExpressionStatement(node as ts.ExpressionStatement);
        break;
      case ts.SyntaxKind.ReturnStatement:
        this.visitReturnStatement(node as ts.ReturnStatement);
        break;
      case ts.SyntaxKind.IfStatement:
        this.visitIfStatement(node as ts.IfStatement);
        break;
      case ts.SyntaxKind.WhileStatement:
        this.visitWhileStatement(node as ts.WhileStatement);
        break;
      case ts.SyntaxKind.DoStatement:
        this.visitDoStatement(node as ts.DoStatement);
        break;
      case ts.SyntaxKind.ForStatement:
        this.visitForStatement(node as ts.ForStatement);
        break;
      case ts.SyntaxKind.ForOfStatement:
        this.visitForOfStatement(node as ts.ForOfStatement);
        break;
      case ts.SyntaxKind.ForInStatement:
        this.visitForInStatement(node as ts.ForInStatement);
        break;
      case ts.SyntaxKind.BreakStatement:
        this.ctx.output.push({ kind: "break_statement" });
        break;
      case ts.SyntaxKind.ContinueStatement:
        this.ctx.output.push({ kind: "continue_statement" });
        break;
      case ts.SyntaxKind.SwitchStatement:
        this.visitSwitchStatement(node as ts.SwitchStatement);
        break;
      case ts.SyntaxKind.TryStatement:
        this.visitTryStatement(node as ts.TryStatement);
        break;
      case ts.SyntaxKind.ThrowStatement:
        this.visitThrowStatement(node as ts.ThrowStatement);
        break;
      case ts.SyntaxKind.Block:
        this.visitBlock(node as ts.Block, this.ctx.output);
        break;
      case ts.SyntaxKind.TypeAliasDeclaration:
        this.visitTypeAliasDeclaration(node as ts.TypeAliasDeclaration, typeDefinitions);
        break;
      case ts.SyntaxKind.ImportDeclaration:
        this.visitImportDeclaration(node as ts.ImportDeclaration, imports);
        break;
      case ts.SyntaxKind.ExportDeclaration:
      case ts.SyntaxKind.ExportAssignment:
        // Handled by module resolver
        break;
      default:
        ts.forEachChild(node, (child) => this.visit(child, typeDefinitions, imports));
        break;
    }
  }

  private visitFunctionDeclaration(node: ts.FunctionDeclaration): void {
    const originalName = node.name?.getText() || "anonymous";
    const sig = this.ctx.checker.getTypeAtLocation(node).getCallSignatures()[0];
    const params = sig
      ? sig.parameters.map(p => {
          const paramType = this.ctx.checker.getTypeOfSymbolAtLocation(p, p.valueDeclaration!);
          return {
            name: p.getName(),
            type: this.ctx.typeMapper.mapType(paramType).cType,
          };
        })
      : [];
    let returnType = sig
      ? this.ctx.typeMapper.mapType(this.ctx.checker.getReturnTypeOfSignature(sig)).cType
      : "Value";

    // Check if function is exported
    const hasExport = node.modifiers?.some(
      m => m.kind === ts.SyntaxKind.ExportKeyword
    ) ?? false;

    // Use mangled name for exported functions or entry point (main)
    let name = originalName;
    if (hasExport || originalName === "main") {
      const exportName = originalName === "main" ? "entry" : originalName;
      name = this.ctx.mangler.mangle(this.ctx.currentFile, exportName);
      // Entry function must return void (not Value) since it's the program entry
      if (originalName === "main") {
        returnType = "void";
      }
    }

    const body: CNode[] = [];
    if (node.body) {
      this.ctx.scope.push();
      for (const param of params) {
        this.ctx.scope.declare(param.name, param.type);
      }
      this.visitBlockTo(node.body, body);
      this.ctx.scope.pop();
    }

    this.ctx.output.push({
      kind: "function_decl",
      name,
      params,
      returnType,
      body: body.length > 0 ? { kind: "block", statements: body } : null,
    });
  }

  private visitClassDeclaration(node: ts.ClassDeclaration, typeDefinitions: CNode[]): void {
    const name = node.name?.getText() || "AnonymousClass";
    const resolved = this.ctx.typeMapper.mapClass(node);

    // Emit struct definition
    typeDefinitions.push({
      kind: "struct_decl",
      name,
      fields: [
        { name: "_vtable", type: "void*" },
        ...resolved.fields.map(f => ({ name: f.name, type: f.type.cType })),
      ],
    });

    // Emit vtable struct with correct C function pointer syntax
    if (resolved.vtable.length > 0) {
      typeDefinitions.push({
        kind: "vtable_decl",
        name: `${name}_VTable`,
        methods: resolved.vtable.map(v => ({
          name: v.methodName,
          returnType: v.returnType.cType,
          paramTypes: v.paramTypes.map(p => p.cType),
        })),
      });
    }

    // Emit methods
    for (const member of node.members) {
      if (ts.isMethodDeclaration(member)) {
        this.visitMethodDeclaration(member, name);
      } else if (ts.isConstructorDeclaration(member)) {
        this.visitConstructorDeclaration(member, name, node);
      }
    }

    // Emit destructor
    this.ctx.output.push({
      kind: "function_decl",
      name: `${name}_destructor`,
      params: [{ name: "self", type: `${name}*` }],
      returnType: "void",
      body: {
        kind: "block",
        statements: [
          {
            kind: "expression_statement",
            expression: {
              kind: "call_expression",
              callee: { kind: "identifier", name: "free" },
              arguments: [{ kind: "identifier", name: "self" }],
            },
          },
        ],
      },
    });
  }

  private visitMethodDeclaration(node: ts.MethodDeclaration, className: string): void {
    const methodName = node.name.getText();
    const fullName = this.ctx.mangler.mangleMethod(className, methodName);
    const sig = this.ctx.checker.getTypeAtLocation(node).getCallSignatures()[0];

    const params = [{ name: "self", type: `${className}*` }];
    if (sig) {
      params.push(
        ...sig.parameters.map(p => {
          const paramType = this.ctx.checker.getTypeOfSymbolAtLocation(p, p.valueDeclaration!);
          return { name: p.getName(), type: this.ctx.typeMapper.mapType(paramType).cType };
        })
      );
    }
    let returnType = sig
      ? this.ctx.typeMapper.mapType(this.ctx.checker.getReturnTypeOfSignature(sig)).cType
      : "void";
    // Chainable methods returning `this` → ClassName* (not Value)
    if (sig) {
      const retTs = this.ctx.checker.getReturnTypeOfSignature(sig);
      const retStr = this.ctx.checker.typeToString(retTs);
      if (retStr === "this" || /^this\b/.test(retStr)) {
        returnType = `${className}*`;
      }
    }
    if (returnType === "string") returnType = "TSString*";
    if (returnType === "boolean") returnType = "int";
    if (returnType === "number") returnType = "double";

    const body: CNode[] = [];
    if (node.body) {
      this.ctx.scope.push();
      for (const p of params) this.ctx.scope.declare(p.name, p.type);
      const prevReturnType = this.currentReturnType;
      this.currentReturnType = returnType;
      this.visitBlockTo(node.body, body);
      this.currentReturnType = prevReturnType;
      this.ctx.scope.pop();
    }

    this.ctx.output.push({
      kind: "function_decl",
      name: fullName,
      params,
      returnType,
      body: body.length > 0 ? { kind: "block", statements: body } : null,
    });
  }

  private visitConstructorDeclaration(node: ts.ConstructorDeclaration, className: string, classNode?: ts.ClassDeclaration): void {
    const fullName = this.ctx.mangler.mangleConstructor(className);

    // For constructors, use AST parameters directly (not type checker signatures)
    const params = node.parameters.map(p => {
      const paramType = p.type
        ? this.ctx.typeMapper.mapType(this.ctx.checker.getTypeAtLocation(p))
        : { cType: "Value" };
      return { name: p.name.getText(), type: paramType.cType };
    });

    // Build constructor body: allocate memory, then run user code, then return
    const body: CNode[] = [];

    // Add memory allocation: Point* self = (Point*)malloc(sizeof(Point));
    // Use calloc to zero-init all fields (prevents crashes from uninitialized pointers)
    body.push({
      kind: "variable_decl",
      name: "self",
      type: `${className}*`,
      init: {
        kind: "cast_expression",
        expression: {
          kind: "call_expression",
          callee: { kind: "identifier", name: "calloc" },
          arguments: [
            { kind: "number_literal", value: 1 },
            {
              kind: "call_expression",
              callee: { kind: "identifier", name: "sizeof" },
              arguments: [{ kind: "identifier", name: className }],
            },
          ],
        },
        targetType: `${className}*`,
      },
      isStatic: false,
    });

    // Emit class field initializers (TypeScript synthesizes these before the constructor body)
    // Only emit initializers for fields that the expression emitter can handle properly:
    // - Value-typed fields (object literals, function references, etc.)
    // - Skip pointer/scalar types (Command*, int, double, etc.) — zero-init from malloc is correct
    if (classNode) {
      for (const member of classNode.members) {
        if (ts.isPropertyDeclaration(member) && member.name && member.initializer) {
          const fieldName = member.name.getText();
          const resolvedType = member.type
            ? this.ctx.typeMapper.mapType(this.ctx.checker.getTypeAtLocation(member))
            : undefined;
          const fieldType = resolvedType?.cType || "Value";
          // Emit TSArray* / TSString* / Value field initializers (e.g. options = []).
          // Skip other pointer types (Command*, Option*, …) and scalars — calloc zero-init is fine.
          const isArrayOrString =
            fieldType === "TSArray*" || fieldType === "TSString*" || fieldType === "string" ||
            fieldType === "Value" || fieldType === "TSHashMap*";
          if (!isArrayOrString && (fieldType.endsWith("*") || fieldType === "int" || fieldType === "double" ||
              fieldType === "boolean" || fieldType === "number" || fieldType === "void")) {
            continue;
          }
          const initNode = this.visitExpression(member.initializer);
          body.push({
            kind: "assignment",
            target: {
              kind: "property_access",
              object: { kind: "identifier", name: "self" },
              property: fieldName,
            },
            value: initNode,
            operator: "=",
          });
        }
      }
    }

    // Add user's constructor body
    if (node.body) {
      this.ctx.scope.push();
      this.ctx.scope.declare("self", `${className}*`);
      for (const p of params) this.ctx.scope.declare(p.name, p.type);
      this.inConstructor = true;
      this.visitBlockTo(node.body, body);
      this.inConstructor = false;
      this.ctx.scope.pop();
    }

    // Add return self;
    body.push({
      kind: "return_statement",
      value: { kind: "identifier", name: "self" },
    });

    this.ctx.output.push({
      kind: "function_decl",
      name: fullName,
      params,
      returnType: `${className}*`,
      body: { kind: "block", statements: body },
    });
  }

  private visitInterfaceDeclaration(node: ts.InterfaceDeclaration, typeDefinitions: CNode[]): void {
    const resolved = this.ctx.typeMapper.mapInterface(node);
    typeDefinitions.push({
      kind: "struct_decl",
      name: resolved.name,
      fields: resolved.fields.map(f => ({ name: f.name, type: f.type.cType })),
    });
  }

  private visitEnumDeclaration(node: ts.EnumDeclaration, typeDefinitions: CNode[]): void {
    const resolved = this.ctx.typeMapper.mapEnum(node);
    typeDefinitions.push({
      kind: "enum_decl",
      name: resolved.name,
      values: resolved.values,
      isConst: resolved.isConst,
    });
  }

  private visitTypeAliasDeclaration(node: ts.TypeAliasDeclaration, typeDefinitions: CNode[]): void {
    const typeName = node.name.text;
    const resolvedType = this.ctx.typeMapper.mapType(
      this.ctx.checker.getTypeAtLocation(node)
    );
    typeDefinitions.push({
      kind: "typedef",
      originalType: resolvedType.cType,
      alias: typeName,
    });
  }

  private visitImportDeclaration(node: ts.ImportDeclaration, imports: { filePath: string; symbols: string[] }[]): void {
    if (!node.moduleSpecifier) return;
    const specifier = node.moduleSpecifier.getText().replace(/['"]/g, "");

    // Node built-in modules: register namespace / named imports without local file resolution
    const NODE_BUILTINS = new Set([
      "fs", "path", "http", "https", "net", "os", "process", "child_process",
      "crypto", "url", "util", "events", "stream", "buffer", "querystring",
      "assert", "constants", "module", "repl", "tty", "zlib", "readline",
      "worker_threads", "chalk",
    ]);
    if (NODE_BUILTINS.has(specifier)) {
      // Default import: import chalk from "chalk"
      if (node.importClause?.name) {
        const defaultName = node.importClause.name.text;
        this.ctx.scope.declare(defaultName, "Value");
        this.namespaceModulePaths.set(defaultName, `node:${specifier}`);
      }
      if (node.importClause?.namedBindings) {
        if (ts.isNamespaceImport(node.importClause.namedBindings)) {
          const namespaceName = node.importClause.namedBindings.name.text;
          this.ctx.scope.declare(namespaceName, "Value");
          // Special marker so codegen maps wt.Worker → node_worker_threads_Worker
          this.namespaceModulePaths.set(namespaceName, `node:${specifier}`);
        } else if (ts.isNamedImports(node.importClause.namedBindings)) {
          for (const el of node.importClause.namedBindings.elements) {
            const local = el.name.text;
            const imported = el.propertyName?.text || local;
            this.ctx.scope.declare(local, "Value");
            this.nodeBuiltinNamedImports.set(local, `node_${specifier}_${imported}`);
          }
        }
      }
      return;
    }

    // Resolve the import path (handles both relative and npm package imports)
    let resolvedPath: string | null = null;
    if (specifier.startsWith(".") || specifier.startsWith("/")) {
      const currentDir = path.dirname(this.ctx.currentFile);
      resolvedPath = path.resolve(currentDir, specifier);
    } else {
      // npm package — try to resolve via module resolver's logic
      resolvedPath = this.resolveNpmPackageImport(specifier);
    }

    if (!resolvedPath) return;

    const symbols: string[] = [];
    if (node.importClause) {
      if (node.importClause.name) {
        symbols.push(node.importClause.name.text);
      }
      if (node.importClause.namedBindings) {
        if (ts.isNamedImports(node.importClause.namedBindings)) {
          for (const el of node.importClause.namedBindings.elements) {
            symbols.push(el.name.text);
          }
        } else if (ts.isNamespaceImport(node.importClause.namedBindings)) {
          // `import * as commander from "commander"` — treat the namespace as a variable
          const namespaceName = node.importClause.namedBindings.name.text;
          symbols.push(namespaceName);
          // Register namespace as a Value variable (it's a module namespace object)
          this.ctx.scope.declare(namespaceName, "Value");
          // Store the namespace → module path mapping for property access resolution
          this.namespaceModulePaths.set(namespaceName, resolvedPath);
        }
      }
    }
    imports.push({ filePath: resolvedPath, symbols });
  }

  /** Resolve npm package imports by searching for matching local source files */
  private resolveNpmPackageImport(specifier: string): string | null {
    // Walk up from current file to find project root
    const entryDir = path.dirname(this.ctx.currentFile);
    let dir = path.resolve(entryDir);
    let projectRoot: string | null = null;
    for (let i = 0; i < 10; i++) {
      const pkgPath = path.join(dir, "package.json");
      if (fs.existsSync(pkgPath)) {
        projectRoot = dir;
        break;
      }
      const parent = path.dirname(dir);
      if (parent === dir) break;
      dir = parent;
    }
    if (!projectRoot) return null;

    // Search in src/ directory for a matching module
    const srcDir = path.join(projectRoot, "src");
    if (!fs.existsSync(srcDir)) return null;

    return this.findModuleInDir(specifier, srcDir);
  }

  private findModuleInDir(specifier: string, searchDir: string): string | null {
    // Direct match: searchDir/specifier/index.ts
    const directCandidates = [
      path.join(searchDir, specifier, "index.ts"),
      path.join(searchDir, specifier + ".ts"),
    ];
    for (const c of directCandidates) {
      if (fs.existsSync(c) && fs.statSync(c).isFile()) return c;
    }

    // Deep search: look for subdirectories containing a matching module
    // e.g., "commander" → src/cli/commander/index.ts (found in cli/ subdirectory)
    try {
      const entries = fs.readdirSync(searchDir);
      for (const entry of entries) {
        const sub = path.join(searchDir, entry);
        try {
          if (!fs.existsSync(sub) || !fs.statSync(sub).isDirectory()) continue;
          if (entry.startsWith(".") || entry === "node_modules") continue;

          const subCandidates = [
            path.join(sub, specifier, "index.ts"),
            path.join(sub, specifier + ".ts"),
            path.join(sub, "index.ts"),
          ];
          for (const c of subCandidates) {
            if (fs.existsSync(c) && fs.statSync(c).isFile()) {
              const baseName = path.basename(c, ".ts");
              if (baseName === specifier || baseName === "index") {
                if (entry === specifier || path.basename(path.dirname(c)) === specifier) {
                  return c;
                }
              }
            }
          }
        } catch {
          // skip entries we can't stat
        }
      }
    } catch {
      // ignore
    }

    return null;
  }

  private visitVariableStatement(node: ts.VariableStatement): void {
    const isExport = node.modifiers?.some(
      m => m.kind === ts.SyntaxKind.ExportKeyword
    ) ?? false;

    for (const decl of node.declarationList.declarations) {
      // Object destructuring: const { done, value } = expr
      if (ts.isObjectBindingPattern(decl.name)) {
        this.emitObjectDestructure(decl, isExport);
        continue;
      }

      if (!ts.isIdentifier(decl.name)) {
        // Array/other binding patterns not yet supported
        continue;
      }

      const originalName = decl.name.getText();
      const varType = this.ctx.checker.getTypeAtLocation(decl);
      let cType = this.ctx.typeMapper.mapType(varType).cType;

      // Sanitize invalid C type names (generics, unions that slipped through)
      if (cType.includes("<") || cType.includes(">") || cType.includes("|") ||
          cType.includes(" ") && cType.startsWith("struct Union_")) {
        cType = "Value";
      }
      if (cType.startsWith("struct ") && /[^a-zA-Z0-9_\s*]/.test(cType)) {
        cType = "Value";
      }

      // Namespace import property access (e.g., const program = commander.program)
      // The exported variable is a Value, not the TS type
      if (decl.initializer && ts.isPropertyAccessExpression(decl.initializer)) {
        const obj = decl.initializer.expression;
        if (ts.isIdentifier(obj) && this.namespaceModulePaths.has(obj.text)) {
          cType = "Value";
        }
      }

      // ts_await() always returns Value — force C type for await initializers
      if (decl.initializer && ts.isAwaitExpression(decl.initializer)) {
        cType = "Value";
      }

      // Node builtin C return types come from the registry signature, not TS
      // (TS may say Promise<boolean>/string[] while C returns Value).
      {
        const builtinModules = [
          "fs", "path", "process", "os", "http", "net", "child_process",
          "events", "readline", "assert", "crypto", "worker_threads",
        ];
        let initExpr: ts.Expression | undefined = decl.initializer;
        if (initExpr && ts.isAwaitExpression(initExpr)) {
          initExpr = initExpr.expression;
        }
        if (initExpr && ts.isCallExpression(initExpr) &&
            ts.isPropertyAccessExpression(initExpr.expression) &&
            ts.isIdentifier(initExpr.expression.expression) &&
            builtinModules.includes(initExpr.expression.expression.text)) {
          const modName = initExpr.expression.expression.text;
          const method = initExpr.expression.name.getText();
          const mod = getBuiltinModule(modName);
          const fn = mod?.functions.find(f => f.tsName === method);
          if (fn) {
            // signature like "Value node_fs_access(...)" or "int node_fs_existsSync(...)"
            const ret = fn.signature.split(/\s+/)[0];
            if (ret === "Value" || ret === "int" || ret === "double" ||
                ret === "void" || ret === "TSString*" || ret === "TSArray*") {
              cType = ret === "void" ? "Value" : ret;
            } else {
              cType = "Value";
            }
          } else {
            // Unknown builtin method — default to Value (async APIs)
            cType = "Value";
          }
          // await of async builtin still yields Value (Promise payload boxed)
          if (decl.initializer && ts.isAwaitExpression(decl.initializer)) {
            cType = "Value";
          }
        }
      }
      // prompt() returns Value (string | null); confirm() returns int boolean
      if (decl.initializer && ts.isCallExpression(decl.initializer) &&
          ts.isIdentifier(decl.initializer.expression)) {
        const g = decl.initializer.expression.text;
        if (g === "prompt") cType = "Value";
        if (g === "confirm") cType = "int";
      }
      // new events.EventEmitter() / new EventEmitter()
      if (decl.initializer && ts.isNewExpression(decl.initializer)) {
        const text = decl.initializer.expression.getText();
        if (text === "EventEmitter" || text.endsWith(".EventEmitter") || text.includes("EventEmitter")) {
          cType = "Value";
        }
      }
      // Server from http.createServer → Value
      if ((cType.includes("Server") || cType.endsWith("Server*")) &&
          decl.initializer && ts.isCallExpression(decl.initializer)) {
        cType = "Value";
      }
      // EventEmitter / Interface type name → Value
      if (cType.includes("EventEmitter") || cType.includes("Interface") || cType.includes("ReadLine")) {
        cType = "Value";
      }
      // readline.createInterface → Value
      if (decl.initializer && ts.isCallExpression(decl.initializer) &&
          ts.isPropertyAccessExpression(decl.initializer.expression)) {
        const obj = decl.initializer.expression.expression.getText();
        const prop = decl.initializer.expression.name.getText();
        if (obj === "readline" && prop === "createInterface") {
          cType = "Value";
        }
      }

      // Mangle exported variable names
      const name = isExport
        ? this.ctx.mangler.mangle(this.ctx.currentFile, originalName)
        : originalName;

      // Declare in scope BEFORE visiting initializer so closures can capture
      // e.g. const id = setInterval(() => clearInterval(id), 30)
      this.ctx.scope.declare(originalName, cType);
      if (isExport) {
        this.ctx.scope.declare(name, cType);
      }

      let init: CNode | undefined;
      if (decl.initializer) {
        init = this.visitExpression(decl.initializer);
      }

      // If this var was promoted to file-scope static by a nested closure, emit
      // as assignment to the static instead of a new local declaration.
      const wasPromoted = this.ctx.scope.lookup(`__static_${originalName}`) !== undefined;
      if (wasPromoted) {
        if (init) {
          this.ctx.output.push({
            kind: "assignment",
            target: { kind: "identifier", name: originalName },
            value: init,
            operator: "=",
          });
        }
      } else {
        this.ctx.output.push({
          kind: "variable_decl",
          name,
          type: cType,
          init,
          isStatic: false,
          isExport,
        });
      }
    }
  }

  /** const { a, b } = expr → temp + per-field variable decls */
  private emitObjectDestructure(decl: ts.VariableDeclaration, isExport: boolean): void {
    const pattern = decl.name as ts.ObjectBindingPattern;
    const tempName = `__destruct_${this.closureCounter++}`;
    const init = decl.initializer
      ? this.visitExpression(decl.initializer)
      : { kind: "undefined_literal" as const };

    this.ctx.output.push({
      kind: "variable_decl",
      name: tempName,
      type: "Value",
      init,
      isStatic: false,
      isExport: false,
    });
    this.ctx.scope.declare(tempName, "Value");

    for (const el of pattern.elements) {
      if (!ts.isBindingElement(el) || el.dotDotDotToken) continue;
      if (!ts.isIdentifier(el.name)) continue;
      const localName = el.name.text;
      const propName = el.propertyName
        ? (ts.isIdentifier(el.propertyName) ? el.propertyName.text : el.propertyName.getText())
        : localName;

      const elType = this.ctx.checker.getTypeAtLocation(el.name);
      let cType = this.ctx.typeMapper.mapType(elType).cType;
      if (cType.includes("<") || cType.includes(">") || cType.startsWith("struct Union_") ||
          cType.includes("|") || /[^a-zA-Z0-9_*\s]/.test(cType)) {
        cType = "Value";
      }
      // Destructuring from Value objects: prefer Value for unknown object props
      if (cType === "void" || cType === "any") cType = "Value";

      const name = isExport
        ? this.ctx.mangler.mangle(this.ctx.currentFile, localName)
        : localName;

      this.ctx.output.push({
        kind: "variable_decl",
        name,
        type: cType,
        init: {
          kind: "property_access",
          object: { kind: "identifier", name: tempName },
          property: propName,
          objectType: "any",
          propertyType: cType === "TSString*" || cType === "string" ? "string" :
                        cType === "double" || cType === "number" ? "number" :
                        cType === "int" || cType === "boolean" ? "boolean" : "any",
          propertyCType: cType,
        },
        isStatic: false,
        isExport,
      });
      this.ctx.scope.declare(localName, cType);
      if (isExport) this.ctx.scope.declare(name, cType);
    }
  }

  private visitExpressionStatement(node: ts.ExpressionStatement): void {
    const expr = this.visitExpression(node.expression);
    this.ctx.output.push({
      kind: "expression_statement",
      expression: expr,
    });
  }

  private visitReturnStatement(node: ts.ReturnStatement): void {
    const value = node.expression ? this.visitExpression(node.expression) : undefined;
    // `return this` in Value-returning methods (NOT constructors) — wrap in Value struct
    // When return type is ClassName*, leave bare self (pointer return)
    if (value && value.kind === "identifier" && value.name === "self" &&
        !this.inConstructor && this.currentReturnType === "Value") {
      this.ctx.output.push({
        kind: "return_statement",
        value: {
          kind: "cast_expression",
          expression: value,
          targetType: "Value",
        },
      });
    } else {
      this.ctx.output.push({
        kind: "return_statement",
        value,
      });
    }
  }

  private visitIfStatement(node: ts.IfStatement): void {
    const condition = this.visitExpression(node.expression);
    const thenBlock: CNode[] = [];
    const elseBlock: CNode[] = [];

    this.ctx.scope.push();
    this.visitStatementOrBlockTo(node.thenStatement, thenBlock);
    this.ctx.scope.pop();

    if (node.elseStatement) {
      this.ctx.scope.push();
      this.visitStatementOrBlockTo(node.elseStatement, elseBlock);
      this.ctx.scope.pop();
    }

    this.ctx.output.push({
      kind: "if_statement",
      condition,
      then: { kind: "block", statements: thenBlock },
      else: elseBlock.length > 0 ? { kind: "block", statements: elseBlock } : undefined,
    });
  }

  private visitWhileStatement(node: ts.WhileStatement): void {
    const condition = this.visitExpression(node.expression);
    const body: CNode[] = [];
    this.ctx.scope.push();
    this.visitStatementOrBlockTo(node.statement, body);
    this.ctx.scope.pop();

    this.ctx.output.push({
      kind: "while_statement",
      condition,
      body: { kind: "block", statements: body },
    });
  }

  private visitDoStatement(node: ts.DoStatement): void {
    const condition = this.visitExpression(node.expression);
    const body: CNode[] = [];
    this.ctx.scope.push();
    this.visitStatementOrBlockTo(node.statement, body);
    this.ctx.scope.pop();

    this.ctx.output.push({
      kind: "do_while_statement",
      condition,
      body: { kind: "block", statements: body },
    });
  }

  private visitForStatement(node: ts.ForStatement): void {
    this.ctx.scope.push();

    let init: CNode | undefined;
    if (node.initializer) {
      if (ts.isVariableDeclarationList(node.initializer)) {
        const first = node.initializer.declarations[0];
        if (first) {
          const varType = this.ctx.checker.getTypeAtLocation(first);
          init = {
            kind: "variable_decl",
            name: first.name.getText(),
            type: this.ctx.typeMapper.mapType(varType).cType,
            init: first.initializer ? this.visitExpression(first.initializer) : undefined,
            isStatic: false,
          };
          this.ctx.scope.declare(first.name.getText(), init.type);
        }
      } else {
        init = this.visitExpression(node.initializer);
      }
    }

    const condition = node.condition ? this.visitExpression(node.condition) : undefined;
    const update = node.incrementor ? this.visitExpression(node.incrementor) : undefined;
    const body: CNode[] = [];
    this.visitStatementOrBlockTo(node.statement, body);

    this.ctx.scope.pop();

    this.ctx.output.push({
      kind: "for_statement",
      init,
      condition,
      update,
      body: { kind: "block", statements: body },
    });
  }

  private visitForOfStatement(node: ts.ForOfStatement): void {
    this.ctx.scope.push();

    let iterVar: CNode | undefined;
    if (ts.isVariableDeclarationList(node.initializer)) {
      const first = node.initializer.declarations[0];
      if (first) {
        const varType = this.ctx.checker.getTypeAtLocation(first);
        iterVar = {
          kind: "variable_decl",
          name: first.name.getText(),
          type: this.ctx.typeMapper.mapType(varType).cType,
          init: undefined,
          isStatic: false,
        };
        this.ctx.scope.declare(first.name.getText(), iterVar.type);
      }
    }

    const iterable = this.visitExpression(node.expression);
    const iterableTsType = this.ctx.checker.getTypeAtLocation(node.expression);
    const iterableMapped = this.ctx.typeMapper.mapType(iterableTsType);
    const body: CNode[] = [];
    this.visitStatementOrBlockTo(node.statement, body);

    this.ctx.scope.pop();

    this.ctx.output.push({
      kind: "for_of_statement",
      iterVar,
      iterable,
      iterableType: iterableMapped.kind,
      iterableCType: iterableMapped.cType,
      body: { kind: "block", statements: body },
    });
  }

  private visitForInStatement(node: ts.ForInStatement): void {
    this.ctx.scope.push();

    let iterVar: CNode | undefined;
    if (ts.isVariableDeclarationList(node.initializer)) {
      const first = node.initializer.declarations[0];
      if (first) {
        iterVar = {
          kind: "variable_decl",
          name: first.name.getText(),
          type: "TSString*",
          init: undefined,
          isStatic: false,
        };
        this.ctx.scope.declare(first.name.getText(), "TSString*");
      }
    }

    const iterable = this.visitExpression(node.expression);
    const body: CNode[] = [];
    this.visitStatementOrBlockTo(node.statement, body);

    this.ctx.scope.pop();

    this.ctx.output.push({
      kind: "for_in_statement",
      iterVar,
      iterable,
      body: { kind: "block", statements: body },
    });
  }

  private visitSwitchStatement(node: ts.SwitchStatement): void {
    const expression = this.visitExpression(node.expression);
    const cases: CNode[] = [];

    for (const clause of node.caseBlock.clauses) {
      const test = clause.kind === ts.SyntaxKind.CaseClause
        ? this.visitExpression(clause.expression)
        : undefined;
      const stmts: CNode[] = [];
      for (const stmt of clause.statements) {
        this.visitStatementOrBlockTo(stmt, stmts);
      }
      cases.push({
        kind: "switch_case",
        test,
        statements: stmts,
      });
    }

    this.ctx.output.push({
      kind: "switch_statement",
      expression,
      cases,
    });
  }

  private visitTryStatement(node: ts.TryStatement): void {
    const tryBlock: CNode[] = [];
    this.visitBlockTo(node.tryBlock, tryBlock);

    let catchClause: { errorVar: string; body: CNode[] } | undefined;
    if (node.catchClause) {
      const errorVar = node.catchClause.variableDeclaration?.name.getText() || "err";
      const catchBody: CNode[] = [];
      this.ctx.scope.push();
      this.ctx.scope.declare(errorVar, "Value");
      this.visitBlockTo(node.catchClause.block, catchBody);
      this.ctx.scope.pop();
      catchClause = { errorVar, body: catchBody };
    }

    this.ctx.output.push({
      kind: "try_statement",
      tryBlock: { kind: "block", statements: tryBlock },
      catchClause,
    });
  }

  private visitThrowStatement(node: ts.ThrowStatement): void {
    const value = this.visitExpression(node.expression);
    this.ctx.output.push({
      kind: "throw_statement",
      value,
    });
  }

  private visitBlock(node: ts.Block, output: CNode[]): void {
    this.ctx.scope.push();
    for (const stmt of node.statements) {
      this.visitStatementOrBlockTo(stmt, output);
    }
    this.ctx.scope.pop();
  }

  private visitBlockTo(node: ts.Block, output: CNode[]): void {
    for (const stmt of node.statements) {
      this.visitStatementOrBlockTo(stmt, output);
    }
  }

  private visitStatementOrBlockTo(node: ts.Statement, output: CNode[]): void {
    if (ts.isBlock(node)) {
      this.visitBlockTo(node, output);
    } else {
      const prevOutput = this.ctx.output;
      this.ctx.output = output;
      this.visit(node, [], []);
      this.ctx.output = prevOutput;
    }
  }

  private visitExpression(node: ts.Expression): CNode {
    switch (node.kind) {
      case ts.SyntaxKind.Identifier: {
        const name = node.getText();
        // `undefined` used as an identifier in expressions
        if (name === "undefined") {
          return { kind: "undefined_literal" };
        }
        // Free-var capture: rewrite outer name to __cap_N_name inside closure body
        const cap = this.activeCaptures.get(name);
        if (cap) {
          return { kind: "identifier", name: cap };
        }
        // Node/CommonJS globals
        if (name === "__dirname" || name === "__filename") {
          return { kind: "identifier", name };
        }
        // Handle TypeScript constructor references as string literals
        const constructors = new Set([
          "String", "Number", "Boolean", "Object", "Array",
          "Function", "RegExp", "Error", "TypeError",
          "RangeError", "SyntaxError", "ReferenceError", "Date",
          "JSON", "Request", "Response", "Headers", "Blob", "Buffer",
          "Promise", "URL", "AbortController", "AbortSignal",
          "EventTarget", "Event", "TextEncoder", "TextDecoder",
          "ArrayBuffer", "Uint8Array", "Int8Array", "Float32Array",
          "Float64Array", "Uint16Array", "Int16Array", "Uint32Array", "Int32Array",
          "Map", "Set", "WeakMap", "WeakSet", "Symbol",
          "Proxy", "Reflect", "FinalizationRegistry", "WeakRef",
          "ReadableStream", "WritableStream", "TransformStream",
          "Crypto", "CryptoKey", "SubtleCrypto",
          "FormData", "File", "FileReader", "Worker", "MessageChannel",
          "BroadcastChannel", "URLSearchParams", "DOMException",
        ]);
        // Only treat as string-literal constructor *name* when used as bare value
        // (e.g. console.log(String)). When used as callee String(x), keep identifier
        // so emitCall can rewrite to ts_to_string / etc.
        if (constructors.has(name)) {
          const parent = node.parent;
          if (parent && ts.isCallExpression(parent) && parent.expression === node) {
            return { kind: "identifier", name };
          }
          if (parent && ts.isNewExpression(parent) && parent.expression === node) {
            return { kind: "identifier", name };
          }
          return { kind: "string_literal", value: name };
        }
        // Date is handled specially - not a constructor reference
        return { kind: "identifier", name };
      }

      case ts.SyntaxKind.NumericLiteral:
        return { kind: "number_literal", value: parseFloat(node.getText()) };

      case ts.SyntaxKind.StringLiteral:
        return { kind: "string_literal", value: (node as ts.StringLiteral).text };

      case ts.SyntaxKind.TrueKeyword:
        return { kind: "boolean_literal", value: true };

      case ts.SyntaxKind.FalseKeyword:
        return { kind: "boolean_literal", value: false };

      case ts.SyntaxKind.NullKeyword:
        return { kind: "null_literal" };

      case ts.SyntaxKind.UndefinedKeyword:
        return { kind: "undefined_literal" };

      case ts.SyntaxKind.ThisKeyword:
        return { kind: "identifier", name: "self" };

      case ts.SyntaxKind.SuperKeyword:
        return { kind: "identifier", name: "super" };

      case ts.SyntaxKind.BinaryExpression: {
        const binExpr = node as ts.BinaryExpression;
        const left = this.visitExpression(binExpr.left);
        const right = this.visitExpression(binExpr.right);
        const leftType = this.ctx.checker.getTypeAtLocation(binExpr.left);

        // Assignment operators
        const op = binExpr.operatorToken.getText();
        if (op === "=" || op === "+=" || op === "-=" || op === "*=" || op === "/=" ||
            op === "%=" || op === "<<=" || op === ">>=" || op === ">>>=" ||
            op === "&=" || op === "^=" || op === "|=") {
          return {
            kind: "assignment",
            target: left,
            operator: op,
            value: right,
          };
        }

        // `in` operator → leave as binary; emitter handles Value→HashMap cast correctly
        // (must NOT cast Value struct directly to TSHashMap* — need .as.object)
        if (op === "in") {
          return {
            kind: "binary_expression",
            operator: "in",
            left,
            right,
            leftType: this.ctx.typeMapper.mapType(leftType).kind,
          };
        }

        // `instanceof` operator — emit as type check (always false for now)
        if (op === "instanceof") {
          return { kind: "boolean_literal", value: false };
        }

        return {
          kind: "binary_expression",
          operator: op,
          left,
          right,
          leftType: this.ctx.typeMapper.mapType(leftType).kind,
        };
      }

      case ts.SyntaxKind.PrefixUnaryExpression: {
        const prefixExpr = node as ts.PrefixUnaryExpression;
        return {
          kind: "unary_expression",
          operator: ts.tokenToString(prefixExpr.operator) || "+",
          operand: this.visitExpression(prefixExpr.operand),
          prefix: true,
        };
      }

      case ts.SyntaxKind.PostfixUnaryExpression: {
        const postfixExpr = node as ts.PostfixUnaryExpression;
        return {
          kind: "unary_expression",
          operator: ts.tokenToString(postfixExpr.operator) || "++",
          operand: this.visitExpression(postfixExpr.operand),
          prefix: false,
        };
      }

      case ts.SyntaxKind.TypeOfExpression: {
        const typeofExpr = node as ts.TypeOfExpression;
        return {
          kind: "unary_expression",
          operator: "typeof",
          operand: this.visitExpression(typeofExpr.expression),
          prefix: true,
        };
      }

      case ts.SyntaxKind.CallExpression: {
        const callExpr = node as ts.CallExpression;
        // Detect method chains: a.b().c() — break into temp variable
        if (ts.isPropertyAccessExpression(callExpr.expression)) {
          const propAccess = callExpr.expression;
          if (ts.isCallExpression(propAccess.expression)) {
            const tempName = `__chain_${this.closureCounter++}`;
            const innerCall = this.visitExpression(propAccess.expression);
            // Prefer the real return C type so chained string/array/class methods dispatch correctly
            const innerReturnType = this.ctx.checker.getTypeAtLocation(propAccess.expression);
            const innerReturnTypeStr = this.ctx.checker.typeToString(innerReturnType);
            let tempCType = this.ctx.typeMapper.mapType(innerReturnType).cType;
            if (!tempCType || tempCType.includes("<") || tempCType.includes("|") ||
                tempCType.startsWith("struct ") || tempCType === "any" || tempCType === "unknown") {
              tempCType = "Value";
            }
            if (tempCType === "string") tempCType = "TSString*";
            if (tempCType === "boolean") tempCType = "int";
            if (tempCType === "number") tempCType = "double";
            // `this` return from chainable class methods → ClassName*
            let checkerName = innerReturnTypeStr;
            if (innerReturnTypeStr === "this" || /^this\b/.test(innerReturnTypeStr)) {
              // Recover class name from the receiver of the inner call
              const innerCallee = propAccess.expression.expression;
              if (ts.isPropertyAccessExpression(innerCallee) || ts.isIdentifier(innerCallee) ||
                  ts.isCallExpression(innerCallee) || ts.isParenthesizedExpression(innerCallee) ||
                  ts.isPropertyAccessExpression(propAccess.expression.expression)) {
                const objType = this.ctx.checker.getTypeAtLocation(propAccess.expression.expression);
                const objStr = this.ctx.checker.typeToString(objType);
                const m = objStr.match(/^([A-Z][A-Za-z0-9_]*)/);
                if (m) {
                  checkerName = m[1];
                  tempCType = `${m[1]}*`;
                }
              }
              // Also try symbol of the this type
              const sym = (innerReturnType as any).symbol || innerReturnType.getSymbol?.();
              if (sym) {
                const n = sym.getName?.() || sym.name;
                if (n && /^[A-Z]/.test(n) && n !== "__type") {
                  checkerName = n;
                  tempCType = `${n}*`;
                }
              }
            }
            this.ctx.output.push({
              kind: "variable_decl",
              name: tempName,
              type: tempCType,
              init: innerCall,
              isStatic: false,
            });
            this.ctx.scope.declare(tempName, tempCType);
            // Get type info for the outer property access
            const outerPropType = this.ctx.checker.getTypeAtLocation(propAccess);
            const resolvedOuterPropType = this.ctx.typeMapper.mapType(outerPropType);
            return {
              kind: "call_expression",
              callee: {
                kind: "property_access",
                object: { kind: "identifier", name: tempName },
                property: propAccess.name.getText(),
                objectType: tempCType === "TSString*" ? "string"
                  : tempCType === "TSArray*" ? "array"
                  : tempCType === "Value" ? "Value"
                  : tempCType.endsWith("*") ? "class" : "any",
                propertyType: resolvedOuterPropType.kind,
                propertyCType: resolvedOuterPropType.cType,
                checkerTypeName: checkerName,
              },
              arguments: callExpr.arguments.map(a => this.visitExpression(a)),
            };
          }
        }
        return {
          kind: "call_expression",
          callee: this.visitExpression(callExpr.expression),
          arguments: callExpr.arguments.map(a => this.visitExpression(a)),
        };
      }

      case ts.SyntaxKind.NewExpression: {
        const newExpr = node as ts.NewExpression;
        return {
          kind: "new_expression",
          className: newExpr.expression.getText(),
          arguments: newExpr.arguments?.map(a => this.visitExpression(a)) || [],
        };
      }

      case ts.SyntaxKind.PropertyAccessExpression: {
        const propAccess = node as ts.PropertyAccessExpression;
        const objectType = this.ctx.checker.getTypeAtLocation(propAccess.expression);
        // Get the property type
        const propertyType = this.ctx.checker.getTypeAtLocation(propAccess);
        const resolvedPropType = this.ctx.typeMapper.mapType(propertyType);

        // Namespace import property access (e.g., commander.program)
        // The actual value is a Value, but for method dispatch we need the struct type
        if (propAccess.expression.kind === ts.SyntaxKind.Identifier) {
          const objName = propAccess.expression.getText();
          if (this.namespaceModulePaths.has(objName)) {
            // Check if property is a method (function type) — keep struct type for dispatch
            const propIsMethod = propertyType.getCallSignatures().length > 0;
            if (propIsMethod) {
              // Keep the struct type so method dispatch works (e.g., Command_version)
              return {
                kind: "property_access",
                object: this.visitExpression(propAccess.expression),
                property: propAccess.name.getText(),
                objectType: "Value",
                propertyType: resolvedPropType.kind,
                propertyCType: resolvedPropType.cType,
              };
            }
            // Variable export — force Value type
            return {
              kind: "property_access",
              object: this.visitExpression(propAccess.expression),
              property: propAccess.name.getText(),
              objectType: "Value",
              propertyType: "Value",
              propertyCType: "Value",
            };
          }
        }

        return {
          kind: "property_access",
          object: this.visitExpression(propAccess.expression),
          property: propAccess.name.getText(),
          objectType: this.ctx.typeMapper.mapType(objectType).kind,
          propertyType: resolvedPropType.kind,
          propertyCType: resolvedPropType.cType,
          // Store the checker's type name so emitter can dispatch Value methods
          checkerTypeName: this.ctx.checker.typeToString(objectType),
        };
      }

      case ts.SyntaxKind.ElementAccessExpression: {
        const elemAccess = node as ts.ElementAccessExpression;
        const objType = this.ctx.checker.getTypeAtLocation(elemAccess.expression);
        return {
          kind: "element_access",
          object: this.visitExpression(elemAccess.expression),
          index: this.visitExpression(elemAccess.argumentExpression),
          objectType: this.ctx.typeMapper.mapType(objType).kind,
        };
      }

      case ts.SyntaxKind.ConditionalExpression: {
        const cond = node as ts.ConditionalExpression;
        return {
          kind: "conditional_expression",
          condition: this.visitExpression(cond.condition),
          trueExpr: this.visitExpression(cond.whenTrue),
          falseExpr: this.visitExpression(cond.whenFalse),
        };
      }

      case ts.SyntaxKind.ParenthesizedExpression: {
        const paren = node as ts.ParenthesizedExpression;
        return {
          kind: "parenthesized",
          expression: this.visitExpression(paren.expression),
        };
      }

      case ts.SyntaxKind.ArrayLiteralExpression: {
        const arrLit = node as ts.ArrayLiteralExpression;
        return {
          kind: "array_literal",
          elements: arrLit.elements.map(e => this.visitExpression(e)),
        };
      }

      case ts.SyntaxKind.ObjectLiteralExpression: {
        const objLit = node as ts.ObjectLiteralExpression;
        const properties = objLit.properties.map(prop => {
          if (ts.isSpreadAssignment(prop)) {
            return {
              key: "...",
              value: this.visitExpression(prop.expression),
              spread: true,
            };
          }
          if (ts.isPropertyAssignment(prop)) {
            // Get key text and strip quotes if present
            let key = prop.name.getText();
            if ((key.startsWith('"') && key.endsWith('"')) ||
                (key.startsWith("'") && key.endsWith("'"))) {
              key = key.slice(1, -1);
            }
            return {
              key,
              value: this.visitExpression(prop.initializer),
            };
          }
          if (ts.isShorthandPropertyAssignment(prop)) {
            return {
              key: prop.name.text,
              value: { kind: "identifier", name: prop.name.text },
            };
          }
          return {
            key: "unknown",
            value: { kind: "identifier", name: "/* unsupported property */" },
          };
        });
        return {
          kind: "object_literal",
          properties,
        };
      }

      case ts.SyntaxKind.ArrowFunction: {
        return this.hoistClosure(node as ts.ArrowFunction);
      }

      case ts.SyntaxKind.FunctionExpression: {
        return this.hoistClosure(node as ts.FunctionExpression);
      }

      case ts.SyntaxKind.AsExpression: {
        const assertExpr = node as ts.AsExpression;
        const targetType = this.ctx.typeMapper.mapType(
          this.ctx.checker.getTypeAtLocation(assertExpr)
        );
        return {
          kind: "cast_expression",
          expression: this.visitExpression(assertExpr.expression),
          targetType: targetType.cType,
        };
      }

      case ts.SyntaxKind.TemplateExpression: {
        const tmpl = node as ts.TemplateExpression;
        return {
          kind: "template_expression",
          head: tmpl.head.text,
          templateSpans: tmpl.templateSpans.map(span => ({
            expression: this.visitExpression(span.expression),
            literal: span.literal.text,
          })),
        };
      }

      case ts.SyntaxKind.TemplateHead: {
        return { kind: "string_literal", value: (node as any).text || "" };
      }

      case ts.SyntaxKind.NoSubstitutionTemplateLiteral: {
        return { kind: "string_literal", value: (node as ts.NoSubstitutionTemplateLiteral).text };
      }

      case ts.SyntaxKind.AwaitExpression: {
        const awaitExpr = node as ts.AwaitExpression;
        return {
          kind: "await_expression",
          expression: this.visitExpression(awaitExpr.expression),
        };
      }

      case ts.SyntaxKind.VoidExpression: {
        // void expr — evaluate for side effects, result is undefined
        const voidExpr = node as ts.VoidExpression;
        return {
          kind: "unary_expression",
          operator: "void",
          operand: this.visitExpression(voidExpr.expression),
          prefix: true,
        };
      }

      case ts.SyntaxKind.RegularExpressionLiteral: {
        // Emit regex as its pattern string (for split/replace calls)
        const regex = node as ts.RegularExpressionLiteral;
        const text = regex.text;
        const lastSlash = text.lastIndexOf('/');
        const pattern = lastSlash > 0 ? text.slice(1, lastSlash) : text.slice(1);
        return { kind: "string_literal", value: pattern };
      }

      case ts.SyntaxKind.SpreadElement: {
        // Spread in call args or array literal — emit the expression directly
        // The emitter will handle wrapping into array form
        const spread = node as ts.SpreadElement;
        return this.visitExpression(spread.expression);
      }

      default:
        return { kind: "identifier", name: `/* unsupported: ${node.kind} */` };
    }
  }

  /** Hoist arrow/function expressions to file-scope C functions and return a function reference */
  private hoistClosure(node: ts.ArrowFunction | ts.FunctionExpression): CNode {
    const id = this.closureCounter++;
    const moduleSlug = this.ctx.currentFile
      .replace(/\\/g, "/")
      .replace(/^.*\//, "")
      .replace(/\.(ts|tsx|js|jsx)$/, "")
      .replace(/[^a-zA-Z0-9_]/g, "_");
    const fnName = `__closure_${moduleSlug}_${id}`;

    const sig = this.ctx.checker.getTypeAtLocation(node).getCallSignatures()[0];
    const params = (node.parameters || []).map(p => {
      const paramType = this.ctx.checker.getTypeAtLocation(p);
      let cType = this.ctx.typeMapper.mapType(paramType).cType;
      // Callback params used with runtime objects should be Value
      if (cType.includes("IncomingMessage") || cType.includes("Server") ||
          cType === "any" || cType.startsWith("struct ")) {
        cType = "Value";
      }
      // Function pointer params (closures/callbacks) are passed as Value at runtime
      if (cType.includes("(*)")) {
        cType = "Value";
      }
      // Prefer Value for loose / runtime-callback params
      const typeStr = this.ctx.checker.typeToString(paramType);
      if (typeStr.includes("IncomingMessage") || typeStr === "any" || typeStr === "unknown" ||
          typeStr === "string" || cType === "TSString*" || cType === "string") {
        // Runtime callbacks always pass Value (readline.question, timers, dialogs, …)
        cType = "Value";
      }
      return {
        name: p.name.getText(),
        type: cType,
      };
    });

    let returnType = "Value";
    if (sig) {
      const mapped = this.ctx.typeMapper.mapType(this.ctx.checker.getReturnTypeOfSignature(sig)).cType;
      if (mapped && mapped !== "void" && !mapped.startsWith("struct ")) {
        returnType = mapped;
      }
      // void-returning callbacks still use Value for uniform calling convention
      if (mapped === "void") {
        returnType = "Value";
      }
    }

    // Free variables → leading Value formals. Call sites bind a per-call snapshot
    // via ts_bind_function so loop vars (setTimeout in for-loop) are not shared.
    const freeVars = this.collectFreeVariables(node, new Set(params.map(p => p.name)));
    const captures: { outerName: string; captureName: string; type: string }[] = [];
    const captureParams: { name: string; type: string }[] = [];
    for (const name of freeVars) {
      // Always Value so timer/bind packing is uniform; body uses Value ops / ts_to_number
      captureParams.push({ name, type: "Value" });
      captures.push({ outerName: name, captureName: name, type: "Value" });
    }
    // Leading formals: captures first, then original params (e.g. resolve)
    const allParams = [...captureParams, ...params];
    // Mutate params in place for the rest of this function
    params.length = 0;
    params.push(...allParams);

    const bodyNodes: CNode[] = [];
    this.ctx.scope.push();
    for (const p of params) {
      this.ctx.scope.declare(p.name, p.type);
    }

    // Free vars are real params — no static rename
    const prevCaptures = this.activeCaptures;
    this.activeCaptures = new Map();

    if (ts.isArrowFunction(node)) {
      if (ts.isBlock(node.body)) {
        this.visitBlockTo(node.body, bodyNodes);
      } else {
        const prevOutput = this.ctx.output;
        this.ctx.output = bodyNodes;
        const expr = this.visitExpression(node.body);
        this.ctx.output = prevOutput;
        bodyNodes.push({ kind: "return_statement", value: expr });
      }
    } else if (node.body) {
      this.visitBlockTo(node.body, bodyNodes);
    }

    this.activeCaptures = prevCaptures;

    // After building body, check if any return statement calls a runtime function
    // (node_*, ts_json_*, ts_to_string, process.*, console.*, etc.) — these return Value,
    // not the TS-mapped type. Override returnType to Value to avoid type mismatches.
    // Also: setTimeout returns double but Promise executors / Value callbacks must return Value.
    if (returnType !== "Value") {
      for (const stmt of bodyNodes) {
        if (stmt.kind === "return_statement" && stmt.value) {
          if (this.callsRuntimeFunction(stmt.value) || this.returnsTimerId(stmt.value)) {
            returnType = "Value";
            break;
          }
        }
      }
    }

    // Ensure non-void closures return a Value if no explicit return.
    // Use undefined_literal (not identifier "ts_value_undefined()") so the
    // emitter does not sanitize the trailing () into underscores.
    if (returnType === "Value") {
      const hasReturn = bodyNodes.some(n => n.kind === "return_statement");
      if (!hasReturn) {
        bodyNodes.push({
          kind: "return_statement",
          value: { kind: "undefined_literal" },
        });
      }
    }

    this.ctx.scope.pop();

    this.hoistedClosures.push({
      kind: "function_decl",
      name: fnName,
      params,
      returnType,
      body: { kind: "block", statements: bodyNodes },
    });

    return {
      kind: "function_ref",
      name: fnName,
      freeVars, // outer names captured
      captures, // { outerName, captureName, type }[] for emission-site assign
    };
  }

  /** Rewrite variable_decl of `name` into assignment to file-scope static. */
  private rewriteLocalDeclToStaticAssign(nodes: CNode[], name: string): void {
    for (let i = 0; i < nodes.length; i++) {
      const n = nodes[i];
      if (n.kind === "variable_decl" && n.name === name && !n.isStatic) {
        if (n.init) {
          nodes[i] = {
            kind: "assignment",
            target: { kind: "identifier", name },
            value: n.init,
            operator: "=",
          };
        } else {
          nodes.splice(i, 1);
          i--;
        }
      } else if (n.kind === "function_decl" && n.body?.kind === "block" && n.body.statements) {
        this.rewriteLocalDeclToStaticAssign(n.body.statements, name);
      } else if (n.kind === "block" && n.statements) {
        this.rewriteLocalDeclToStaticAssign(n.statements, name);
      } else if (n.kind === "if_statement") {
        if (n.then?.kind === "block") this.rewriteLocalDeclToStaticAssign(n.then.statements || [], name);
        if (n.else?.kind === "block") this.rewriteLocalDeclToStaticAssign(n.else.statements || [], name);
      }
    }
  }

  /** Collect free variable names used inside a function-like node. */
  private collectFreeVariables(
    node: ts.ArrowFunction | ts.FunctionExpression,
    paramNames: Set<string>,
  ): string[] {
    const locals = new Set<string>(paramNames);
    const free = new Set<string>();

    const visit = (n: ts.Node): void => {
      if (ts.isVariableDeclaration(n) && ts.isIdentifier(n.name)) {
        locals.add(n.name.text);
      }
      if (ts.isIdentifier(n)) {
        const name = n.text;
        // Skip property names (obj.prop), labels, etc.
        const parent = n.parent;
        if (parent && ts.isPropertyAccessExpression(parent) && parent.name === n) return;
        if (parent && ts.isPropertyAssignment(parent) && parent.name === n) return;
        if (locals.has(name)) return;
        // Only capture if outer scope knows this name
        if (this.ctx.scope.lookup(name) !== undefined) {
          free.add(name);
        }
      }
      // Don't recurse into nested function bodies for free-var of *this* closure
      // beyond tracking their own locals — still scan nested for simplicity of v1
      ts.forEachChild(n, visit);
    };

    if (ts.isArrowFunction(node)) {
      if (ts.isBlock(node.body)) visit(node.body);
      else visit(node.body);
    } else if (node.body) {
      visit(node.body);
    }

    // Don't capture globals / builtins
    const skip = new Set([
      "console", "Math", "JSON", "Date", "Object", "Array", "String", "Number",
      "Boolean", "Error", "parseInt", "parseFloat", "isNaN", "isFinite",
      "setTimeout", "setInterval", "clearTimeout", "clearInterval",
      "alert", "confirm", "prompt",
      "undefined", "null", "true", "false", "NaN", "Infinity",
      "process", "Buffer", "global", "globalThis", "fetch", "URL", "Blob",
      "WebSocket", "WebSocketServer", "Headers", "Response", "Request",
      "WritableStream", "ReadableStream", "TransformStream",
    ]);
    return [...free].filter(n => !skip.has(n) && !/^[A-Z]/.test(n));
  }

  /** setTimeout/setInterval return double — wrap as Value when used as Promise executor body. */
  private returnsTimerId(node: CNode): boolean {
    if (!node || typeof node !== "object") return false;
    if (node.kind === "call_expression") {
      const callee = node.callee;
      if (callee?.kind === "identifier" &&
          (callee.name === "setTimeout" || callee.name === "setInterval" ||
           callee.name === "ts_set_timeout" || callee.name === "ts_set_interval")) {
        return true;
      }
    }
    return false;
  }

  /** Check if a CNode expression tree contains a call to a runtime function (node_*, ts_*, process.*, console.*). */
  private callsRuntimeFunction(node: CNode): boolean {
    if (!node || typeof node !== "object") return false;
    if (node.kind === "call_expression") {
      const callee = node.callee;
      if (callee?.kind === "identifier") {
        if (callee.name.startsWith("node_") || callee.name.startsWith("ts_")) return true;
        if (callee.name === "setTimeout" || callee.name === "setInterval" ||
            callee.name === "clearTimeout" || callee.name === "clearInterval" ||
            callee.name === "fetch" || callee.name === "Promise") return true;
      }
      if (callee?.kind === "property_access") {
        // Walk the object chain to find the root identifier
        let obj: any = callee.object;
        while (obj && obj.kind === "property_access") obj = obj.object;
        if (obj?.kind === "identifier") {
          const rootName = obj.name;
          // node_* / ts_* runtime functions
          if (rootName.startsWith("node_") || rootName.startsWith("ts_")) return true;
          // process.*, console.*, etc. → runtime functions
          if (rootName === "process" || rootName === "console" ||
              rootName === "Buffer" || rootName === "Date" ||
              rootName === "Math" || rootName === "JSON" ||
              rootName === "global" || rootName === "globalThis" ||
              rootName === "fetch" || rootName === "URL" ||
              rootName === "Blob" || rootName === "AbortController") {
            return true;
          }
        }
      }
      // Also check arguments for runtime calls
      if (node.arguments) {
        for (const arg of node.arguments) {
          if (this.callsRuntimeFunction(arg)) return true;
        }
      }
    }
    // Recurse into child nodes
    for (const key of Object.keys(node)) {
      if (key === "kind") continue;
      const v = (node as any)[key];
      if (Array.isArray(v)) {
        for (const item of v) {
          if (item && typeof item === "object" && item.kind) {
            if (this.callsRuntimeFunction(item)) return true;
          }
        }
      } else if (v && typeof v === "object" && v.kind) {
        if (this.callsRuntimeFunction(v)) return true;
      }
    }
    return false;
  }
}
