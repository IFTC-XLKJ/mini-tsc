import * as ts from "typescript";

export function defaultCompilerOptions(): ts.CompilerOptions {
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
    // Don't pull full @types/node — we provide simplified ambient modules in types/
    types: [],
  };
}
