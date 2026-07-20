import type { CNode } from "./c-emitter.js";
import { sanitizeCIdentifier } from "./expression-emitter.js";

export class TypeEmitter {
  emitTypeDefinition(node: CNode): string {
    switch (node.kind) {
      case "struct_decl":
        return this.emitStruct(node);
      case "vtable_decl":
        return this.emitVTable(node);
      case "enum_decl":
        return this.emitEnum(node);
      case "union_decl":
        return this.emitUnion(node);
      case "typedef":
        return this.emitTypedef(node);
      default:
        return `/* unknown type: ${node.kind} */`;
    }
  }

  private emitField(type: string, name: string): string {
    const cName = sanitizeCIdentifier(name);
    const fpMatch = type.match(/^(.+)\s*\(\*\)\s*\(([^)]*)\)$/);
    if (fpMatch) {
      const [, returnType, paramList] = fpMatch;
      return `  ${returnType} (*${cName})(${paramList || ""});`;
    }
    return `  ${type} ${cName};`;
  }

  private emitStruct(node: CNode): string {
    const fields = node.fields
      .map((f: any) => this.emitField(f.type, f.name))
      .join('\n');
    return `struct ${node.name} {\n${fields}\n};`;
  }

  private emitVTable(node: CNode): string {
    const methods = node.methods || [];
    // Generate function pointer typedef for each method
    const lines: string[] = [];
    for (const m of methods) {
      const params = ["void* self"];
      if (m.paramTypes && m.paramTypes.length > 0) {
        params.push(...m.paramTypes);
      }
      const cName = sanitizeCIdentifier(m.name);
      lines.push(`typedef ${m.returnType} (*${node.name}_${cName}_fn)(${params.join(", ")});`);
    }
    // Generate the struct with function pointer fields
    lines.push(`struct ${node.name} {`);
    for (const m of methods) {
      const cName = sanitizeCIdentifier(m.name);
      lines.push(`  ${node.name}_${cName}_fn ${cName};`);
    }
    lines.push(`};`);
    return lines.join('\n');
  }

  private emitEnum(node: CNode): string {
    const values = node.values
      .map((v: any) => v.value !== undefined ? `${v.name} = ${v.value}` : v.name)
      .join(',\n  ');
    const prefix = node.isConst ? "const " : "";
    return `${prefix}enum ${node.name} {\n  ${values}\n};`;
  }

  private emitUnion(node: CNode): string {
    const variants = node.variants
      .map((v: any) => `  ${v.type} as_${v.name};`)
      .join('\n');
    const tagField = `  int tag;\n`;
    return `struct ${node.name} {\n${tagField}${variants}\n};`;
  }

  private emitTypedef(node: CNode): string {
    const original = node.originalType as string;
    const alias = node.alias as string;
    const fpMatch = original.match(/^(.+)\s*\(\*\)\s*\(([^)]*)\)$/);
    if (fpMatch) {
      const [, returnType, paramList] = fpMatch;
      return `typedef ${returnType} (*${alias})(${paramList || ""});`;
    }
    return `typedef ${original} ${alias};`;
  }

  emitFunctionPtr(name: string, params: string[], returnType: string): string {
    const paramStr = params.length > 0 ? params.join(", ") : "void";
    return `typedef ${returnType} (*${name})(${paramStr});`;
  }
}
