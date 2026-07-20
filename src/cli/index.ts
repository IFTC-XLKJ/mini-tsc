#!/usr/bin/env node
import { Command } from "./commander/index.js";
import * as path from "path";
import { fileURLToPath } from "url";
import { CompilerDriver, type CompilerOptions } from "../driver/compiler.js";

// Determine project root from the CLI script location
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const PROJECT_ROOT = path.resolve(__dirname, "..", "..");

const program = new Command();

program
  .name("mini-tsc")
  .description("TypeScript-to-C transpiler")
  .version("0.1.0");

program
  .command("compile")
  .alias("c")
  .description("Compile TypeScript to native executable")
  .argument("<entry>", "Entry TypeScript file")
  .option("-o, --output <name>", "Output executable name", "output")
  .option("-d, --out-dir <dir>", "Output directory for .c/.h files", "./out")
  .option("-t, --target <platform>", "Target platform (windows|linux)", process.platform === "win32" ? "windows" : "linux")
  .option("--no-runtime", "Exclude runtime library")
  .option("-v, --verbose", "Print intermediate C code")
  .option("--keep-c", "Keep generated .c/.h files")
  .option("--clang-args <args>", "Extra args to pass to clang")
  .action(async (entry: string, opts: any) => {
    const options: CompilerOptions = {
      entry,
      output: opts.output,
      outDir: opts.outDir,
      target: opts.target,
      runtime: opts.runtime,
      verbose: opts.verbose,
      keepC: opts.keepC,
      clangArgs: opts.clangArgs?.split(" "),
      projectRoot: PROJECT_ROOT,
    };

    const driver = new CompilerDriver();

    console.log(`mini-tsc: compiling ${entry}...`);

    const result = await driver.compile(options);

    if (result.diagnostics.length > 0) {
      console.log("\nDiagnostics:");
      for (const diag of result.diagnostics) {
        console.log(`  ${diag}`);
      }
    }

    if (opts.verbose && result.verbose.length > 0) {
      console.log("\nVerbose:");
      for (const msg of result.verbose) {
        console.log(`  ${msg}`);
      }
    }

    if (opts.verbose && result.files.length > 0) {
      console.log(`\nGenerated ${result.files.length} files:`);
      for (const file of result.files) {
        console.log(`  ${file.path} (${file.kind})`);
      }
    }

    if (result.success) {
      console.log(`\n✓ Compilation successful`);
      if (result.outputPath) {
        const absPath = path.resolve(result.outputPath);
        console.log(`  ${absPath}`);
      }
    } else {
      console.log(`\n✗ Compilation failed`);
      process.exit(1);
    }
  });

program
  .command("transpile")
  .alias("t")
  .description("Transpile TypeScript to C code (without compiling)")
  .argument("<entry>", "Entry TypeScript file")
  .option("-d, --out-dir <dir>", "Output directory for .c/.h files", "./out")
  .option("-v, --verbose", "Print generated C code")
  .action(async (entry: string, opts: any) => {
    const options: CompilerOptions = {
      entry,
      outDir: opts.outDir,
      verbose: opts.verbose,
      keepC: true,
    };

    const driver = new CompilerDriver();
    const result = await driver.compile(options);

    if (result.diagnostics.length > 0) {
      console.log("\nDiagnostics:");
      for (const diag of result.diagnostics) {
        console.log(`  ${diag}`);
      }
    }

    if (opts.verbose && result.verbose.length > 0) {
      console.log("\nVerbose:");
      for (const msg of result.verbose) {
        console.log(`  ${msg}`);
      }
    }

    if (opts.verbose) {
      for (const file of result.files) {
        console.log(`\n--- ${file.path} ---`);
        console.log(file.content);
      }
    }

    if (result.success) {
      console.log(`\n✓ Transpiled ${result.files.length} files`);
    } else {
      console.log(`\n✗ Transpilation failed`);
      process.exit(1);
    }
  });

// parseAsync so the async compile/transpile actions are awaited.
// Argv mode is auto-detected (node vs native binary) inside Command.
await program.parseAsync();
