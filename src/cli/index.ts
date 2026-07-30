#!/usr/bin/env node
import { Command } from "./commander/index.js";
import * as path from "path";
import { fileURLToPath } from "url";
import { CompilerDriver, type CompilerOptions } from "../driver/compiler.js";
import { init } from "./init.js";
import { run, listScripts } from "./run.js";
import { findAppJson, resolveBuildOptions } from "./app-json.js";

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
  .description("Compile TypeScript to native executable or shared library")
  .argument("<entry>", "Entry TypeScript file")
  .option("-o, --output <name>", "Output executable name", "output")
  .option("-d, --out-dir <dir>", "Output directory for .c/.h files", "./out")
  .option("-t, --target <platform>", "Target platform (windows|linux)", process.platform === "win32" ? "windows" : "linux")
  .option("--shared", "Compile as shared library (.dll/.so) instead of executable")
  .option("--no-runtime", "Exclude runtime library")
  .option("-v, --verbose", "Print intermediate C code")
  .option("--keep-c", "Keep generated .c/.h files")
  .option("--clang-args <args>", "Extra args to pass to clang")
  .option("--zip", "Create a zip archive of the build output after linking")
  .action(async (entry: string, opts: any) => {
    // Load app.json build metadata (name / icon / author / …) when present
    const entryDir = path.dirname(path.resolve(entry));
    const found = findAppJson(entryDir) || findAppJson(process.cwd());
    let appBuild: CompilerOptions["appBuild"];
    let output = opts.output as string | undefined;
    if (found) {
      const targetPlat =
        opts.target === "windows" ? "win32" : opts.target === "linux" ? "linux" : process.platform;
      const resolved = resolveBuildOptions(found.app, targetPlat);
      appBuild = {
        name: resolved.name,
        productName: resolved.productName,
        // Always a string path (or undefined) after resolveIconPath
        icon: typeof resolved.icon === "string" ? resolved.icon : undefined,
        description: resolved.description,
        author: resolved.author,
        company: resolved.company,
        copyright: resolved.copyright,
        version: resolved.version,
        appDir: found.dir,
        assetsDir: resolved.assetsDir,
        gui: resolved.gui,
      };
      // Default -o to productName when user left the CLI default
      if (!opts.output || opts.output === "output") {
        output = resolved.productName;
      }
    }

    const options: CompilerOptions = {
      entry,
      output,
      outDir: opts.outDir,
      target: opts.target,
      shared: opts.shared,
      runtime: opts.runtime,
      verbose: opts.verbose,
      keepC: opts.keepC,
      clangArgs: opts.clangArgs?.split(" "),
      projectRoot: PROJECT_ROOT,
      appBuild,
      zip: opts.zip,
    };

    const driver = new CompilerDriver();

    console.log(`mini-tsc: compiling ${entry}...`);
    if (appBuild?.name) {
      console.log(`  app: ${appBuild.name}${appBuild.version ? ` v${appBuild.version}` : ""}`);
    }

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
      if (result.zipPath) {
        console.log(`  ${path.resolve(result.zipPath)}`);
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

program
  .command("init")
  .description("Initialize a new mini-tsc project")
  .argument("<project-name>", "Project name (letters, underscores, and numbers only)")
  .action(async (projectName: string) => {
    await init(projectName, { projectRoot: process.cwd() });
  });

program
  .command("run")
  .description("Run a script defined in app.json")
  .argument("[script]", "Script name to run (omit to list all scripts)")
  .action(async (...args: any[]) => {
    // args: [script?, opts, command] when script is provided
    // args: [opts, command] when script is omitted
    const script = args.length >= 3 ? args[0] : undefined;
    if (script) {
      await run(script, { cwd: process.cwd() });
    } else {
      await listScripts({ cwd: process.cwd() });
    }
  });

// parseAsync so the async compile/transpile actions are awaited.
// Argv mode is auto-detected (node vs native binary) inside Command.
await program.parseAsync();
