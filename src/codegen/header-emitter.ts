import type { ExportEntry } from "../modules/module-resolver.js";
import type { CNode } from "./c-emitter.js";
import { SymbolMangler } from "../modules/symbol-mangler.js";
import { sanitizeCIdentifier } from "./expression-emitter.js";

export class HeaderEmitter {
  private mangler = new SymbolMangler();

  emitHeader(
    moduleName: string,
    exports: ExportEntry[],
    typeDefinitions: CNode[],
    functionDeclarations?: CNode[],
  ): string {
    const guard = `${moduleName.toUpperCase().replace(/[^A-Z0-9]/g, "_")}_H`;
    const lines: string[] = [];
    const neededTypeHeaders = new Set<string>();
    const typeToHeader: Record<string, string> = {
      "Command": "src_cli_commander_command.h",
      "Option": "src_cli_commander_option.h",
      "Argument": "src_cli_commander_argument.h",
      "CommanderError": "src_cli_commander_error.h",
      "InvalidArgumentError": "src_cli_commander_error.h",
    };

    const collectTypeRefs = (typeStr: string | undefined): void => {
      if (!typeStr) return;
      for (const [typeName, header] of Object.entries(typeToHeader)) {
        if (typeStr.includes(typeName)) {
          neededTypeHeaders.add(header);
        }
      }
    };

    // Scan function declarations for type references (before emitting)
    if (functionDeclarations) {
      for (const node of functionDeclarations) {
        if (node.kind === "function_decl") {
          for (const p of node.params || []) collectTypeRefs(p.type);
          collectTypeRefs(node.returnType);
        }
      }
    }

    lines.push(`#ifndef ${guard}`);
    lines.push(`#define ${guard}`);
    lines.push('');
    lines.push('#include "runtime.h"');

    // Add type header includes
    if (neededTypeHeaders.size > 0) {
      for (const h of neededTypeHeaders) {
        lines.push(`#include "${h}"`);
      }
    }
    lines.push('');

    // Type definitions visible to other modules
    for (const typeDef of typeDefinitions) {
      if (typeDef.kind === "struct_decl") {
        lines.push(`typedef struct ${typeDef.name} ${typeDef.name};`);
        lines.push(`struct ${typeDef.name} {`);
        const hasNameField = (typeDef.fields || []).some((f: any) => f.name === "name");
        const isLikelyError = typeDef.name.includes("Error") || typeDef.name.includes("Exception");
        if (isLikelyError && !hasNameField) {
          lines.push(`  TSString* name;`);
        }
        for (const field of typeDef.fields || []) {
          const cName = sanitizeCIdentifier(field.name);
          const fpMatch = (field.type || "").match(/^(.+)\s*\(\*\)\s*\(([^)]*)\)$/);
          if (fpMatch) {
            const [, returnType, paramList] = fpMatch;
            lines.push(`  ${returnType} (*${cName})(${paramList || ""});`);
          } else {
            lines.push(`  ${field.type} ${cName};`);
          }
        }
        lines.push(`};`);
        lines.push('');
      } else if (typeDef.kind === "enum_decl") {
        const prefix = typeDef.isConst ? "const " : "";
        const values = (typeDef.values || [])
          .map((v: any) => v.value !== undefined ? `  ${typeDef.name}_${v.name} = ${v.value}` : `  ${typeDef.name}_${v.name}`)
          .join(',\n');
        lines.push(`${prefix}enum ${typeDef.name} {`);
        lines.push(values);
        lines.push(`};`);
        lines.push('');
      } else if (typeDef.kind === "union_decl") {
        lines.push(`struct ${typeDef.name} {`);
        lines.push(`  int tag;`);
        for (const variant of typeDef.variants || []) {
          lines.push(`  ${variant.type} as_${variant.name};`);
        }
        lines.push(`};`);
        lines.push('');
      } else if (typeDef.kind === "typedef") {
        const original = typeDef.originalType as string;
        const alias = typeDef.alias as string;
        const fpMatch = original.match(/^(.+)\s*\(\*\)\s*\(([^)]*)\)$/);
        if (fpMatch) {
          const [, returnType, paramList] = fpMatch;
          lines.push(`typedef ${returnType} (*${alias})(${paramList || ""});`);
        } else {
          lines.push(`typedef ${original} ${alias};`);
        }
        lines.push('');
      }
    }

    // Module init declaration
    const initFn = this.mangler.mangleInit(moduleName);
    lines.push(`void ${initFn}(void);`);

    // Exported declarations
    for (const exp of exports) {
      if (!exp.isType && exp.name !== "entry") {
        if (exp.isConstant) {
          const varType = exp.returnType || "Value";
          lines.push(`extern ${varType} ${exp.mangledName};`);
        } else if (exp.paramTypes && exp.paramTypes.length > 0) {
          lines.push(`extern ${exp.returnType || "Value"} ${exp.mangledName}(${exp.paramTypes.join(", ")});`);
        } else if (exp.returnType && exp.returnType !== "Value") {
          lines.push(`extern ${exp.returnType} ${exp.mangledName}(void);`);
        }
      }
    }

    // Function forward declarations (for cross-module calls)
    if (functionDeclarations) {
      const emittedSignatures = new Set<string>();
      for (const node of functionDeclarations) {
        if (node.kind === "function_decl" && node.name) {
          const funcName = node.name;
          if (funcName.endsWith("_destructor") || funcName.startsWith("__closure_")) continue;
          if (funcName === "entry") continue;

          const paramStr = (node.params || [])
            .map((p: any) => {
              const type = p.type || "Value";
              const name = p.name || "";
              const fpMatch = type.match(/^(.+)\s*\(\*\)\s*\(([^)]*)\)$/);
              if (fpMatch) {
                return `${fpMatch[1]} (*${name})(${fpMatch[2] || ""})`;
              }
              return `${type} ${name}`;
            })
            .join(", ");
          const sig = `${node.returnType || "void"} ${funcName}(${paramStr || "void"})`;
          if (!emittedSignatures.has(funcName)) {
            emittedSignatures.add(funcName);
            lines.push(`${sig};`);
          }
        }
      }
    }

    lines.push('');
    lines.push(`#endif /* ${guard} */`);

    return lines.join('\n');
  }
}
