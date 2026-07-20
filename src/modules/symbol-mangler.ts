import * as path from "path";

/** Reserved C keywords that must be escaped */
const C_RESERVED = new Set([
  "auto", "break", "case", "char", "const", "continue", "default", "do",
  "double", "else", "enum", "extern", "float", "for", "goto", "if",
  "inline", "int", "long", "register", "restrict", "return", "short",
  "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
  "unsigned", "void", "volatile", "while",
]);

function escapeName(name: string): string {
  if (C_RESERVED.has(name)) return `$${name}`;
  // Replace ALL non-alphanumeric characters (except underscore) with underscore
  // This includes hyphens, dots, colons, etc.
  return name.replace(/[^a-zA-Z0-9_]/g, "_");
}

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

function filePathToPrefix(filePath: string): string {
  // Use the same logic as filePathToModuleName for consistency
  return filePathToModuleName(filePath);
}

export class SymbolMangler {
  /**
   * Mangle a named export: utils/math.ts + "add" → "utils_math_add"
   */
  mangle(filePath: string, exportName: string): string {
    return `${filePathToPrefix(filePath)}_${escapeName(exportName)}`;
  }

  /**
   * Mangle a default export: module_name__default
   */
  mangleDefault(filePath: string): string {
    return `${filePathToPrefix(filePath)}__default`;
  }

  /**
   * Mangle a class method: ClassName_methodName
   */
  mangleMethod(className: string, methodName: string): string {
    return `${escapeName(className)}_${escapeName(methodName)}`;
  }

  /**
   * Mangle a class constructor: ClassName_constructor
   */
  mangleConstructor(className: string): string {
    return `${escapeName(className)}_constructor`;
  }

  /**
   * Mangle a class destructor: ClassName_destructor
   */
  mangleDestructor(className: string): string {
    return `${escapeName(className)}_destructor`;
  }

  /**
   * Mangle a re-export: forward the original mangled name
   */
  mangleReExport(sourceFile: string, originalName: string): string {
    return this.mangle(sourceFile, originalName);
  }

  /**
   * Mangle a generic specialization: fnName_Type1_Type2
   */
  mangleGeneric(baseName: string, typeSuffixes: string[]): string {
    const suffix = typeSuffixes.map(s => escapeName(s)).join("_");
    return `${baseName}_${suffix}`;
  }

  /**
   * Mangle a private field: _ClassName_fieldName
   */
  manglePrivate(className: string, fieldName: string): string {
    return `_${escapeName(className)}_${escapeName(fieldName)}`;
  }

  /**
   * Mangle a static field: ClassName_statField
   */
  mangleStatic(className: string, fieldName: string): string {
    return `${escapeName(className)}_static_${escapeName(fieldName)}`;
  }

  /**
   * Module init function name
   */
  mangleInit(filePath: string): string {
    return `__init_${filePathToPrefix(filePath)}`;
  }

  /**
   * Module check flag name
   */
  mangleInitFlag(filePath: string): string {
    return `__init_done_${filePathToPrefix(filePath)}`;
  }
}
