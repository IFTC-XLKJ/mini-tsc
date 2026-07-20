import type { ModuleGraph } from "./module-resolver.js";
import { SymbolMangler } from "./symbol-mangler.js";

/** Convert absolute path to relative path from project root, then mangle for C */
function filePathToModuleName(filePath: string): string {
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

export class ModuleInitializer {
  private mangler = new SymbolMangler();

  /**
   * Generate initialization code for a DAG module (no cycles).
   * Each module gets a __init_<name>() function that runs once.
   */
  generateDagInit(graph: ModuleGraph, filePath: string): string {
    const moduleName = filePathToModuleName(filePath);
    const initFn = this.mangler.mangleInit(moduleName);
    const flagName = this.mangler.mangleInitFlag(moduleName);

    const moduleNode = graph.nodes.get(filePath);
    if (!moduleNode) return "";

    // Dependencies that need initialization first
    const deps = moduleNode.imports
      .filter(i => graph.nodes.has(i) && !graph.cycles.flat().includes(i))
      .map(i => `  ${this.mangler.mangleInit(filePathToModuleName(i))}();`)
      .join("\n");

    return `static int ${flagName} = 0;

void ${initFn}(void) {
  if (${flagName}) return;
  ${flagName} = 1;
${deps ? deps + "\n" : ""}  // Module-level code goes here
}`;
  }

  /**
   * Generate lazy initialization for cyclic modules.
   * Uses function pointers and init-once pattern.
   */
  generateLazyInit(cycleGroup: string[]): string {
    const lines: string[] = [];

    for (const filePath of cycleGroup) {
      const moduleName = filePathToModuleName(filePath);
      const initFn = this.mangler.mangleInit(moduleName);
      const flagName = this.mangler.mangleInitFlag(moduleName);

      lines.push(`static int ${flagName} = 0;`);
      lines.push(`extern void ${initFn}_impl(void);`);
      lines.push('');
      lines.push(`void ${initFn}(void) {`);
      lines.push(`  if (${flagName}) return;`);
      lines.push(`  ${flagName} = 1;`);
      lines.push(`  ${initFn}_impl();`);
      lines.push(`}`);
    }

    return lines.join("\n");
  }

  /**
   * Generate the top-level init call sequence (topological order).
   */
  generateInitSequence(graph: ModuleGraph): string {
    const lines: string[] = [];

    // Include module headers
    for (const filePath of graph.sortedOrder) {
      const moduleName = filePathToModuleName(filePath);
      lines.push(`#include "${moduleName}.h"`);
    }
    lines.push('');

    // Declare init functions
    for (const filePath of graph.sortedOrder) {
      const moduleName = filePathToModuleName(filePath);
      const initFn = this.mangler.mangleInit(moduleName);
      lines.push(`extern void ${initFn}(void);`);
    }
    lines.push('');

    // Generate init calls
    const initCalls: string[] = [];
    for (const filePath of graph.sortedOrder) {
      const isCycle = graph.cycles.flat().includes(filePath);
      const moduleName = filePathToModuleName(filePath);
      const initFn = this.mangler.mangleInit(moduleName);

      if (!isCycle) {
        initCalls.push(`  ${initFn}();`);
      } else {
        // Cycle: use lazy init (called on first access)
        initCalls.push(`  // ${moduleName} uses lazy init (cyclic dependency)`);
      }
    }

    lines.push('void __init_all_modules(void) {');
    lines.push(initCalls.join('\n'));
    lines.push('}');

    return lines.join('\n');
  }
}
