import * as ts from "typescript";
import * as path from "path";
import * as fs from "fs";
import { defaultCompilerOptions } from "./options.js";

export interface ParseResult {
  program: ts.Program;
  checker: ts.TypeChecker;
  sourceFiles: ts.SourceFile[];
  diagnostics: ts.Diagnostic[];
}

/** Walk up from startDir to find a directory containing package.json */
function findProjectRoot(startDir: string): string {
  let dir = path.resolve(startDir);
  for (let i = 0; i < 10; i++) {
    if (fs.existsSync(path.join(dir, "package.json"))) return dir;
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return startDir;
}

/** Node.js built-in module names (to skip when scanning imports) */
const NODE_BUILTINS = new Set([
  "fs", "path", "http", "https", "net", "os", "process", "child_process",
  "crypto", "url", "util", "events", "stream", "buffer", "querystring",
  "assert", "constants", "module", "repl", "tty", "zlib", "readline",
  "worker_threads", "chalk",
]);

/** Scan a TS file for import specifiers that resolve to local source files */
function findLocalNpmImports(filePath: string, projectRoot: string): string[] {
  const result: string[] = [];
  const content = fs.readFileSync(filePath, "utf-8");

  // Match import ... from "specifier" and import "specifier"
  const importRegex = /(?:import\s+(?:.*?\s+from\s+)?|require\s*\(\s*)['"]([^'"]+)['"]/g;
  let match: RegExpExecArray | null;
  while ((match = importRegex.exec(content)) !== null) {
    const specifier = match[1];
    // Skip relative imports and node builtins
    if (specifier.startsWith(".") || specifier.startsWith("/") || NODE_BUILTINS.has(specifier)) continue;
    // Try to resolve to a local source file
    const resolved = resolveNpmToLocal(specifier, projectRoot);
    if (resolved) {
      result.push(resolved);
    }
  }
  return result;
}

/** Resolve an npm package name to a local source file in the project */
function resolveNpmToLocal(specifier: string, projectRoot: string): string | null {
  const srcDir = path.join(projectRoot, "src");
  if (!fs.existsSync(srcDir)) return null;
  return findModuleInDir(specifier, srcDir);
}

/** Search for a module by name within a directory tree */
function findModuleInDir(specifier: string, searchDir: string): string | null {
  // Direct match: searchDir/specifier/index.ts
  const directCandidates = [
    path.join(searchDir, specifier, "index.ts"),
    path.join(searchDir, specifier + ".ts"),
  ];
  for (const c of directCandidates) {
    if (fs.existsSync(c) && fs.statSync(c).isFile()) return c;
  }

  // Deep search: look for subdirectories containing a matching module
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

export function parseTypeScript(
  entryFile: string,
  compilerOptions?: ts.CompilerOptions
): ParseResult {
  const options = compilerOptions ?? defaultCompilerOptions();
  // Include project ambient type declarations (types/*.d.ts) so modules like "http" resolve
  const rootFiles = [entryFile];
  const typesDir = path.resolve(process.cwd(), "types");
  if (ts.sys.directoryExists(typesDir)) {
    for (const name of ts.sys.readDirectory(typesDir, [".ts", ".d.ts"], undefined, undefined) || []) {
      rootFiles.push(name);
    }
  }

  // Pre-scan entry file for npm-package imports that resolve to local source files
  // and add them as root files so TypeScript includes them in the program
  const projectRoot = findProjectRoot(path.dirname(path.resolve(entryFile)));
  const additionalFiles = findLocalNpmImports(entryFile, projectRoot);
  for (const f of additionalFiles) {
    if (!rootFiles.includes(f)) rootFiles.push(f);
  }

  const program = ts.createProgram(rootFiles, options);
  const checker = program.getTypeChecker();

  // Filter to only project source files (exclude lib.d.ts, node_modules, etc.)
  const entryDir = path.dirname(path.resolve(entryFile));
  const sourceFiles = program
    .getSourceFiles()
    .filter(f => {
      if (f.isDeclarationFile) return false;
      if (f.fileName.includes("node_modules")) return false;
      if (f.fileName.includes("lib.") && f.fileName.includes("typescript")) return false;
      // Include files that TypeScript resolved via imports (anywhere in the project)
      const resolved = path.resolve(f.fileName);
      return resolved.includes(entryDir) || resolved.includes(projectRoot);
    });

  // Get parse-only diagnostics (not semantic) to avoid stack overflow
  const diagnostics: ts.Diagnostic[] = [];
  for (const sf of sourceFiles) {
    const syntacticDiags = program.getSyntacticDiagnostics(sf);
    diagnostics.push(...syntacticDiags);
  }

  return { program, checker, sourceFiles, diagnostics };
}

export function loadTsConfig(searchPath: string): ts.CompilerOptions | undefined {
  const configPath = ts.findConfigFile(searchPath, ts.sys.fileExists, "tsconfig.json");
  if (!configPath) return undefined;

  const configFile = ts.readConfigFile(configPath, ts.sys.readFile);
  if (configFile.error) return undefined;

  const parsed = ts.parseJsonConfigFileContent(
    configFile.config,
    ts.sys,
    searchPath
  );

  return parsed.options;
}
