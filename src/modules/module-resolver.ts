import * as ts from "typescript";
import * as path from "path";
import * as fs from "fs";
import { SymbolMangler } from "./symbol-mangler.js";

/** Node.js built-in module names */
const NODE_BUILTINS = new Set([
  "fs", "path", "http", "https", "net", "os", "process", "child_process",
  "crypto", "url", "util", "events", "stream", "buffer", "querystring",
  "assert", "constants", "module", "repl", "tty", "zlib", "readline",
  "worker_threads", "chalk",
]);

export interface ExportEntry {
  name: string;
  isDefault: boolean;
  isType: boolean;
  mangledName: string;
  returnType?: string;
  paramTypes?: string[];
  isConstant?: boolean;
}

export interface ModuleNode {
  filePath: string;
  imports: string[];
  exports: ExportEntry[];
  usedBuiltins: string[];
}

export interface ModuleGraph {
  nodes: Map<string, ModuleNode>;
  cycles: string[][];
  sortedOrder: string[];
}

export interface ModuleInfo {
  filePath: string;
  exports: ExportEntry[];
  imports: { filePath: string; symbols: string[] }[];
}

export class ModuleResolver {
  private mangler = new SymbolMangler();

  buildGraph(sourceFiles: ts.SourceFile[], checker: ts.TypeChecker): ModuleGraph {
    const nodes = new Map<string, ModuleNode>();

    for (const sf of sourceFiles) {
      const filePath = sf.fileName;
      const imports: string[] = [];
      const exports: ExportEntry[] = [];
      const usedBuiltins: string[] = [];

      ts.forEachChild(sf, (node) => {
        // Collect imports
        if (ts.isImportDeclaration(node) && node.moduleSpecifier) {
          const specifier = node.moduleSpecifier.getText(sf).replace(/['"]/g, "");

          if (NODE_BUILTINS.has(specifier)) {
            if (!usedBuiltins.includes(specifier)) {
              usedBuiltins.push(specifier);
            }
          } else {
            const resolved = this.resolveImport(specifier, filePath);
            if (resolved) imports.push(resolved);
          }
        }

        // Collect exports
        if (ts.isExportDeclaration(node) && node.exportClause) {
          if (ts.isNamedExports(node.exportClause)) {
            for (const el of node.exportClause.elements) {
              const name = el.name.getText(sf);
              const isType = false;
              // Determine if exported symbol is a variable (constant) or function
              let isConstant = false;
              let returnType = "Value";
              // Search source file for the variable/function declaration
              ts.forEachChild(sf, (topNode) => {
                if (ts.isVariableStatement(topNode)) {
                  for (const decl of topNode.declarationList.declarations) {
                    if (decl.name.getText(sf) === name) {
                      isConstant = true;
                      try {
                        const varType = checker.getTypeAtLocation(decl);
                        returnType = mapExportVarCType(checker, varType);
                      } catch { /* fallback to Value */ }
                    }
                  }
                } else if (ts.isFunctionDeclaration(topNode) && topNode.name?.getText(sf) === name) {
                  // Function export — don't set isConstant
                }
              });
              exports.push({
                name,
                isDefault: false,
                isType,
                mangledName: this.mangler.mangle(filePath, name),
                isConstant,
                returnType,
              });
            }
          }
        }

        // Export default
        if (ts.isExportAssignment(node)) {
          exports.push({
            name: "default",
            isDefault: true,
            isType: false,
            mangledName: this.mangler.mangleDefault(filePath),
          });
        }

        // Top-level declarations that are implicitly exported
        if (ts.isFunctionDeclaration(node) && node.name) {
          const hasExport = node.modifiers?.some(
            m => m.kind === ts.SyntaxKind.ExportKeyword
          );
          if (hasExport) {
            exports.push({
              name: node.name.text,
              isDefault: false,
              isType: false,
              mangledName: this.mangler.mangle(filePath, node.name.text),
            });
          }
        }

        if (ts.isClassDeclaration(node) && node.name) {
          const hasExport = node.modifiers?.some(
            m => m.kind === ts.SyntaxKind.ExportKeyword
          );
          if (hasExport) {
            exports.push({
              name: node.name.text,
              isDefault: false,
              isType: false,
              mangledName: this.mangler.mangle(filePath, node.name.text),
            });
          }
        }

        if (ts.isVariableStatement(node)) {
          const hasExport = node.modifiers?.some(
            m => m.kind === ts.SyntaxKind.ExportKeyword
          );
          if (hasExport) {
            for (const decl of node.declarationList.declarations) {
              const name = decl.name.getText(sf);
              // Determine the C type from the TypeScript type (incl. number/string literals)
              let returnType = "Value";
              try {
                const varType = checker.getTypeAtLocation(decl);
                returnType = mapExportVarCType(checker, varType);
              } catch {
                // fallback to Value
              }
              exports.push({
                name,
                isDefault: false,
                isType: false,
                mangledName: this.mangler.mangle(filePath, name),
                isConstant: true,
                returnType,
              });
            }
          }
        }

        if (ts.isEnumDeclaration(node) && node.name) {
          const hasExport = node.modifiers?.some(
            m => m.kind === ts.SyntaxKind.ExportKeyword
          );
          if (hasExport) {
            exports.push({
              name: node.name.text,
              isDefault: false,
              isType: false,
              mangledName: this.mangler.mangle(filePath, node.name.text),
            });
          }
        }

        if (ts.isTypeAliasDeclaration(node)) {
          const hasExport = node.modifiers?.some(
            m => m.kind === ts.SyntaxKind.ExportKeyword
          );
          if (hasExport) {
            exports.push({
              name: node.name.text,
              isDefault: false,
              isType: true,
              mangledName: this.mangler.mangle(filePath, node.name.text),
            });
          }
        }

        // Re-exports
        if (ts.isExportDeclaration(node) && node.moduleSpecifier) {
          const specifier = node.moduleSpecifier.getText(sf).replace(/['"]/g, "");
          const resolved = this.resolveImport(specifier, filePath);
          if (resolved && node.exportClause && ts.isNamedExports(node.exportClause)) {
            for (const el of node.exportClause.elements) {
              const name = el.name.getText(sf);
              const localName = el.propertyName?.getText(sf) || name;
              exports.push({
                name,
                isDefault: false,
                isType: false,
                mangledName: this.mangler.mangleReExport(resolved, localName),
              });
            }
          }
        }
      });

      nodes.set(filePath, { filePath, imports, exports, usedBuiltins });
    }

    const cycles = this.detectCycles(nodes);
    const sortedOrder = this.topoSort(nodes, cycles);

    return { nodes, cycles, sortedOrder };
  }

  private resolveImport(specifier: string, fromFile: string): string | null {
    // Skip node builtins
    if (NODE_BUILTINS.has(specifier)) return null;

    // Relative imports
    if (specifier.startsWith(".") || specifier.startsWith("/")) {
      return this.resolveRelativeImport(specifier, fromFile);
    }

    // npm package imports — resolve to local source files in project src/ directory
    return this.resolveNpmPackage(specifier, fromFile);
  }

  private resolveRelativeImport(specifier: string, fromFile: string): string | null {
    const dir = path.dirname(fromFile);
    const candidates = [
      path.join(dir, specifier),
      path.join(dir, specifier + ".ts"),
      path.join(dir, specifier + ".tsx"),
      path.join(dir, specifier + ".js"),
      path.join(dir, specifier, "index.ts"),
      path.join(dir, specifier, "index.tsx"),
    ];

    for (const candidate of candidates) {
      try {
        if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) {
          return candidate;
        }
      } catch {
        // continue
      }
    }

    // Fallback: return first candidate without extension
    return path.join(dir, specifier);
  }

  /**
   * Resolve npm package imports by searching for matching local source files.
   * For example, `import * as commander from "commander"` resolves to
   * `src/cli/commander/index.ts` if it exists in the project.
   */
  private resolveNpmPackage(specifier: string, fromFile: string): string | null {
    // Search from the project root (walk up from fromFile to find package.json)
    const projectRoot = this.findProjectRoot(fromFile);
    if (!projectRoot) return null;

    // Try to find a matching local module under src/ or types/
    // e.g., "commander" → src/cli/commander/index.ts, src/commander/index.ts, etc.
    const searchDirs = [
      path.join(projectRoot, "src"),
      path.join(projectRoot, "types"),
    ];

    for (const searchDir of searchDirs) {
      if (!fs.existsSync(searchDir)) continue;
      const found = this.findModuleInDir(specifier, searchDir);
      if (found) return found;
    }

    return null;
  }

  /** Walk up from startDir to find a directory containing package.json */
  private findProjectRoot(startDir: string): string | null {
    let dir = path.resolve(startDir);
    for (let i = 0; i < 10; i++) {
      if (fs.existsSync(path.join(dir, "package.json"))) return dir;
      const parent = path.dirname(dir);
      if (parent === dir) break;
      dir = parent;
    }
    return null;
  }

  /** Search for a module by name within a directory tree */
  private findModuleInDir(specifier: string, searchDir: string): string | null {
    // Direct match: searchDir/specifier/index.ts
    const directCandidates = [
      path.join(searchDir, specifier, "index.ts"),
      path.join(searchDir, specifier + ".ts"),
      path.join(searchDir, specifier, "index.tsx"),
      path.join(searchDir, specifier + ".tsx"),
    ];
    for (const c of directCandidates) {
      if (fs.existsSync(c) && fs.statSync(c).isFile()) return c;
    }

    // Deep search: look for subdirectories containing a matching module
    // e.g., "commander" → src/cli/commander/index.ts
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
      // ignore read errors
    }

    return null;
  }

  detectCycles(nodes: Map<string, ModuleNode>): string[][] {
    const cycles: string[][] = [];
    const visited = new Set<string>();
    const inStack = new Set<string>();
    const stack: string[] = [];

    const dfs = (node: string) => {
      if (inStack.has(node)) {
        const cycleStart = stack.indexOf(node);
        cycles.push(stack.slice(cycleStart).concat(node));
        return;
      }
      if (visited.has(node)) return;

      visited.add(node);
      inStack.add(node);
      stack.push(node);

      const moduleNode = nodes.get(node);
      if (moduleNode) {
        for (const imp of moduleNode.imports) {
          if (nodes.has(imp)) dfs(imp);
        }
      }

      stack.pop();
      inStack.delete(node);
    };

    for (const node of nodes.keys()) {
      dfs(node);
    }

    return cycles;
  }

  topoSort(nodes: Map<string, ModuleNode>, cycles: string[][]): string[] {
    const inCycleNodes = new Set(cycles.flat());
    const visited = new Set<string>();
    const result: string[] = [];

    const dfs = (node: string) => {
      if (visited.has(node)) return;
      visited.add(node);

      const moduleNode = nodes.get(node);
      if (moduleNode) {
        for (const imp of moduleNode.imports) {
          if (nodes.has(imp) && !inCycleNodes.has(imp)) dfs(imp);
        }
      }

      result.push(node);
    };

    // Sort non-cycle nodes first (topological order)
    for (const node of nodes.keys()) {
      if (!inCycleNodes.has(node)) dfs(node);
    }

    // Append cycle nodes at the end (they use lazy init)
    for (const node of nodes.keys()) {
      if (inCycleNodes.has(node)) result.push(node);
    }

    return result;
  }
}

/**
 * Map a TS variable type to a C type for exported/imported constants.
 * Handles number/string/boolean literals (e.g. `const PI = 3.14159` → double).
 */
function mapExportVarCType(checker: ts.TypeChecker, varType: ts.Type): string {
  // Prefer flags for literals (typeToString returns "3.14159" not "number")
  if (varType.flags & ts.TypeFlags.NumberLike) return "double";
  if (varType.flags & ts.TypeFlags.StringLike) return "TSString*";
  if (varType.flags & (ts.TypeFlags.Boolean | ts.TypeFlags.BooleanLiteral)) return "int";
  if (varType.flags & ts.TypeFlags.EnumLike) return "int";

  const typeStr = checker.typeToString(varType);
  if (typeStr === "number" || typeStr === "bigint") return "double";
  if (typeStr === "string") return "TSString*";
  if (typeStr === "boolean") return "int";
  if (typeStr === "void") return "void";
  // Numeric / string / boolean literal display forms
  if (/^-?\d+(\.\d+)?([eE][+-]?\d+)?$/.test(typeStr)) return "double";
  if (/^["'`]/.test(typeStr)) return "TSString*";
  if (typeStr === "true" || typeStr === "false") return "int";
  if (typeStr.includes("Command")) return "Command*";
  if (typeStr.includes("Option")) return "Option*";
  if (typeStr.includes("Argument")) return "Argument*";
  return "Value";
}
