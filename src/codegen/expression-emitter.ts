import type { CNode, ImportedSymbolInfo } from "./c-emitter.js";

/** C/stdio macros that cannot be used as identifiers */
const C_RESERVED_IDS = new Set([
  "auto", "break", "case", "char", "const", "continue", "default", "do", "double",
  "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long",
  "register", "restrict", "return", "short", "signed", "static", "struct",
  "switch", "typedef", "union", "unsigned", "void", "volatile", "while",
  "stdin", "stdout", "stderr", "errno", "FILE", "EOF",
  "BUFSIZ", "NULL", "true", "false", "bool", "main",
  "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
  "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local",
]);

export function sanitizeCIdentifier(name: string): string {
  if (!name) return name;
  if (C_RESERVED_IDS.has(name)) return `${name}_`;
  // Avoid leading digits and non-C identifier chars
  let s = name.replace(/[^a-zA-Z0-9_]/g, "_");
  if (/^[0-9]/.test(s)) s = `_${s}`;
  return s;
}

/** Convert absolute file path to a mangled C module prefix (matches SymbolMangler logic) */
function filePathToMangledPrefix(filePath: string): string {
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
  return relative
    .replace(/\.(ts|tsx|js|jsx)$/, "")
    .replace(/[/\\]/g, "_")
    .replace(/[^a-zA-Z0-9_]/g, "_");
}

export class ExpressionEmitter {
  /** Map of variable names to their C types */
  private varTypes: Map<string, string> = new Map();
  /** Map of original names to mangled names for imported symbols */
  private importedSymbols: Map<string, string> = new Map();
  /** Tracks the last builtin module function call for special argument handling */
  private _lastBuiltinCall: string | null = null;
  /** Map of namespace import names to their module file paths (for `import * as X from "..."`) */
  private namespaceModulePaths: Map<string, string> = new Map();

  /** Register a variable and its type */
  declareVar(name: string, type: string): void {
    this.varTypes.set(name, type);
    // Also register sanitized form for lookups after rename
    const san = sanitizeCIdentifier(name);
    if (san !== name) this.varTypes.set(san, type);
  }

  /** Get the type of a variable */
  getVarType(name: string): string | undefined {
    return this.varTypes.get(name);
  }

  /** Register an imported symbol mapping */
  declareImport(originalName: string, mangledName: string): void {
    this.importedSymbols.set(originalName, mangledName);
  }

  /** Register imported symbols from a map */
  setImportedSymbols(symbols: Map<string, ImportedSymbolInfo>): void {
    for (const [original, info] of symbols) {
      this.importedSymbols.set(original, info.mangledName);
      // Register constants in varTypes for correct wrapping
      if (info.isConstant) {
        // Use the actual return type from the imported symbol info
        const varType = info.returnType || "double";
        this.varTypes.set(info.mangledName, varType);
        // Also register original name with the same type
        this.varTypes.set(original, varType);
      }
    }
  }

  /** Register a namespace import's module path (for `import * as X from "..."`) */
  setNamespaceModulePath(namespaceName: string, modulePath: string): void {
    this.namespaceModulePaths.set(namespaceName, modulePath);
  }

  emit(node: CNode): string {
    switch (node.kind) {
      case "identifier":
        // CommonJS module globals (set in main.c / module init)
        if (node.name === "__dirname") {
          return `ts_value_string(ts_string_new(__ts_dirname))`;
        }
        if (node.name === "__filename") {
          return `ts_value_string(ts_string_new(__ts_filename))`;
        }
        // Check if this is an imported symbol that needs mangled name
        {
          const imported = this.importedSymbols.get(node.name);
          if (imported) return imported;
          return sanitizeCIdentifier(node.name);
        }
      case "number_literal":
        return String(node.value);
      case "string_literal":
        return `ts_string_new("${this.escapeString(node.value)}")`;
      case "boolean_literal":
        return node.value ? "1" : "0";
      case "null_literal":
        return "ts_value_null()";
      case "undefined_literal":
        return "ts_value_undefined()";
      case "binary_expression":
        return this.emitBinary(node);
      case "unary_expression":
        return this.emitUnary(node);
      case "call_expression":
        return this.emitCall(node);
      case "property_access":
        return this.emitPropertyAccess(node);
      case "element_access":
        return this.emitElementAccess(node);
      case "cast_expression":
        return this.emitCast(node);
      case "new_expression":
        return this.emitNew(node);
      case "array_literal":
        return this.emitArrayLiteral(node);
      case "object_literal":
        return this.emitObjectLiteral(node);
      case "arrow_function":
        return this.emitArrowFunction(node);
      case "function_expression":
        return this.emitFunctionExpression(node);
      case "function_ref": {
        // Bind free vars as a per-call snapshot (BoundFn) so loop captures are correct
        const caps: { outerName: string; captureName: string; type?: string }[] =
          node.captures || [];
        if (caps.length === 0) {
          return `ts_value_function((void*)${node.name})`;
        }
        const wrapCap = (outer: string): string => {
          const t = this.varTypes.get(outer);
          if (t === "Value" || t === "any" || t === "unknown" || !t) return outer;
          if (t === "double" || t === "number" || t === "int") return `ts_value_number(${outer})`;
          if (t === "boolean") return `ts_value_boolean(${outer})`;
          if (t === "TSString*" || t === "string") return `ts_value_string(${outer})`;
          if (t === "TSArray*") return `ts_value_array(${outer})`;
          if (t.endsWith("*") && !t.startsWith("TS")) return `ts_value_object((void*)${outer})`;
          return outer;
        };
        const capExprs = caps.map(c => wrapCap(c.outerName));
        return `ts_bind_function((void*)${node.name}, (Value[]){${capExprs.join(", ")}}, ${caps.length})`;
      }
      case "conditional_expression":
        return this.emitConditional(node);
      case "template_expression":
        return this.emitTemplate(node);
      case "parenthesized":
        return `(${this.emit(node.expression)})`;
      case "assignment":
        return this.emitAssignment(node);
      case "await_expression":
        return `ts_await(${this.emit(node.expression)})`;
      default:
        return `/* unsupported expr: ${node.kind} */`;
    }
  }

  private emitAssignment(node: CNode): string {
    const op = node.operator || "=";
    // Element assignment: obj[key] = value → ts_hashmap_set / ts_array_set
    if (op === "=" && node.target?.kind === "element_access") {
      const obj = this.emit(node.target.object);
      const key = this.emit(node.target.index);
      let val = this.emit(node.value);
      if (!val.startsWith("ts_value_") && !val.startsWith("ts_null(") && !val.startsWith("ts_undefined(")) {
        if (node.value?.kind === "string_literal") val = `ts_value_string(${val})`;
        else if (node.value?.kind === "number_literal") val = `ts_value_number(${val})`;
        else if (node.value?.kind === "boolean_literal") val = `ts_value_boolean(${val})`;
        else if (node.value?.kind === "identifier") {
          const t = this.varTypes.get(node.value.name);
          if (t === "TSString*" || t === "string") val = `ts_value_string(${val})`;
          else if (t === "double" || t === "number") val = `ts_value_number(${val})`;
          else if (t === "int" || t === "boolean") val = `ts_value_boolean(${val})`;
          else if (t === "Value") { /* keep */ }
          else if (t && t.endsWith("*") && !t.startsWith("TS")) val = `ts_value_object((void*)${val})`;
        } else if (val.startsWith("ts_string_") || val.startsWith("ts_to_string(") || val.includes("/*__ts_str*/")) {
          val = `ts_value_string(${val})`;
        }
      }
      // Key as TSString* for hashmap (string keys on Value objects)
      let keyStr = key;
      if (key.startsWith("ts_value_string(")) keyStr = key.replace(/^ts_value_string\((.+)\)$/, "$1");
      else if (key.startsWith("ts_to_string(")) {
        // strip if wrapping TSString*
        keyStr = key;
      } else if (!/^\d+$/.test(key) && !key.startsWith("ts_string_")) {
        const idxType = node.target.index?.kind === "identifier"
          ? this.varTypes.get(node.target.index.name) : undefined;
        if (idxType === "TSString*" || idxType === "string" || node.target.index?.kind === "string_literal") {
          keyStr = key;
        } else if (idxType === "Value") {
          keyStr = `ts_to_string(${key})`;
        } else if (idxType === "double" || idxType === "number" || idxType === "int") {
          // numeric — array path below
          return `ts_array_set(((TSArray*)${obj}.as.object), (int32_t)(${key}), ${val})`;
        } else {
          // default string key
          keyStr = key;
        }
      }
      // Prefer hashmap for Value objects / non-numeric keys
      if (!/^\d+$/.test(key) && (node.target.index?.kind !== "number_literal")) {
        const mapExpr = `((TSHashMap*)${obj}.as.object)`;
        return `ts_hashmap_set(${mapExpr}, ${keyStr}, ${val})`;
      }
      // Numeric index → array set
      return `ts_array_set(((TSArray*)${obj}.as.object), (int32_t)(${key}), ${val})`;
    }
    // Nested property assignment on Value: obj.prop = x → ts_hashmap_set(...)
    // (hashmap get is not an lvalue)
    if (op === "=" && node.target?.kind === "property_access") {
      const tgt = node.target;
      const objName = tgt.object?.kind === "identifier" ? tgt.object.name : null;
      const objType = objName ? this.varTypes.get(objName) : undefined;
      const objIsValue =
        objType === "Value" ||
        tgt.objectType === "any" || tgt.objectType === "map" || tgt.objectType === "Value" ||
        (tgt.object?.kind === "property_access"); // nested Value
      // Don't rewrite struct field assigns (self->field)
      const isStructField =
        objName && objType && objType.endsWith("*") && !objType.startsWith("TS");
      // WebSocket / WebSocketServer event handlers: ws.onmessage = fn
      const wsHandlers = new Set(["onopen", "onmessage", "onerror", "onclose"]);
      if (wsHandlers.has(tgt.property) && (objIsValue || /ws|socket|wss/i.test(objName || ""))) {
        const object = this.emit(tgt.object);
        let val = this.emit(node.value);
        // ts_bind_function / ts_value_* already produce Value
        if (!val.startsWith("ts_value_") && !val.startsWith("ts_null(") &&
            !val.startsWith("ts_undefined(") && !val.startsWith("ts_bind_function(")) {
          if (node.value?.kind === "identifier") {
            const t = this.varTypes.get(node.value.name);
            if (t === "Value") { /* keep */ }
            else val = `ts_value_function((void*)${val})`;
          } else if (node.value?.kind === "arrow_function" || node.value?.kind === "function_expression" ||
                     node.value?.kind === "function_ref") {
            val = `ts_value_function((void*)${val})`;
          } else {
            val = `ts_value_function((void*)${val})`;
          }
        }
        return `ts_websocket_set_handler(${object}, ts_string_new("${tgt.property}"), ${val})`;
      }
      if (objIsValue && !isStructField) {
        const object = this.emit(tgt.object);
        let val = this.emit(node.value);
        // Wrap value into Value
        if (!val.startsWith("ts_value_") && !val.startsWith("ts_null(") && !val.startsWith("ts_undefined(")) {
          if (node.value?.kind === "string_literal") val = `ts_value_string(${val})`;
          else if (node.value?.kind === "number_literal") val = `ts_value_number(${val})`;
          else if (node.value?.kind === "boolean_literal") val = `ts_value_boolean(${val})`;
          else if (node.value?.kind === "identifier") {
            const t = this.varTypes.get(node.value.name);
            if (t === "TSString*" || t === "string") val = `ts_value_string(${val})`;
            else if (t === "double" || t === "number") val = `ts_value_number(${val})`;
            else if (t === "int" || t === "boolean") val = `ts_value_boolean(${val})`;
            else if (t && t.includes("(*)")) val = `ts_value_function((void*)${val})`;
          } else if (val.startsWith("ts_string_new(") || val.startsWith("ts_to_string(") ||
                     val.startsWith("ts_string_concat(") || val.includes("/*__ts_str*/")) {
            val = `ts_value_string(${val})`;
          } else if (val.startsWith("ts_hashmap_get(")) {
            // already Value
          } else {
            // function-typed property values
            val = `ts_value_function((void*)${val})`;
          }
        }
        // object may be self->_outputConfig (Value field) — always cast via .as.object when Value
        let mapExpr: string;
        if (object.startsWith("ts_hashmap_get(") || object.startsWith("ts_value_")) {
          mapExpr = `((TSHashMap*)${object}.as.object)`;
        } else if (objType === "Value" || object.includes("->") || object.includes(".")) {
          // Struct field of type Value, or nested access
          mapExpr = `((TSHashMap*)${object}.as.object)`;
        } else {
          mapExpr = `((TSHashMap*)${object}.as.object)`;
        }
        return `ts_hashmap_set(${mapExpr}, ts_string_new("${tgt.property}"), ${val})`;
      }
    }

    const target = this.emit(node.target);
    let value = this.emit(node.value);

    // String += operator: str += x → str = ts_string_concat(str, x)
    if (op === "+=" && node.target) {
      const targetType = this.resolveTargetType(node.target);
      if (targetType === "TSString*" || targetType === "string") {
        const rightStr = value.startsWith("ts_string_new(") || value.startsWith("ts_string_concat(") ||
          value.startsWith("ts_to_string(") || value.startsWith("ts_number_to_string(") ||
          value.includes("/*__ts_str*/")
          ? value : `ts_to_string(${value})`;
        return `${target} = ts_string_concat(${target}, ${rightStr})`;
      }
    }

    // Coerce Value (hashmap get / fetch helpers) into the target's C type
    if (op === "=") {
      const t = this.resolveTargetType(node.target);
      if (t) {
        if (t === "TSArray*") {
          if (value.startsWith("ts_value_array(")) {
            value = value.replace(/^ts_value_array\((.+)\)$/, "$1");
          } else if (value.startsWith("ts_hashmap_get(") || value.startsWith("ts_value_")) {
            value = `((TSArray*)${value}.as.object)`;
          }
          // spread / array_from already handled via ts_value_array unwrap
        } else if (t === "TSString*" || t === "string") {
          if (value.startsWith("ts_value_string(")) {
            value = value.replace(/^ts_value_string\((.+)\)$/, "$1");
          } else if (value.startsWith("ts_hashmap_get(") ||
                     (value.startsWith("ts_value_") && !value.includes("/*__ts_str*/"))) {
            if (!value.includes("/*__ts_str*/") && !value.startsWith("ts_string_") &&
                !value.startsWith("ts_to_string(")) {
              value = `ts_to_string(${value})`;
            }
          }
        } else if (t === "int" || t === "boolean") {
          if (value.startsWith("ts_hashmap_get(") || value.startsWith("ts_value_") ||
              value.startsWith("ts_fetch_")) {
            value = `ts_to_boolean(${value})`;
          }
        } else if (t === "double" || t === "number") {
          if (value.startsWith("ts_hashmap_get(") || value.startsWith("ts_value_")) {
            value = `ts_to_number(${value})`;
          }
        } else if (t === "Value") {
          // Wrap concrete pointers into Value when assigning to Value fields
          if (value.startsWith("ts_string_new(") || value.startsWith("ts_string_concat(") ||
              value.startsWith("ts_to_string(") || value.includes("/*__ts_str*/")) {
            value = `ts_value_string(${value})`;
          } else if (value.startsWith("ts_array_new(") || value.startsWith("ts_array_from_") ||
                     value.startsWith("ts_string_split(") || value.startsWith("ts_array_filter(") ||
                     value.startsWith("ts_array_map(")) {
            value = `ts_value_array(${value})`;
          }
        }
      }
    }
    return `${target} ${op} ${value}`;
  }

  /** Resolve the C type of an assignment target (identifier, struct field, etc.) */
  private resolveTargetType(target: CNode): string | undefined {
    if (target.kind === "identifier") {
      return this.varTypes.get(target.name);
    }
    // struct field access: self->field or obj.field
    if (target.kind === "property_access") {
      // Prefer visitor-resolved C type when present (e.g. Point.x → double)
      if (target.propertyCType &&
          !target.propertyCType.includes("(*)") &&
          !target.propertyCType.includes("<") &&
          target.propertyCType !== "any") {
        return target.propertyCType;
      }
      // Try to determine the field type from the object type
      const objName = target.object?.kind === "identifier" ? target.object.name : undefined;
      if (objName) {
        const objType = this.varTypes.get(objName);
        if (objType && objType.endsWith("*") && !objType.startsWith("TS")) {
          // Known struct field types
          const fieldName = target.property;
          if (fieldName === "short_" || fieldName === "short" || fieldName === "long_" ||
              fieldName === "long" || fieldName === "flags" ||
              fieldName === "description" || fieldName === "name" || fieldName === "_name" ||
              fieldName === "code" || fieldName === "message" || fieldName === "nestedError" ||
              fieldName === "defaultValueDescription" || fieldName === "envVar" ||
              fieldName === "_version" || fieldName === "_versionFlags" || fieldName === "_versionDescription" ||
              fieldName === "_description" || fieldName === "_versionFlags" || fieldName === "attr") {
            return "TSString*";
          }
          if (fieldName === "required" || fieldName === "optional" || fieldName === "variadic" ||
              fieldName === "negate" || fieldName === "mandatory" || fieldName === "hidden" ||
              fieldName === "_hidden" || fieldName === "_defaultCommand" || fieldName === "_exitOverride" ||
              fieldName === "_allowUnknownOption" || fieldName === "_allowExcessArguments" ||
              fieldName === "_helpEnabled" || fieldName === "isDefault") {
            return "int";
          }
          if (fieldName === "defaultValue" || fieldName === "presetArg" || fieldName === "_outputConfig" ||
              fieldName === "_opts" || fieldName === "_implies" || fieldName === "optionValues") {
            return "Value";
          }
          if (fieldName === "exitCode" || fieldName === "x" || fieldName === "y" ||
              fieldName === "width" || fieldName === "height" || fieldName === "radius" ||
              fieldName === "value" || fieldName === "score" || fieldName === "count") {
            return "double";
          }
          if (fieldName === "options" || fieldName === "commands" || fieldName === "arguments" ||
              fieldName === "_aliases" || fieldName === "_preActionHooks" || fieldName === "_postActionHooks" ||
              fieldName === "args" || fieldName === "processedArgs" || fieldName === "argChoices" ||
              fieldName === "_conflicts") {
            return "TSArray*";
          }
          if (fieldName === "short_" || fieldName === "long_") return "TSString*";
        }
      }
      // For property access on Value (ts_hashmap_get results), the target is the hashmap entry
      return undefined;
    }
    return undefined;
  }

  private emitBinary(node: CNode): string {
    const left = this.emit(node.left);
    const right = this.emit(node.right);
    const op = node.operator;

    // String concatenation → ts_string_concat (only when at least one side is a string)
    {
      const leftLooksString =
        node.leftType === "string" ||
        left.startsWith("ts_string_new(") || left.startsWith("ts_string_concat(") ||
        left.startsWith("ts_to_string(") || left.includes("/*__ts_str*/") ||
        /^(Command|Option|Argument)_\w+\(/.test(left) ||
        (node.left?.kind === "identifier" && this.varTypes.get(node.left.name) === "TSString*") ||
        (node.left?.kind === "string_literal") ||
        (node.left?.kind === "template_expression") ||
        (left.startsWith("({") && left.includes("/*__ts_str*/")) ||
        (left.startsWith("({") && left.includes("TSString*"));
      const rightLooksString =
        right.startsWith("ts_string_new(") || right.startsWith("ts_string_concat(") ||
        right.startsWith("ts_to_string(") || right.includes("/*__ts_str*/") ||
        /^(Command|Option|Argument)_\w+\(/.test(right) ||
        (node.right?.kind === "identifier" && this.varTypes.get(node.right.name) === "TSString*") ||
        (node.right?.kind === "string_literal") ||
        (node.right?.kind === "template_expression") ||
        (right.startsWith("({") && right.includes("/*__ts_str*/"));
      // Numeric binary ops (a - b + 2) must NOT become string concat
      const leftIsNumericBinary =
        node.left?.kind === "binary_expression" &&
        ["+", "-", "*", "/", "%"].includes(node.left.operator || "") &&
        node.leftType !== "string" && !leftLooksString;
      if (op === "+" && (leftLooksString || rightLooksString) && !leftIsNumericBinary) {
        let leftStr = this.asTSString(left, node.left);
        let rightStr = this.asTSString(right, node.right);
        const isNumericSide = (n?: CNode, emitted?: string) => {
          if (!n) return false;
          if (n.kind === "number_literal") return true;
          if (n.kind === "identifier") {
            const t = this.varTypes.get(n.name);
            return t === "double" || t === "number" || t === "int";
          }
          // String-producing binary (concat) is never numeric
          if (n.kind === "binary_expression") {
            if (n.operator === "+" && (n.leftType === "string" || n.rightType === "string")) return false;
            if (n.leftType === "string") return false;
            if (["+", "-", "*", "/", "%"].includes(n.operator || "") && n.leftType !== "string") return true;
            return false;
          }
          if (n.kind === "property_access") {
            const t = this.resolveTargetType(n);
            if (t === "double" || t === "number" || t === "int") return true;
            if (t === "TSString*" || t === "string" || t === "TSArray*" || t === "Value") return false;
            // Known string struct fields — never numeric
            const prop = n.property as string;
            if (this.isKnownStringStructField(prop)) return false;
            // Known numeric struct fields only
            if (this.isKnownNumericStructField(prop)) return true;
            // .length is int (string/array/buffer)
            if (prop === "length") return true;
            // Do NOT treat arbitrary ->field as numeric (opt->long_ is TSString*)
            return false;
          }
          if (n.kind === "call_expression") {
            if (emitted?.startsWith("ts_math_")) return true;
            const propCType = n.callee?.kind === "property_access" ? n.callee.propertyCType : undefined;
            if (propCType && /^(double|int|number)\b/.test(propCType)) return true;
          }
          return false;
        };
        if (isNumericSide(node.right, right) && !rightLooksString &&
            !rightStr.startsWith("ts_number_to_string(") &&
            !rightStr.startsWith("ts_string_") && !rightStr.startsWith("ts_to_string(")) {
          rightStr = `ts_number_to_string((double)(${right}))`;
        }
        if (isNumericSide(node.left, left) && !leftLooksString &&
            !leftStr.startsWith("ts_number_to_string(") &&
            !leftStr.startsWith("ts_string_") && !leftStr.startsWith("ts_to_string(")) {
          leftStr = `ts_number_to_string((double)(${left}))`;
        }
        const stripTsToString = (s: string): string => {
          let m = s.match(/^ts_to_string\((\{[\s\S]*\/\*__ts_str\*\/\s*\})\)$/);
          if (m) return m[1];
          m = s.match(/^ts_to_string\(((?:Command|Option|Argument)_\w+\([^)]*\))\)$/);
          if (m) return m[1];
          m = s.match(/^ts_to_string\((ts_json_stringify(?:_indent)?\(.+)\)$/);
          if (m) return m[1];
          if (s.startsWith("ts_to_string(ts_to_string(")) {
            return s.replace(/^ts_to_string\((ts_to_string\(.+\))\)$/, "$1");
          }
          // GNU stmt without outer ts_to_string but nested wrong wrap inside concat
          m = s.match(/^ts_to_string\((.+\/\*__ts_str\*\/.*)\)$/);
          if (m) return m[1];
          return s;
        };
        leftStr = stripTsToString(leftStr);
        rightStr = stripTsToString(rightStr);
        return `ts_string_concat(${leftStr}, ${rightStr})`;
      }
    }

    // String comparison → ts_string_equals (but not when comparing with undefined/null)
    if ((op === "===" || op === "!==") && node.leftType === "string") {
      const rightIsUndef = node.right?.kind === "undefined_literal" || node.right?.kind === "null_literal";
      const leftIsUndef = node.left?.kind === "undefined_literal" || node.left?.kind === "null_literal";
      if (rightIsUndef || leftIsUndef) {
        // Compare with undefined/null → null pointer check
        const ptr = leftIsUndef ? right : left;
        const eq = op === "===" ? "" : "!";
        return `${eq}(${ptr})`;
      }
      const eq = op === "===" ? "" : "!";
      // Headers.get / hashmap get return Value — coerce to TSString*
      let leftStr = left;
      let rightStr = right;
      if (left.startsWith("ts_hashmap_get(") || left.startsWith("ts_value_") ||
          left.startsWith("node_") || left.startsWith("ts_fetch_")) {
        leftStr = `ts_to_string(${left})`;
      }
      if (right.startsWith("ts_hashmap_get(") || right.startsWith("ts_value_") ||
          right.startsWith("node_") || right.startsWith("ts_fetch_")) {
        rightStr = `ts_to_string(${right})`;
      }
      return `${eq}ts_string_equals(${leftStr}, ${rightStr})`;
    }

    // Nullish coalescing — Value-aware
    if (op === "??") {
      if (left.startsWith("ts_hashmap_get(") || left.startsWith("ts_value_") ||
          (node.left?.kind === "identifier" && this.varTypes.get(node.left.name) === "Value") ||
          node.left?.kind === "property_access") {
        // Value ?? fallback — if left is nullish, use right (coerced to match)
        const rightStr = this.coerceLogicalRight(right, node.right);
        return `(ts_to_boolean(${left}) ? ts_to_string(${left}) : ${rightStr})`;
      }
      return `(${left} != NULL ? ${left} : ${right})`;
    }

    // Logical operators
    if (op === "&&") return `${left} && ${right}`;
    if (op === "||") {
      // Scalar int/boolean expressions (startsWith, includes, comparisons) → plain C ||
      const isScalarBoolExpr = (s: string, n?: CNode) =>
        s.startsWith("ts_string_starts_with(") || s.startsWith("ts_string_ends_with(") ||
        s.startsWith("ts_string_includes(") || s.startsWith("ts_string_equals(") ||
        s.startsWith("ts_string_index_of(") || s.startsWith("ts_to_boolean(") ||
        s.startsWith("ts_array_index_of(") || s.startsWith("ts_array_some(") ||
        s.startsWith("ts_array_every(") || s.startsWith("Option_isBoolean(") ||
        (s.includes(" >= 0)") && s.includes("ts_array_index_of")) ||
        n?.kind === "boolean_literal" || n?.kind === "number_literal" ||
        // Only comparison ops are scalar bool — NOT || / && (those may be string-or)
        (n?.kind === "binary_expression" && ["<", ">", "<=", ">=", "==", "===", "!=", "!=="].includes(n.operator || "")) ||
        (n?.kind === "identifier" && (this.varTypes.get(n.name) === "int" || this.varTypes.get(n.name) === "boolean")) ||
        // struct int fields: opt->required, opt->optional, opt->negate
        (n?.kind === "property_access" && this.resolveTargetType(n) === "int") ||
        /->(required|optional|variadic|negate|mandatory|hidden|_hidden|_defaultCommand)\b/.test(s);
      // TSString* || string fallback (long_ || short_ || "")
      const isStrExpr = (s: string, n?: CNode) =>
        s.startsWith("ts_string_new(") || s.startsWith("ts_string_concat(") ||
        s.startsWith("ts_to_string(") || s.includes("/*__ts_str*/") ||
        s.includes("->long_") || s.includes("->short_") || s.includes("->_name") ||
        (n?.kind === "identifier" && (this.varTypes.get(n.name) === "TSString*" || this.varTypes.get(n.name) === "string")) ||
        (n?.kind === "property_access" && this.resolveTargetType(n) === "TSString*") ||
        n?.kind === "string_literal" ||
        /^(Command|Option|Argument)_/.test(s);
      // Prefer scalar bool || before string || (required || optional)
      if (isScalarBoolExpr(left, node.left) || isScalarBoolExpr(right, node.right)) {
        if (isScalarBoolExpr(left, node.left) && isScalarBoolExpr(right, node.right)) {
          return `(${left} || ${right})`;
        }
        // One side scalar, other might be int field too
        if (isScalarBoolExpr(left, node.left) && /->(required|optional|variadic|negate|mandatory|hidden)\b/.test(right)) {
          return `(${left} || ${right})`;
        }
        if (isScalarBoolExpr(right, node.right) && /->(required|optional|variadic|negate|mandatory|hidden)\b/.test(left)) {
          return `(${left} || ${right})`;
        }
        const l = isScalarBoolExpr(left, node.left) ? left : `ts_to_boolean(${left})`;
        const r = isScalarBoolExpr(right, node.right) ? right : `ts_to_boolean(${right})`;
        return `(${l} || ${r})`;
      }
      // Value-producing left (hashmap get, node_*, ts_value_*) must not enter pure string ||
      const leftIsRawValue =
        left.startsWith("ts_hashmap_get(") || left.startsWith("ts_value_") ||
        left.startsWith("ts_array_get(") || left.startsWith("node_") ||
        left.startsWith("ts_fetch_") || left.startsWith("ts_json_parse(") ||
        (node.left?.kind === "identifier" && this.varTypes.get(node.left.name) === "Value") ||
        (node.left?.kind === "property_access" && this.resolveTargetType(node.left) === "Value");
      if (!leftIsRawValue && (isStrExpr(left, node.left) || isStrExpr(right, node.right) ||
          left.includes("/*__ts_str*/") || right.includes("/*__ts_str*/") ||
          left.includes("->long_") || left.includes("->short_") ||
          right.startsWith("ts_string_new("))) {
        // Pure string || string (including chained GNU stmts)
        const leftStr = left.includes("/*__ts_str*/") || left.startsWith("ts_string_") ||
          left.includes("->") || left.startsWith("({") || /^(Command|Option|Argument)_/.test(left)
          ? left : (isStrExpr(left, node.left) ? left : `ts_to_string(${left})`);
        const rightStr = right.includes("/*__ts_str*/") || right.startsWith("ts_string_") ||
          right.includes("->") || right.startsWith("({") || right.startsWith("ts_to_boolean") ||
          /^(Command|Option|Argument)_/.test(right)
          ? (right.startsWith("ts_to_boolean(") ? 'ts_string_new("")' : right)
          : (isStrExpr(right, node.right) ? right : `ts_to_string(${right})`);
        const r = right.startsWith("ts_to_boolean(") ? 'ts_string_new("")' : rightStr;
        return `({ TSString* __or_l = ${leftStr}; TSString* __or_r = (__or_l && __or_l->data && __or_l->length > 0) ? __or_l : ${r}; __or_r; /*__ts_str*/ })`;
      }
      // Value || Value fallback (e.g. reader.read() || { done: true })
      // Unwrap parenthesized: (await reader?.read()) || fallback
      const leftKind = this.unwrapKind(node.left);
      const rightKind = this.unwrapKind(node.right);
      // Helpers that already return TSString* (not Value)
      const isTSStringExpr = (s: string) =>
        s.startsWith("ts_string_new(") || s.startsWith("ts_string_concat(") ||
        s.startsWith("ts_to_string(") || s.startsWith("ts_number_to_string(") ||
        s.startsWith("ts_url_") || s.startsWith("ts_fetch_response_url(") ||
        s.startsWith("ts_fetch_response_statusText(") || s.startsWith("ts_blob_type(") ||
        s.includes("/*__ts_str*/");
      // Value-producing expressions
      const isValueExpr = (s: string) =>
        s.startsWith("ts_hashmap_get(") || s.startsWith("ts_value_") ||
        s.startsWith("ts_fetch_reader_read(") || s.startsWith("ts_fetch_body_get_reader(") ||
        s.startsWith("ts_fetch_response_body(") || s.startsWith("ts_fetch_json(") ||
        s.startsWith("ts_fetch(") || s.startsWith("ts_json_parse(") ||
        s.startsWith("node_");

      // Class methods returning TSString* (Command_getDescription, Option_name, …)
      const isClassStringCall = (s: string) =>
        /^(Command|Option|Argument)_\w+\(/.test(s) ||
        s.startsWith("camelcase(") || s.startsWith("src_cli_commander_") ||
        s.startsWith("formatOptionFlags(") || s.startsWith("ts_array_join(") ||
        s.startsWith("ts_json_stringify");
      const leftIsTSString = isTSStringExpr(left) || isClassStringCall(left);
      const leftIsValue =
        !leftIsTSString && (
          isValueExpr(left) ||
          (leftKind === "identifier" && this.varTypes.get(node.left?.name ?? this.unwrapNode(node.left)?.name) === "Value") ||
          (leftKind === "property_access" && !isClassStringCall(left)) ||
          (leftKind === "call_expression" && !isClassStringCall(left) && !isTSStringExpr(left)) ||
          leftKind === "object_literal"
        );
      if (leftIsTSString || isClassStringCall(left)) {
        const rightStr = isTSStringExpr(right) || rightKind === "string_literal" || isClassStringCall(right)
          ? right
          : this.coerceLogicalRight(right, node.right);
        return `({ TSString* __or_l = ${left}; TSString* __or_r = (__or_l && __or_l->data && __or_l->length > 0) ? __or_l : ${rightStr}; __or_r; /*__ts_str*/ })`;
      }
      if (leftIsValue) {
        // If right is already a Value (object_literal etc.), keep Value-level ||
        if (!isTSStringExpr(right) && !isClassStringCall(right) &&
            (isValueExpr(right) || rightKind === "object_literal" || rightKind === "call_expression")) {
          // GNU C statement expr avoids double-evaluating left (important for reader.read())
          return `({ Value __or_l = ${left}; ts_to_boolean(__or_l) ? __or_l : ${right}; })`;
        }
        // Value || string → TSString* (marker comment so callers don't re-wrap)
        const rightStr = this.coerceLogicalRight(right, node.right);
        return `({ Value __or_l = ${left}; TSString* __or_r = ts_to_boolean(__or_l) ? ts_to_string(__or_l) : ${rightStr}; __or_r; /*__ts_str*/ })`;
      }
      // TSString* || string fallback
      if (rightKind === "string_literal" || isTSStringExpr(right) || isClassStringCall(right)) {
        const rightStr = isTSStringExpr(right) || rightKind === "string_literal" || isClassStringCall(right)
          ? right
          : this.coerceLogicalRight(right, node.right);
        return `({ TSString* __or_l = ${left}; TSString* __or_r = (__or_l && __or_l->data && __or_l->length > 0) ? __or_l : ${rightStr}; __or_r; /*__ts_str*/ })`;
      }
      return `(${left} || ${right})`;
    }

    // `key in object` → ts_hashmap_has
    if (op === "in") {
      let keyStr = left;
      // Prefer bare TSString* keys (attr, Option_attributeName(opt), …)
      if (node.left?.kind === "identifier" && this.varTypes.get(node.left.name) === "TSString*") {
        keyStr = left;
      } else if (/^(Option|Command|Argument)_/.test(left) || left.startsWith("camelcase(") ||
                 left.startsWith("src_cli_commander_option_camelcase(") ||
                 left.startsWith("ts_string_") || left.includes("/*__ts_str*/")) {
        keyStr = left;
      } else if (left.startsWith("ts_value_string(")) {
        keyStr = left.replace(/^ts_value_string\((.+)\)$/, "$1");
      } else if (left.startsWith("ts_to_string(")) {
        // already coerced
        keyStr = left;
      } else if (node.left?.kind === "identifier" && this.varTypes.get(node.left.name) === "Value") {
        keyStr = `ts_to_string(${left})`;
      } else if (left.startsWith("ts_hashmap_get(") || left.startsWith("ts_value_") || left.startsWith("ts_array_get(")) {
        keyStr = `ts_to_string(${left})`;
      } else if (node.left?.kind === "string_literal") {
        keyStr = left;
      } else {
        // default: treat as TSString* identifier (attr etc.)
        keyStr = left;
      }
      // Always extract .as.object for Value maps (optionValues is Value, never TSHashMap*)
      // CRITICAL: cannot cast Value struct to pointer — must use .as.object first
      const rightName = node.right?.kind === "identifier" ? node.right.name : "";
      const rightType = rightName ? this.varTypes.get(rightName) : undefined;
      let mapExpr: string;
      if (rightType === "TSHashMap*") {
        mapExpr = right;
      } else {
        // Always .as.object for Value / unknown
        mapExpr = `((TSHashMap*)${right}.as.object)`;
      }
      return `ts_hashmap_has(${mapExpr}, ${keyStr})`;
    }

    // Comparison (=== and ==)
    if (op === "===" || op === "==") {
      // Check if comparing with undefined/null
      if (node.right?.kind === "undefined_literal" || node.right?.kind === "null_literal") {
        const leftIsValue = (node.left?.kind === "identifier" && this.varTypes.get(node.left.name) === "Value") ||
          left.startsWith("ts_hashmap_get(") || left.startsWith("ts_value_") || left.startsWith("ts_array_get(") ||
          (node.left?.kind === "property_access" && this.resolveTargetType(node.left) === "Value");
        if (leftIsValue) return `!ts_to_boolean(${left})`;
        return `!(${left})`;
      }
      if (node.left?.kind === "undefined_literal" || node.left?.kind === "null_literal") {
        const rightIsValue = node.right?.kind === "identifier" && this.varTypes.get(node.right.name) === "Value";
        if (rightIsValue) return `!ts_to_boolean(${right})`;
        return `!(${right})`;
      }
      // Value == number / boolean
      if (left.startsWith("ts_hashmap_get(") || left.startsWith("ts_value_") || left.startsWith("ts_array_get(") ||
          (node.left?.kind === "element_access") ||
          (node.left?.kind === "identifier" && this.varTypes.get(node.left.name) === "Value")) {
        if (node.right?.kind === "number_literal" || node.right?.kind === "boolean_literal") {
          return `(ts_to_number(${left}) == (double)(${right}))`;
        }
        if (node.right?.kind === "string_literal") {
          return `ts_string_equals(ts_to_string(${left}), ${right})`;
        }
      }
      return `(${left} == ${right})`;
    }
    if (op === "!==" || op === "!=") {
      if (node.right?.kind === "undefined_literal" || node.right?.kind === "null_literal") {
        // For Value types, use ts_to_boolean; for pointers, use !
        const leftIsValue = (node.left?.kind === "identifier" && this.varTypes.get(node.left.name) === "Value") ||
          left.startsWith("ts_hashmap_get(") || left.startsWith("ts_value_") ||
          (node.left?.kind === "property_access" && this.resolveTargetType(node.left) === "Value");
        if (leftIsValue) return `ts_to_boolean(${left})`;
        return `(!(${left}))`;
      }
      if (node.left?.kind === "undefined_literal" || node.left?.kind === "null_literal") {
        const rightIsValue = node.right?.kind === "identifier" && this.varTypes.get(node.right.name) === "Value";
        if (rightIsValue) return `ts_to_boolean(${right})`;
        return `(!(${right}))`;
      }
      // Value != undefined already handled; Value != number
      if (left.startsWith("ts_hashmap_get(") || left.startsWith("ts_value_") || left.startsWith("ts_array_get(") ||
          (node.left?.kind === "element_access")) {
        if (node.right?.kind === "number_literal" || node.right?.kind === "boolean_literal") {
          return `(ts_to_number(${left}) != (double)(${right}))`;
        }
      }
      return `(${left} != ${right})`;
    }
    if (op === "!=" || op === "==") {
      // Non-strict equality with undefined/null → null check
      if (node.right?.kind === "undefined_literal" || node.right?.kind === "null_literal") {
        const leftIsValue = node.left?.kind === "identifier" && this.varTypes.get(node.left.name) === "Value";
        if (op === "!=") {
          return leftIsValue ? `ts_to_boolean(${left})` : `(${left})`;
        }
        return leftIsValue ? `!ts_to_boolean(${left})` : `(!(${left}))`;
      }
      if (node.left?.kind === "undefined_literal" || node.left?.kind === "null_literal") {
        const rightIsValue = node.right?.kind === "identifier" && this.varTypes.get(node.right.name) === "Value";
        if (op === "!=") {
          return rightIsValue ? `ts_to_boolean(${right})` : `(${right})`;
        }
        return rightIsValue ? `!ts_to_boolean(${right})` : `(!(${right}))`;
      }
    }

    // Modulo operator - C doesn't support % on doubles, use fmod()
    if (op === "%") {
      return `fmod(${left}, ${right})`;
    }

    // Exponentiation operator - C doesn't support **, use pow()
    if (op === "**") {
      return `pow(${left}, ${right})`;
    }

    return `${left} ${op} ${right}`;
  }

  private emitUnary(node: CNode): string {
    const operand = this.emit(node.operand);
    if (node.operator === "typeof") {
      // ts_typeof expects Value and returns Value (string tag) — callers that
      // compare with string literals must use ts_to_string(ts_typeof(...))
      let wrapped = operand;
      if (node.operand.kind === "identifier") {
        const varType = this.varTypes.get(node.operand.name);
        if (varType === "double") wrapped = `ts_value_number(${operand})`;
        else if (varType === "TSString*") wrapped = `ts_value_string(${operand})`;
        else if (varType === "int") wrapped = `ts_value_boolean(${operand})`;
        else if (varType === "TSArray*") wrapped = `ts_value_array(${operand})`;
        else wrapped = operand; // assume it's already a Value
      } else if (node.operand.kind === "string_literal") {
        wrapped = `ts_value_string(${operand})`;
      } else if (node.operand.kind === "number_literal") {
        wrapped = `ts_value_number(${operand})`;
      } else if (node.operand.kind === "boolean_literal") {
        wrapped = `ts_value_boolean(${operand})`;
      }
      // Return TSString* form so string comparisons (typeof x === "string") work
      return `ts_to_string(ts_typeof(${wrapped}))`;
    }
    if (node.operator === "!") {
      // Value fields / expressions
      if (operand.startsWith("ts_value_") || operand.startsWith("ts_hashmap_get(") ||
          operand.startsWith("ts_fetch_") || operand.startsWith("ts_array_get(") ||
          (node.operand?.kind === "identifier" && this.varTypes.get(node.operand.name) === "Value") ||
          (node.operand?.kind === "property_access" && this.resolveTargetType(node.operand) === "Value") ||
          /->(defaultValue|presetArg|_opts|_implies|_outputConfig)\b/.test(operand)) {
        return `!ts_to_boolean(${operand})`;
      }
      // !ts_hashmap_has(...) is already int
      if (operand.startsWith("ts_hashmap_has(") || operand.startsWith("ts_string_equals(") ||
          operand.startsWith("ts_to_boolean(") || operand.startsWith("Option_isBoolean(") ||
          operand.startsWith("ts_array_index_of(")) {
        return `!${operand}`;
      }
      // Pointer null-check for TSString*/TSArray*
      if (node.operand?.kind === "property_access") {
        const t = this.resolveTargetType(node.operand);
        if (t === "TSString*" || t === "TSArray*" || (t && t.endsWith("*") && t !== "Value*")) {
          return `!(${operand})`;
        }
        if (t === "Value") return `!ts_to_boolean(${operand})`;
      }
      // binary `in` already produces int
      if (node.operand?.kind === "binary_expression" && node.operand.operator === "in") {
        return `!${operand}`;
      }
      return `!${operand}`;
    }
    if (node.operator === "void") {
      // Evaluate operand for side effects; value is undefined
      return `((void)(${operand}), ts_value_undefined())`;
    }
    if (node.prefix) {
      return `${node.operator}${operand}`;
    }
    // Postfix
    return `${operand}${node.operator}`;
  }

  private emitCall(node: CNode): string {
    const callee = node.callee;

    // Explicit GC: gc() / global.gc() → ts_gc_collect()
    if (callee.kind === "identifier" && callee.name === "gc") {
      return "(ts_gc_collect(), ts_value_undefined())";
    }
    if (callee.kind === "property_access" &&
        callee.property === "gc" &&
        callee.object?.kind === "identifier" &&
        (callee.object.name === "global" || callee.object.name === "globalThis")) {
      return "(ts_gc_collect(), ts_value_undefined())";
    }

    // Array.isArray(x) → check Value tag / pointer
    if (callee.kind === "property_access" &&
        (callee.property === "isArray") &&
        (callee.object?.kind === "identifier" &&
         (callee.object.name === "Array" || callee.object.name === "Array_"))) {
      const arg = node.arguments?.[0] ? this.emit(node.arguments[0]) : "ts_value_null()";
      // Runtime: TAG_ARRAY on Value, or non-null TSArray*
      if (arg.startsWith("ts_value_") || arg.startsWith("ts_hashmap_get(") ||
          (node.arguments?.[0]?.kind === "identifier" &&
           (this.varTypes.get(node.arguments[0].name) === "Value" || !this.varTypes.get(node.arguments[0].name)))) {
        return `(${arg}.tag == TAG_ARRAY)`;
      }
      // Already a TSArray* pointer
      return `(${arg} != NULL)`;
    }

    // Boolean(x) as function call → truthiness (returns int, not Value)
    if (callee.kind === "identifier" && (callee.name === "Boolean" || callee.name === "Boolean_")) {
      const arg = node.arguments?.[0] ? this.emit(node.arguments[0]) : "ts_value_null()";
      // Don't double-wrap — if already int expression, leave/coerce once
      if (arg.startsWith("ts_to_boolean(")) return arg;
      if (arg.startsWith("ts_value_") || arg.startsWith("ts_hashmap_get(") ||
          arg.startsWith("ts_array_get(") || arg.includes("presetArg") ||
          arg.includes("defaultValue") || arg.includes("->_opts")) {
        return `ts_to_boolean(${arg})`;
      }
      // Already int/pointer scalar
      if (arg.startsWith("ts_string_") || /->(required|optional|variadic|negate|hidden|mandatory)\b/.test(arg) ||
          arg.startsWith("Option_isBoolean(") || arg.startsWith("ts_hashmap_has(")) {
        return `!!(${arg})`;
      }
      // Value field access via ->defaultValue etc.
      if (/->(defaultValue|presetArg|_implies|_outputConfig)\b/.test(arg)) {
        return `ts_to_boolean(${arg})`;
      }
      return `ts_to_boolean(${arg})`;
    }

    // String(x) constructor/function → toString
    if (callee.kind === "identifier" && (callee.name === "String" || callee.name === "String_")) {
      const arg = node.arguments?.[0] ? this.emit(node.arguments[0]) : 'ts_string_new("")';
      if (arg.startsWith("ts_string_") || arg.startsWith("ts_to_string(") || arg.includes("/*__ts_str*/") ||
          /^(Command|Option|Argument)_/.test(arg)) return arg;
      if (arg.startsWith("ts_value_") || arg.startsWith("ts_hashmap_get(") || arg.startsWith("ts_array_get(")) {
        return `ts_to_string(${arg})`;
      }
      if (node.arguments?.[0]?.kind === "number_literal" ||
          (node.arguments?.[0]?.kind === "identifier" &&
           (this.varTypes.get(node.arguments[0].name) === "double" ||
            this.varTypes.get(node.arguments[0].name) === "number" ||
            this.varTypes.get(node.arguments[0].name) === "int"))) {
        return `ts_number_to_string((double)(${arg}))`;
      }
      // Value field
      if (/->(defaultValue|presetArg)\b/.test(arg)) return `ts_to_string(${arg})`;
      return `ts_to_string(${arg})`;
    }

    // Math.max / Math.min — reduce over args or single array
    if (callee.kind === "property_access" &&
        callee.object?.kind === "identifier" && callee.object.name === "Math" &&
        (callee.property === "max" || callee.property === "min")) {
      const args = (node.arguments || []).map((a: CNode) => this.emit(a));
      if (args.length === 0) return "0";
      if (args.length === 1) {
        // Math.max(...arr) via spread may be a single array — take first for now
        // Or Math.max(ts_array_map(...)) which returns TSArray* — need max of elements
        const a0 = args[0];
        if (a0.startsWith("ts_array_") || a0.includes("ts_array_map")) {
          const op = callee.property === "max" ? ">" : "<";
          return `({ TSArray* __mm = ${a0}; double __m = 0; int __mi = 0; for (int32_t __i = 0; __i < __mm->length; __i++) { double __v = ts_to_number(ts_array_get(__mm, __i)); if (__mi == 0 || __v ${op} __m) { __m = __v; __mi = 1; } } __m; })`;
        }
        return `ts_to_number(${a0})`;
      }
      // Multiple scalar args
      const fn = callee.property === "max" ? "fmax" : "fmin";
      let expr = args[0];
      for (let i = 1; i < args.length; i++) {
        expr = `${fn}(${expr}, ${args[i]})`;
      }
      return expr;
    }

    // Array.isArray via /*Array_isArray*/(x) fallback if property_access was pre-emitted
    if (callee.kind === "identifier" && callee.name === "/*Array_isArray*/") {
      const arg = node.arguments?.[0] ? this.emit(node.arguments[0]) : "ts_value_null()";
      return `(${arg}.tag == TAG_ARRAY)`;
    }

    // Browser-like dialogs: alert / confirm / prompt
    if (callee.kind === "identifier" &&
        (callee.name === "alert" || callee.name === "confirm" || callee.name === "prompt")) {
      const wrapMsg = (a: CNode | undefined): string => {
        if (!a) return `ts_value_string(ts_string_new(""))`;
        const emitted = this.emit(a);
        if (emitted.startsWith("ts_value_")) return emitted;
        if (a.kind === "string_literal") return `ts_value_string(${emitted})`;
        if (a.kind === "number_literal") return `ts_value_number(${emitted})`;
        if (a.kind === "boolean_literal") return `ts_value_boolean(${emitted})`;
        if (a.kind === "identifier") {
          const t = this.varTypes.get(a.name);
          if (t === "double" || t === "number") return `ts_value_number(${emitted})`;
          if (t === "TSString*" || t === "string") return `ts_value_string(${emitted})`;
          if (t === "int" || t === "boolean") return `ts_value_boolean(${emitted})`;
          if (t === "Value") return emitted;
        }
        if (emitted.startsWith("node_") || emitted.startsWith("ts_")) return emitted;
        return `ts_value_string(ts_to_string(${emitted}))`;
      };
      const msg = wrapMsg(node.arguments?.[0]);
      if (callee.name === "alert") return `ts_alert(${msg})`;
      if (callee.name === "confirm") return `ts_confirm(${msg})`;
      return `ts_prompt(${msg})`;
    }

    // node_process_exit / process.exit — wrap int as Value if needed
    // Handled via property_access path below

    // Global timers: setTimeout / setInterval / clearTimeout / clearInterval
    if (callee.kind === "identifier" &&
        (callee.name === "setTimeout" || callee.name === "setInterval" ||
         callee.name === "clearTimeout" || callee.name === "clearInterval")) {
      const wrapCb = (a: CNode | undefined): string => {
        if (!a) return "ts_value_null()";
        const emitted = this.emit(a);
        if (emitted.startsWith("ts_value_") || a.kind === "function_ref" ||
            a.kind === "arrow_function" || a.kind === "function_expression") return emitted;
        if (a.kind === "identifier") {
          const t = this.varTypes.get(a.name);
          if (t === "Value") return emitted;
        }
        // Bare function name / expression
        if (emitted.startsWith("ts_value_function(")) return emitted;
        return `ts_value_function((void*)${emitted})`;
      };
      /** Delay is always a number (ms). Never stringify binary/call expressions. */
      const wrapDelay = (a: CNode | undefined): string => {
        if (!a) return "ts_value_number(0)";
        const emitted = this.emit(a);
        if (emitted.startsWith("ts_value_number(")) return emitted;
        if (emitted.startsWith("ts_value_")) {
          // Value that may hold a number
          return `ts_value_number(ts_to_number(${emitted}))`;
        }
        // double-producing: literals, arithmetic, Math.*, randomInt(), identifiers
        if (a.kind === "number_literal" || a.kind === "binary_expression" ||
            a.kind === "unary_expression" || a.kind === "call_expression" ||
            a.kind === "parenthesized" || a.kind === "conditional_expression") {
          return `ts_value_number(${emitted})`;
        }
        if (a.kind === "identifier") {
          const t = this.varTypes.get(a.name);
          if (t === "double" || t === "number" || t === "int" || t === "boolean" || !t) {
            return `ts_value_number(${emitted})`;
          }
          if (t === "Value") return `ts_value_number(ts_to_number(${emitted}))`;
        }
        // ts_math_* / bare double expressions
        if (emitted.startsWith("ts_math_") || emitted.startsWith("ts_to_number(") ||
            emitted.startsWith("date_") || /^[a-zA-Z_][\w]*\(/.test(emitted)) {
          return `ts_value_number(${emitted})`;
        }
        return `ts_value_number(ts_to_number(${emitted}))`;
      };
      const wrapRest = (a: CNode): string => {
        const emitted = this.emit(a);
        if (emitted.startsWith("ts_value_") || a.kind === "function_ref" ||
            a.kind === "arrow_function" || a.kind === "function_expression") return emitted;
        if (a.kind === "string_literal") return `ts_value_string(${emitted})`;
        if (a.kind === "number_literal") return `ts_value_number(${emitted})`;
        if (a.kind === "boolean_literal") return `ts_value_boolean(${emitted})`;
        if (a.kind === "identifier") {
          const t = this.varTypes.get(a.name);
          if (t === "double" || t === "number") return `ts_value_number(${emitted})`;
          if (t === "TSString*" || t === "string") return `ts_value_string(${emitted})`;
          if (t === "int" || t === "boolean") return `ts_value_boolean(${emitted})`;
          if (t === "Value") return emitted;
        }
        if (emitted.startsWith("node_") || emitted.startsWith("ts_")) return emitted;
        return `ts_value_string(ts_to_string(${emitted}))`;
      };
      if (callee.name === "clearTimeout" || callee.name === "clearInterval") {
        let idArg = wrapDelay(node.arguments?.[0]);
        return callee.name === "clearTimeout"
          ? `ts_clear_timeout(${idArg})`
          : `ts_clear_interval(${idArg})`;
      }
      // setTimeout(fn, delay, ...args) / setInterval(fn, delay, ...args)
      const cb = wrapCb(node.arguments?.[0]);
      const delay = wrapDelay(node.arguments?.[1]);
      const rest = (node.arguments || []).slice(2).map(wrapRest);
      const cFn = callee.name === "setInterval" ? "ts_set_interval" : "ts_set_timeout";
      if (rest.length === 0) {
        return `${cFn}(${cb}, ${delay}, NULL, 0)`;
      }
      return `${cFn}(${cb}, ${delay}, (Value[]){${rest.join(", ")}}, ${rest.length})`;
    }

    // Special handling for console.log / console.info / console.warn / console.error / console.debug - wrap args in Value constructors
    if (callee.kind === "property_access" &&
        callee.object.kind === "identifier" &&
        callee.object.name === "console" &&
        (callee.property === "log" || callee.property === "info" || callee.property === "warn" || callee.property === "error" || callee.property === "debug")) {
      // Size fast-path: console.log("literal") → puts("literal") — no TSString/Value/runtime
      // Only for plain log (not colored info/warn/error/debug) and only string literals.
      if (callee.property === "log" &&
          node.arguments?.length === 1 &&
          node.arguments[0].kind === "string_literal") {
        const lit = this.emit(node.arguments[0]);
        // lit is typically ts_string_new("...") — extract C string if possible
        const m = lit.match(/^ts_string_new\(([\s\S]*)\)$/);
        if (m) {
          return `puts(${m[1]})`;
        }
        // Fallback: still avoid full inspect path
        return `puts((${lit})->data)`;
      }
      if (callee.property === "log" &&
          (!node.arguments || node.arguments.length === 0)) {
        return `puts("")`;
      }
      const consoleFn = callee.property === "log" ? "ts_console_log" :
                        callee.property === "info" ? "ts_console_info" :
                        callee.property === "warn" ? "ts_console_warn" :
                        callee.property === "debug" ? "ts_console_debug" : "ts_console_error";
      const args = (node.arguments || []).map((a: CNode) => {
        const emitted = this.emit(a);
        // If already a Value constructor, pass as-is
        if (emitted.startsWith("ts_value_") ||
            emitted.startsWith("ts_null(") ||
            emitted.startsWith("ts_undefined(") ||
            emitted.startsWith("ts_typeof(")) {
          return emitted;
        }
        // Builtin functions returning Value (node_ prefix + known names)
        if (emitted.match(/^node_\w+_(cwd|env|argv|readFileSync|writeFileSync|readdirSync|statSync|join|resolve|basename|dirname|extname|normalize|parse|format|isAbsolute|relative|createServer|request|get|execSync|platform|hostname|totalmem|freemem|arch|cpus|userInfo|type|release|loadavg|homedir|tmpdir|version|machine|EOL|devNull|stdin|stdout|stderr)\b/)) {
          return emitted;
        }
        // node_process_pid() returns int
        if (emitted.match(/^node_\w+_pid\s*\(/)) {
          return `ts_value_number((double)${emitted})`;
        }
        // totalmem/freemem/uptime return double
        if (emitted.match(/^node_\w+_(totalmem|freemem|uptime)\s*\(/)) {
          return `ts_value_number(${emitted})`;
        }
        // Date functions returning TSString* or double
        if (emitted.match(/^date_(toISOString|toDateString|toTimeString|toLocaleString)\b/)) {
          return `ts_value_string(${emitted})`;
        }
        if (emitted.match(/^date_(getFullYear|getMonth|getDate|getDay|getHours|getMinutes|getSeconds|getMilliseconds|getTime)\b/)) {
          return `ts_value_number(${emitted})`;
        }
        // Date.now() and Date.parse() return double
        if (emitted.match(/^date_(now_ts|parse_ts)\b/)) {
          return `ts_value_number(${emitted})`;
        }
        // Buffer functions returning int (boolean)
        if (emitted.match(/^ts_buffer_isBuffer\b/)) {
          return `ts_value_boolean(${emitted})`;
        }
        // Check if this is a function call that returns Value
        if (a.kind === "call_expression") {
          // For class methods, check the return type from the callee's propertyType
          if (a.callee && a.callee.kind === "property_access") {
            const propCType = a.callee.propertyCType;
            // propCType might be a function type like "TSString* (*)(void)"
            // Extract the return type from the function pointer type
            if (propCType && propCType.includes("(*)")) {
              // Function pointer type - extract return type
              const match = propCType.match(/^([^(]+)\s*\(\*\)/);
              if (match) {
                const returnType = match[1].trim();
                if (returnType === "TSString*") {
                  return `ts_value_string(${emitted})`;
                }
                if (returnType === "double") {
                  return `ts_value_number(${emitted})`;
                }
                if (returnType === "int") {
                  return `ts_value_boolean(${emitted})`;
                }
              }
            }
            // Direct type check
            if (propCType === "TSString*") {
              return `ts_value_string(${emitted})`;
            }
            if (propCType === "double") {
              return `ts_value_number(${emitted})`;
            }
            if (propCType === "int") {
              return `ts_value_boolean(${emitted})`;
            }
          }
          // For regular function calls, check if the return type is known
          // Builtin functions returning Value: cwd, env, argv, readFileSync, etc.
          // Builtin functions returning int: pid, existsSync, etc.
          if (a.callee && a.callee.kind === "property_access" && a.callee.object?.kind === "identifier") {
            const propName = a.callee.property;
            const valueReturningProps = new Set([
              "cwd", "env", "argv", "readFileSync", "writeFileSync", "readdirSync",
              "statSync", "join", "resolve", "basename", "dirname", "extname", "normalize",
              "parse", "format", "isAbsolute", "relative",
              "createServer", "request", "get", "execSync", "platform", "hostname",
              "totalmem", "freemem", "arch", "cpus", "userInfo", "type", "release",
              "loadavg", "homedir", "tmpdir", "version", "machine", "EOL", "devNull",
              "stdin", "stdout", "stderr",
            ]);
            if (valueReturningProps.has(propName)) {
              return emitted;
            }
          }
          // If not, assume it returns Value (already correct type)
          return emitted;
        }
        // Check if this is a method call that returns TSString*
        if (a.kind === "property_access" && a.object?.kind === "identifier") {
          const varType = this.varTypes.get(a.object.name);
          if (varType && varType.endsWith("*") && !varType.startsWith("TS")) {
            // Class method - assume it returns Value
            return emitted;
          }
        }
        // Try to determine type from the argument
        if (a.kind === "identifier") {
          // Look up by both original name and emitted name
          const varType = this.varTypes.get(a.name) || this.varTypes.get(emitted);
          if (varType === "double" || varType === "number") {
            return `ts_value_number(${emitted})`;
          }
          if (varType === "TSString*" || varType === "string") {
            return `ts_value_string(${emitted})`;
          }
          if (varType === "int" || varType === "boolean") {
            return `ts_value_boolean(${emitted})`;
          }
        }
        if (a.kind === "number_literal") {
          return `ts_value_number(${emitted})`;
        }
        // Numeric binary expressions (e.g., 0.1 + 0.2)
        // Comparison operators return int (boolean), not double (number)
        if (a.kind === "binary_expression" && a.leftType === "number") {
          const compOps = ["<", ">", "<=", ">=", "==", "===", "!=", "!=="];
          if (compOps.includes(a.operator)) {
            return `ts_value_boolean(${emitted})`;
          }
          return `ts_value_number(${emitted})`;
        }
        if (a.kind === "string_literal") {
          return `ts_value_string(${emitted})`;
        }
        if (a.kind === "boolean_literal") {
          return `ts_value_boolean(${emitted})`;
        }
        // Default: try to_string
        return `ts_value_string(ts_to_string(${emitted}))`;
      }).join(", ");
      // If multiple arguments, use multi-arg version to preserve object formatting
      if ((node.arguments?.length || 0) > 1) {
        const argList = (node.arguments || []).map((a: CNode) => this.emitConsoleLogArgRaw(a));
        return `${consoleFn}_multi((Value[]){${argList.join(", ")}}, ${argList.length})`;
      }
      // Single argument - pass raw Value
      if (node.arguments?.length === 1) {
        const argStr = this.emitConsoleLogArgRaw(node.arguments[0]);
        return `${consoleFn}(${argStr})`;
      }
      // Zero args — no-op
      if (!node.arguments || node.arguments.length === 0) {
        return `${consoleFn}(ts_value_string(ts_string_new("")))`;
      }
      return `${consoleFn}(${args})`;
    }

    // Special handling for console.time / console.timeEnd / assert / clear / count / ...
    if (callee.kind === "property_access" &&
        callee.object.kind === "identifier" &&
        callee.object.name === "console") {
      const method = callee.property;

      // console.time(label) / console.timeEnd(label)
      if (method === "time" || method === "timeEnd") {
        const label = node.arguments?.[0] ? this.emit(node.arguments[0]) : 'ts_string_new("default")';
        const fn = method === "time" ? "ts_console_time" : "ts_console_time_end";
        return `${fn}(${label})`;
      }

      // console.assert(condition, ...args)
      if (method === "assert") {
        const cond = node.arguments?.[0] ? this.emit(node.arguments[0]) : "1";
        const msg = node.arguments?.[1] ? this.emitConsoleLogArg(node.arguments[1]) : 'ts_string_new("assertion failed")';
        const condWrapped = cond.startsWith("ts_value_") ? cond : `ts_value_boolean(${cond})`;
        return `ts_console_assert(${condWrapped}, ts_value_string(${msg}))`;
      }

      // console.clear()
      if (method === "clear") {
        return `ts_console_clear()`;
      }

      // console.count(label) / console.countReset(label)
      if (method === "count" || method === "countReset") {
        const label = node.arguments?.[0] ? this.emit(node.arguments[0]) : 'ts_string_new("default")';
        const fn = method === "count" ? "ts_console_count" : "ts_console_count_reset";
        return `${fn}(${label})`;
      }

      // console.dir(obj)
      if (method === "dir") {
        const arg = node.arguments?.[0] ? this.emitConsoleLogArg(node.arguments[0]) : 'ts_string_new("")';
        return `ts_console_dir(ts_value_string(${arg}))`;
      }

      // console.group(...args) / console.groupEnd()
      if (method === "group") {
        return `ts_console_group()`;
      }
      if (method === "groupEnd") {
        return `ts_console_group_end()`;
      }

      // console.table(data)
      if (method === "table") {
        const arg = node.arguments?.[0] ? this.emitConsoleLogArg(node.arguments[0]) : 'ts_string_new("")';
        return `ts_console_table(ts_value_string(${arg}))`;
      }

      // console.trace(...args)
      if (method === "trace") {
        const arg = node.arguments?.[0] ? this.emitConsoleLogArg(node.arguments[0]) : 'ts_string_new("")';
        return `ts_console_trace(ts_value_string(${arg}))`;
      }
    }

    // Special handling for Math methods
    if (callee.kind === "property_access" &&
        callee.object.kind === "identifier" &&
        callee.object.name === "Math") {
      const args = (node.arguments || []).map((a: CNode) => this.emit(a)).join(", ");
      return `ts_math_${callee.property}(${args})`;
    }

    // Special handling for JSON methods
    if (callee.kind === "property_access" &&
        ((callee.object.kind === "identifier" && callee.object.name === "JSON") ||
         (callee.object.kind === "string_literal" && callee.object.value === "JSON"))) {
      const method = callee.property;
      if (method === "parse") {
        const arg = node.arguments?.[0] ? this.emit(node.arguments[0]) : "ts_string_new(\"\")";
        return `ts_json_parse(${arg})`;
      }
      if (method === "stringify") {
        const arg = node.arguments?.[0] ? this.emit(node.arguments[0]) : "ts_value_null()";
        let spaceVal = "0";
        if (node.arguments?.[2]) {
          const spaceArg = node.arguments[2];
          if (spaceArg.kind === "number_literal") {
            spaceVal = String(spaceArg.value);
          } else {
            spaceVal = `((int)ts_to_number(${this.emit(spaceArg)}))`;
          }
        }
        return `ts_json_stringify_indent(${arg}, ${spaceVal})`;
      }
      if (method === "isRawJSON") {
        const arg = node.arguments?.[0] ? this.emit(node.arguments[0]) : "ts_value_null()";
        let wrappedArg = arg;
        if (node.arguments?.[0]?.kind === "string_literal") {
          wrappedArg = `ts_value_string(${arg})`;
        }
        return `ts_value_boolean(ts_json_is_raw_json(${wrappedArg}))`;
      }
      if (method === "rawJSON") {
        const arg = node.arguments?.[0] ? this.emit(node.arguments[0]) : "ts_string_new(\"\")";
        return `ts_json_raw_json(${arg})`;
      }
    }

    // Special handling for Date methods
    if (callee.kind === "property_access" &&
        ((callee.object.kind === "identifier" && callee.object.name === "Date") ||
         (callee.object.kind === "string_literal" && callee.object.value === "Date"))) {
      const method = callee.property;
      if (method === "now") {
        return `date_now_ts()`;
      }
      if (method === "parse") {
        const arg = node.arguments?.[0] ? this.emit(node.arguments[0]) : "ts_string_new(\"\")";
        return `date_parse_ts(${arg})`;
      }
    }

    // Special handling for Buffer static methods
    if (callee.kind === "property_access" &&
        ((callee.object.kind === "identifier" && callee.object.name === "Buffer") ||
         (callee.object.kind === "string_literal" && callee.object.value === "Buffer"))) {
      const method = callee.property;
      if (method === "alloc") {
        const size = node.arguments?.[0] ? this.emit(node.arguments[0]) : "0";
        return `ts_buffer_alloc(${size})`;
      }
      if (method === "allocUnsafe") {
        const size = node.arguments?.[0] ? this.emit(node.arguments[0]) : "0";
        return `ts_buffer_allocUnsafe(${size})`;
      }
      if (method === "from") {
        const arg = node.arguments?.[0] ? this.emit(node.arguments[0]) : 'ts_string_new("")';
        // Buffer.from(string) or Buffer.from(array)
        if (node.arguments?.[0]?.kind === "string_literal") {
          return `ts_buffer_from_string(${arg})`;
        }
        return `ts_buffer_from_string(ts_to_string(${arg}))`;
      }
      if (method === "isBuffer") {
        const arg = node.arguments?.[0] ? this.emit(node.arguments[0]) : "ts_value_null()";
        // Wrap the argument in Value if it's not already
        let wrappedArg = arg;
        if (!arg.startsWith("ts_value_") && !arg.startsWith("ts_null(")) {
          if (node.arguments?.[0]?.kind === "string_literal") {
            wrappedArg = `ts_value_string(${arg})`;
          } else if (node.arguments?.[0]?.kind === "identifier") {
            wrappedArg = arg; // Already a Value variable
          } else {
            wrappedArg = `ts_value_string(ts_to_string(${arg}))`;
          }
        }
        return `ts_value_boolean(ts_buffer_isBuffer(${wrappedArg}))`;
      }
      if (method === "concat") {
        // Buffer.concat([buf1, buf2]) — array_literal emits TSArray* or Value
        const arrArg = node.arguments?.[0] ? this.emit(node.arguments[0]) : "NULL";
        // Normalize to TSArray*
        let arrExpr = arrArg;
        if (arrArg.startsWith("ts_value_array(")) {
          arrExpr = arrArg.replace(/^ts_value_array\((.+)\)$/, "$1");
        } else if (arrArg.startsWith("ts_array_")) {
          arrExpr = arrArg;
        } else if (arrArg.startsWith("ts_value_") || arrArg.includes(".as.")) {
          arrExpr = `((TSArray*)${arrArg}.as.array)`;
        } else {
          // bare TSArray* from array_literal
          arrExpr = arrArg;
        }
        return `({ TSArray* __bc = ${arrExpr}; ts_buffer_concat(__bc ? (Value*)__bc->items : NULL, __bc ? __bc->length : 0); })`;
      }
    }

    // Special handling for fetch(url, options)
    if (callee.kind === "identifier" && callee.name === "fetch") {
      const url = node.arguments?.[0] ? this.emit(node.arguments[0]) : 'ts_string_new("")';
      const options = node.arguments?.[1] ? this.emit(node.arguments[1]) : 'ts_value_null()';
      return `ts_fetch(${url}, ${options})`;
    }

    // Special handling for new Headers()
    if (callee.kind === "identifier" && callee.name === "Headers") {
      return `ts_headers()`;
    }

    // Special handling for new Blob()
    if (callee.kind === "identifier" && callee.name === "Blob") {
      const data = node.arguments?.[0] ? this.emit(node.arguments[0]) : "ts_string_new(\"\")";
      const type = node.arguments?.[1] ? this.emit(node.arguments[1]) : "ts_string_new(\"\")";
      return `ts_blob_from_string(${data}, ${type})`;
    }

    // Special handling for Response methods: text() / json() / clone() / blob()
    if (callee.kind === "property_access" &&
        (callee.property === "text" || callee.property === "json" || callee.property === "clone" || callee.property === "blob")) {
      const obj = this.emit(callee.object);
      if (callee.property === "text") return `ts_value_string(ts_fetch_text(${obj}))`;
      if (callee.property === "json") return `ts_fetch_json(${obj})`;
      if (callee.property === "clone") return `ts_fetch_clone(${obj})`;
      if (callee.property === "blob") return `ts_blob_from_string(ts_fetch_text(${obj}), ts_string_new("text/plain"))`;
    }

    // Response body stream: body.getReader() / reader.read()
    if (callee.kind === "property_access" && callee.property === "getReader") {
      const obj = this.emit(callee.object);
      return `ts_fetch_body_get_reader(${obj})`;
    }
    if (callee.kind === "property_access" && callee.property === "read") {
      const obj = this.emit(callee.object);
      // ReadableStreamDefaultReader.read()
      if (obj.includes("ts_fetch_body_get_reader") ||
          (callee.object.kind === "identifier" &&
           (this.varTypes.get(callee.object.name) === "Value" || /reader/i.test(callee.object.name)))) {
        return `ts_fetch_reader_read(${obj})`;
      }
    }

    // WritableStream / WritableStreamDefaultWriter methods
    if (callee.kind === "property_access" && callee.property === "getWriter") {
      const obj = this.emit(callee.object);
      return `ts_writable_stream_get_writer(${obj})`;
    }

    // WebSocket / WebSocketServer methods: send / close / addEventListener / removeEventListener
    if (callee.kind === "property_access" &&
        (callee.property === "send" || callee.property === "close" ||
         callee.property === "addEventListener" || callee.property === "removeEventListener")) {
      const typeName = String(callee.checkerTypeName || "");
      const objName = callee.object.kind === "identifier" ? callee.object.name : "";
      const isWs =
        /WebSocket/i.test(typeName) ||
        /ws|socket|wss/i.test(objName) ||
        (callee.object.kind === "identifier" && this.varTypes.get(objName) === "Value" &&
         /ws|socket/i.test(objName));
      if (isWs || callee.property === "addEventListener" || callee.property === "removeEventListener") {
        // Narrow add/removeEventListener: only when object looks like WS
        if ((callee.property === "addEventListener" || callee.property === "removeEventListener") &&
            !/WebSocket/i.test(typeName) && !/ws|socket|wss/i.test(objName)) {
          // fall through — may be EventTarget elsewhere
        } else {
          const obj = this.emit(callee.object);
          if (callee.property === "send") {
            const data = node.arguments?.[0] ? this.emit(node.arguments[0]) : `ts_value_string(ts_string_new(""))`;
            let dataVal = data;
            if (!data.startsWith("ts_value_") && !data.startsWith("ts_null(") && !data.startsWith("ts_undefined(")) {
              if (node.arguments?.[0]?.kind === "string_literal") dataVal = `ts_value_string(${data})`;
              else if (data.startsWith("ts_string_new(") || data.startsWith("ts_string_concat(") ||
                       data.startsWith("ts_to_string(") || data.includes("/*__ts_str*/")) {
                dataVal = `ts_value_string(${data})`;
              } else if (node.arguments?.[0]?.kind === "identifier") {
                const t = this.varTypes.get(node.arguments[0].name);
                if (t === "TSString*" || t === "string") dataVal = `ts_value_string(${data})`;
                else if (t === "Value" || !t) dataVal = data;
                else dataVal = `ts_value_string(ts_to_string(${data}))`;
              } else {
                dataVal = `ts_value_string(ts_to_string(${data}))`;
              }
            }
            return `ts_websocket_send(${obj}, ${dataVal})`;
          }
          if (callee.property === "close") {
            const code = node.arguments?.[0] ? this.emit(node.arguments[0]) : "ts_value_undefined()";
            const reason = node.arguments?.[1] ? this.emit(node.arguments[1]) : "ts_value_undefined()";
            let codeVal = code;
            if (!code.startsWith("ts_value_") && !code.startsWith("ts_null(") && !code.startsWith("ts_undefined(")) {
              if (node.arguments?.[0]?.kind === "number_literal") codeVal = `ts_value_number(${code})`;
              else codeVal = `ts_value_number(${code})`;
            }
            let reasonVal = reason;
            if (!reason.startsWith("ts_value_") && !reason.startsWith("ts_null(") && !reason.startsWith("ts_undefined(")) {
              if (node.arguments?.[1]?.kind === "string_literal") reasonVal = `ts_value_string(${reason})`;
              else if (reason.startsWith("ts_string_")) reasonVal = `ts_value_string(${reason})`;
            }
            return `ts_websocket_close(${obj}, ${codeVal}, ${reasonVal})`;
          }
          if (callee.property === "addEventListener" || callee.property === "removeEventListener") {
            const typeArg = node.arguments?.[0] ? this.emit(node.arguments[0]) : `ts_string_new("")`;
            let typeStr = typeArg;
            if (node.arguments?.[0]?.kind === "string_literal") typeStr = typeArg;
            else if (!typeArg.startsWith("ts_string_")) typeStr = `ts_to_string(${typeArg})`;
            let fn = node.arguments?.[1] ? this.emit(node.arguments[1]) : "ts_value_undefined()";
            if (!fn.startsWith("ts_value_")) fn = `ts_value_function((void*)${fn})`;
            const api = callee.property === "addEventListener"
              ? "ts_websocket_add_event_listener"
              : "ts_websocket_remove_event_listener";
            return `${api}(${obj}, ${typeStr}, ${fn})`;
          }
        }
      }
    }
    if (callee.kind === "property_access" &&
        (callee.property === "write" || callee.property === "close")) {
      const typeName = String(callee.checkerTypeName || "");
      const objName = callee.object.kind === "identifier" ? callee.object.name : "";
      const isWritableWriter =
        /WritableStream/i.test(typeName) ||
        /writer/i.test(objName);
      if (isWritableWriter) {
        const obj = this.emit(callee.object);
        if (callee.property === "close") {
          return `ts_writable_stream_close(${obj})`;
        }
        const chunk = node.arguments?.[0] ? this.emit(node.arguments[0]) : `ts_value_string(ts_string_new(""))`;
        let chunkVal = chunk;
        if (!chunk.startsWith("ts_value_") && !chunk.startsWith("ts_null(") && !chunk.startsWith("ts_undefined(")) {
          if (node.arguments?.[0]?.kind === "string_literal") {
            chunkVal = `ts_value_string(${chunk})`;
          } else if (chunk.startsWith("ts_string_new(") || chunk.startsWith("ts_string_concat(") ||
                     chunk.startsWith("ts_to_string(") || chunk.includes("/*__ts_str*/")) {
            chunkVal = `ts_value_string(${chunk})`;
          } else if (node.arguments?.[0]?.kind === "identifier") {
            const t = this.varTypes.get(node.arguments[0].name);
            if (t === "TSString*" || t === "string") chunkVal = `ts_value_string(${chunk})`;
            else if (t === "double" || t === "number") chunkVal = `ts_value_number(${chunk})`;
            else if (t === "Value" || !t) chunkVal = chunk;
            else chunkVal = `ts_value_string(ts_to_string(${chunk}))`;
          } else {
            chunkVal = `ts_value_string(ts_to_string(${chunk}))`;
          }
        }
        return `ts_writable_stream_write(${obj}, ${chunkVal})`;
      }
    }

    // Special handling for Blob methods: arrayBuffer()
    if (callee.kind === "property_access" && callee.property === "arrayBuffer") {
      const obj = this.emit(callee.object);
      return `ts_value_string(ts_blob_text(${obj}))`;
    }

    // Special handling for Buffer methods (only when name looks like a buffer)
    if (callee.kind === "property_access" &&
        callee.object.kind === "identifier" &&
        this.varTypes.get(callee.object.name) === "Value" &&
        /buf|buffer|chunk|data/i.test(callee.object.name)) {
      const method = callee.property;
      const obj = this.emit(callee.object);
      if (method === "slice") {
        const start = node.arguments?.[0] ? this.emit(node.arguments[0]) : "0";
        const end = node.arguments?.[1] ? this.emit(node.arguments[1]) : "-1";
        return `ts_buffer_slice(${obj}, ${start}, ${end})`;
      }
      if (method === "readUInt8") {
        const offset = node.arguments?.[0] ? this.emit(node.arguments[0]) : "0";
        return `ts_value_number(ts_buffer_readUInt8(${obj}, ${offset}))`;
      }
      if (method === "writeUInt8") {
        const value = node.arguments?.[0] ? this.emit(node.arguments[0]) : "0";
        const offset = node.arguments?.[1] ? this.emit(node.arguments[1]) : "0";
        return `ts_buffer_writeUInt8(${obj}, ${offset}, (uint8_t)${value})`;
      }
    }

    // Special handling for URL / Buffer / Value / number toString()
    // (class Point* etc. fall through to Class_toString dispatch below).
    if (callee.kind === "property_access" && callee.property === "toString") {
      const objName = callee.object.kind === "identifier" ? callee.object.name : "";
      const objType = objName ? this.varTypes.get(objName) : undefined;
      const isClassPtr =
        !!(objType && objType.endsWith("*") && !objType.startsWith("TS") &&
           objType !== "Url*" && !/Promise/i.test(objType));
      if (!isClassPtr) {
        const obj = this.emit(callee.object);
        // number / length scalars — never treat as Buffer just because name has "file"
        // e.g. file.length.toString() → ts_buffer_length(file) is int32_t
        const isNumberExpr =
          objType === "double" || objType === "number" || objType === "int" ||
          callee.object?.kind === "number_literal" ||
          obj.startsWith("ts_buffer_length(") ||
          obj.startsWith("ts_array_") && /length/.test(obj) ||
          /\.length\b/.test(obj) ||
          (callee.object?.kind === "property_access" && callee.object.property === "length");
        if (isNumberExpr) {
          return `ts_number_to_string((double)(${obj}))`;
        }
        // Known URL variables or URL constructor results
        if (obj.startsWith("ts_url_") || /url/i.test(objName) || objType === "Url*") {
          return `ts_url_toString(${obj})`;
        }
        // Buffer-like toString — only when the receiver itself is a Buffer/Value, not a scalar
        const isBufferLike =
          obj.startsWith("ts_buffer_") ||
          (objType === "Value" && /buf|buffer|file|chunk|data|body/i.test(objName));
        const encodingArg = node.arguments?.[0];
        const encoding = encodingArg ? this.emit(encodingArg) : null;
        if (isBufferLike || (encoding && (encoding.includes("hex") || encoding.includes("base64") || encoding.includes("utf")))) {
          if (encoding && encoding.includes("hex")) {
            return `ts_buffer_toString_hex(${obj})`;
          }
          if (encoding && encoding.includes("base64")) {
            return `ts_buffer_toString_base64(${obj})`;
          }
          // default utf-8
          return `ts_buffer_toString_utf8(${obj})`;
        }
        // Value / stream chunks / generic → ts_to_string
        // Only when not a typed class instance (handled later as Class_toString)
        if (objType === "Value" || objType === "any" || !objType ||
            obj.startsWith("ts_value_") || obj.startsWith("ts_hashmap_get(") ||
            obj.startsWith("node_")) {
          return `ts_to_string(${obj})`;
        }
      }
      // else: fall through to class method dispatch (Point_toString etc.)
    }

    // Special handling for Node built-in module calls
    // e.g., fs.readFileSync("file.txt", "utf-8") → node_fs_readFileSync(ts_value_string(ts_string_new("file.txt")), ts_value_string(ts_string_new("utf-8")))
    if (callee.kind === "property_access" &&
        callee.object.kind === "identifier") {
      const moduleName = callee.object.name;
      const builtinModules = ["fs", "path", "process", "os", "http", "net", "child_process", "events", "readline", "assert", "crypto", "worker_threads"];
      if (builtinModules.includes(moduleName)) {
        const funcName = `node_${moduleName}_${callee.property}`;
        // Wrap each argument in Value constructors
        let args = (node.arguments || []).map((a: CNode) => {
          const emitted = this.emit(a);
          // If already a Value constructor, pass as-is
          if (emitted.startsWith("ts_value_") ||
              emitted.startsWith("ts_null(") ||
              emitted.startsWith("ts_undefined(")) {
            return emitted;
          }
          // function_ref already emits ts_value_function
          if (a.kind === "function_ref" || a.kind === "arrow_function" || a.kind === "function_expression") {
            return emitted;
          }
          // String literal → ts_value_string(ts_string_new(...))
          if (a.kind === "string_literal") {
            return `ts_value_string(${emitted})`;
          }
          // Number literal → ts_value_number(...)
          if (a.kind === "number_literal") {
            return `ts_value_number(${emitted})`;
          }
          // Boolean literal → ts_value_boolean(...)
          if (a.kind === "boolean_literal") {
            return `ts_value_boolean(${emitted})`;
          }
          // Variable - check type
          if (a.kind === "identifier") {
            const varType = this.varTypes.get(a.name);
            if (varType === "double" || varType === "number") {
              return `ts_value_number(${emitted})`;
            }
            if (varType === "TSString*" || varType === "string") {
              return `ts_value_string(${emitted})`;
            }
            if (varType === "int" || varType === "boolean") {
              return `ts_value_boolean(${emitted})`;
            }
            if (varType === "Value") {
              return emitted;
            }
          }
          // Nested calls that already return Value (path.parse, fs.statSync, etc.)
          if (a.kind === "call_expression" ||
              emitted.startsWith("node_") || emitted.startsWith("ts_value_") ||
              emitted.startsWith("ts_fetch_") || emitted.startsWith("ts_json_")) {
            return emitted;
          }
          // Object / array literals already emit Value
          if (a.kind === "object_literal" || a.kind === "array_literal") {
            return emitted.startsWith("ts_value_") ? emitted : `ts_value_object(${emitted})`;
          }
          // Default: wrap as string
          return `ts_value_string(ts_to_string(${emitted}))`;
        });

        // For path.join and similar variadic functions, pass args as array
        if (callee.property === "join" || callee.property === "resolve") {
          return `${funcName}((Value[]){${args.join(", ")}}, ${node.arguments?.length || 0})`;
        }

        // path helpers with optional trailing args
        if (moduleName === "path") {
          const pathRequired: Record<string, number> = {
            basename: 2, // path, ext (ext optional → null)
            dirname: 1,
            extname: 1,
            normalize: 1,
            parse: 1,
            format: 1,
            isAbsolute: 1,
            relative: 2,
          };
          const required = pathRequired[callee.property];
          if (required !== undefined) {
            while (args.length < required) {
              args.push("ts_value_null()");
            }
          }
        }

        // Ensure fs functions have required number of arguments
        if (moduleName === "fs") {
          const requiredArgs: Record<string, number> = {
            readFileSync: 2, writeFileSync: 3, existsSync: 1,
            mkdirSync: 2, readdirSync: 1, unlinkSync: 1, statSync: 1,
            rmdirSync: 1, renameSync: 2, readlinkSync: 1, symlinkSync: 2, chmodSync: 2,
            readFile: 2, writeFile: 3, access: 2, mkdir: 2,
            readdir: 1, unlink: 1, stat: 1, rmdir: 1,
            rename: 2, readlink: 1, symlink: 2, chmod: 2,
          };
          const required = requiredArgs[callee.property] || args.length;
          while (args.length < required) {
            args.push("ts_value_null()");
          }
        }

        // events: EventEmitter() zero-arg constructor; emit is variadic
        if (moduleName === "events") {
          if (callee.property === "EventEmitter") {
            return `node_events_EventEmitter()`;
          }
          if (callee.property === "defaultMaxListeners") {
            return `node_events_defaultMaxListeners()`;
          }
          if (callee.property === "getEventListeners") {
            while (args.length < 2) args.push("ts_value_null()");
            return `node_events_getEventListeners(${args[0]}, ${args[1]})`;
          }
          if (callee.property === "setDefaultMaxListeners") {
            while (args.length < 1) args.push("ts_value_number(10)");
            return `node_events_setDefaultMaxListeners(${args[0]})`;
          }
        }

        // readline module-level API
        if (moduleName === "readline") {
          if (callee.property === "createInterface") {
            while (args.length < 1) args.push("ts_value_null()");
            return `node_readline_createInterface(${args[0]})`;
          }
          if (callee.property === "clearLine") {
            while (args.length < 2) args.push("ts_value_number(0)");
            return `node_readline_clearLine(${args[0]}, ${args[1]})`;
          }
          if (callee.property === "cursorTo") {
            while (args.length < 2) args.push("ts_value_number(0)");
            if (args.length < 3) args.push("ts_value_null()");
            return `node_readline_cursorTo(${args[0]}, ${args[1]}, ${args[2]})`;
          }
          if (callee.property === "moveCursor") {
            while (args.length < 3) args.push("ts_value_number(0)");
            return `node_readline_moveCursor(${args[0]}, ${args[1]}, ${args[2]})`;
          }
        }

        // assert: optional message / flexible arity
        if (moduleName === "assert") {
          const twoArg = new Set(["ok", "assert", "ifError", "fail"]);
          const threeArg = new Set([
            "equal", "notEqual", "strictEqual", "notStrictEqual",
            "deepEqual", "deepStrictEqual", "notDeepEqual", "notDeepStrictEqual",
            "match", "doesNotMatch",
          ]);
          const twoFn = new Set(["throws", "doesNotThrow"]);
          if (callee.property === "fail") {
            while (args.length < 1) args.push("ts_value_null()");
            return `node_assert_fail(${args[0]})`;
          }
          if (callee.property === "ifError") {
            while (args.length < 1) args.push("ts_value_null()");
            return `node_assert_ifError(${args[0]})`;
          }
          if (twoArg.has(callee.property)) {
            while (args.length < 2) args.push("ts_value_null()");
            const cName = callee.property === "assert" ? "node_assert_assert" : `node_assert_${callee.property}`;
            return `${cName}(${args[0]}, ${args[1]})`;
          }
          if (twoFn.has(callee.property)) {
            while (args.length < 2) args.push("ts_value_null()");
            return `node_assert_${callee.property}(${args[0]}, ${args[1]})`;
          }
          if (threeArg.has(callee.property)) {
            while (args.length < 3) args.push("ts_value_null()");
            return `node_assert_${callee.property}(${args[0]}, ${args[1]}, ${args[2]})`;
          }
        }

        // child_process: flexible arity (exec(cmd, cb) vs exec(cmd, opts, cb))
        if (moduleName === "child_process") {
          if (callee.property === "exec") {
            // (command, callback) or (command, options, callback)
            if (args.length === 1) args.push("ts_value_null()", "ts_value_null()");
            else if (args.length === 2) {
              // second is callback → insert null options
              args = [args[0], "ts_value_null()", args[1]];
            }
            while (args.length < 3) args.push("ts_value_null()");
          } else if (callee.property === "execFile") {
            // (file, args?, options?, callback?)
            // Common: (file, args, callback) or (file, callback)
            if (args.length === 1) {
              args.push("ts_value_null()", "ts_value_null()", "ts_value_null()");
            } else if (args.length === 2) {
              // file, callback OR file, args
              if (node.arguments?.[1]?.kind === "function_ref" ||
                  node.arguments?.[1]?.kind === "arrow_function" ||
                  node.arguments?.[1]?.kind === "function_expression" ||
                  args[1].startsWith("ts_value_function(")) {
                args = [args[0], "ts_value_null()", "ts_value_null()", args[1]];
              } else {
                args = [args[0], args[1], "ts_value_null()", "ts_value_null()"];
              }
            } else if (args.length === 3) {
              // file, args, callback
              if (node.arguments?.[2]?.kind === "function_ref" ||
                  node.arguments?.[2]?.kind === "arrow_function" ||
                  node.arguments?.[2]?.kind === "function_expression" ||
                  args[2].startsWith("ts_value_function(")) {
                args = [args[0], args[1], "ts_value_null()", args[2]];
              }
            }
            while (args.length < 4) args.push("ts_value_null()");
          } else if (callee.property === "spawn") {
            // (command, args?, options?)
            while (args.length < 3) args.push("ts_value_null()");
          } else if (callee.property === "fork") {
            // (modulePath, args?, options?)
            while (args.length < 3) args.push("ts_value_null()");
          }
        }

        // crypto: ensure required arguments
        if (moduleName === "crypto") {
          const cryptoRequired: Record<string, number> = {
            pbkdf2Sync: 5, pbkdf2: 6, scryptSync: 3,
          };
          const required = cryptoRequired[callee.property];
          if (required !== undefined) {
            while (args.length < required) {
              args.push(callee.property === "pbkdf2Sync" && args.length === 4
                ? 'ts_value_string(ts_string_new("sha256"))'
                : "ts_value_null()");
            }
          }
        }

        // worker_threads module-level helpers
        if (moduleName === "worker_threads") {
          if (callee.property === "Worker") {
            while (args.length < 2) args.push("ts_value_null()");
          } else if (callee.property === "BroadcastChannel") {
            while (args.length < 1) args.push('ts_value_string(ts_string_new(""))');
          } else if (callee.property === "setEnvironmentData") {
            while (args.length < 2) args.push("ts_value_null()");
          } else if (callee.property === "getEnvironmentData" ||
                     callee.property === "receiveMessageOnPort" ||
                     callee.property === "markAsUntransferable" ||
                     callee.property === "isMarkedAsUntransferable" ||
                     callee.property === "markAsUncloneable") {
            while (args.length < 1) args.push("ts_value_null()");
          } else if (callee.property === "postMessageToThread") {
            while (args.length < 3) args.push("ts_value_null()");
          } else if (callee.property === "moveMessagePortToContext") {
            while (args.length < 2) args.push("ts_value_null()");
          }
        }

        // For functions that return Value, we need to wrap in type conversion
        // if the caller expects a specific type (like TSString*)
        // For now, return the Value directly and let the caller handle conversion
        return `${funcName}(${args.join(", ")})`;
      }
    }

    // Special handling for crypto hash method calls
    // e.g., hash.update(data) → node_crypto_hashUpdate(hash, data)
    //       hash.digest('hex') → node_crypto_hashDigest(hash, ts_value_string(ts_string_new("hex")))
    if (callee.kind === "property_access" &&
        callee.object.kind === "identifier" &&
        (callee.property === "update" || callee.property === "digest")) {
      const objName = callee.object.name;
      const objType = this.varTypes.get(objName);
      // Check if this looks like a crypto hash object:
      // - named hash/hmac, OR typed as Value (crypto.createHash returns Value)
      // - OR the variable was assigned from a node_crypto_createHash/createHmac call (heuristic: starts with h + digit)
      const looksLikeHash = objType === "Value" || /hash|hmac/i.test(objName) ||
        (objType === undefined && /^h\d*$/.test(objName)) ||
        (objType === "Value" && !/server|socket|req|res|child|proc|readline|rl|emitter|ee/i.test(objName));
      if (looksLikeHash) {
        const self = this.emit(callee.object);
        const wrapArg = (a: CNode): string => {
          const emitted = this.emit(a);
          if (emitted.startsWith("ts_value_") || a.kind === "function_ref" ||
              a.kind === "arrow_function" || a.kind === "function_expression") return emitted;
          if (a.kind === "string_literal") return `ts_value_string(${emitted})`;
          if (a.kind === "number_literal") return `ts_value_number(${emitted})`;
          if (a.kind === "boolean_literal") return `ts_value_boolean(${emitted})`;
          if (a.kind === "identifier") {
            const t = this.varTypes.get(a.name);
            if (t === "double" || t === "number") return `ts_value_number(${emitted})`;
            if (t === "TSString*" || t === "string") return `ts_value_string(${emitted})`;
            if (t === "int" || t === "boolean") return `ts_value_boolean(${emitted})`;
            if (t === "Value") return emitted;
          }
          return `ts_value_string(ts_to_string(${emitted}))`;
        };
        const callArgs = (node.arguments || []).map(wrapArg);
        if (callee.property === "update") {
          while (callArgs.length < 1) callArgs.push('ts_value_string(ts_string_new(""))');
          return `node_crypto_hashUpdate(${self}, ${callArgs.join(", ")})`;
        }
        if (callee.property === "digest") {
          while (callArgs.length < 1) callArgs.push('ts_value_string(ts_string_new("hex"))');
          return `node_crypto_hashDigest(${self}, ${callArgs.join(", ")})`;
        }
      }
    }

    // Special handling for method calls on class instances
    // e.g., p.toString() → Point_toString(p)
    if (callee.kind === "property_access") {
      const objectName = callee.object.kind === "identifier" ? callee.object.name : null;
      const methodName = callee.property;
      let varType = objectName ? this.varTypes.get(objectName) : null;
      // Infer type from call-expression objects (e.g. process.argv.slice, flags.split().filter)
      if (!varType && callee.object.kind === "call_expression") {
        const inner = this.emit(callee.object);
        if (inner.startsWith("ts_string_split(") || inner.startsWith("ts_array_") ||
            inner.includes("ts_array_") || inner.startsWith("node_process_argv(")) {
          varType = "TSArray*";
        } else if (inner.startsWith("ts_string_") || inner.startsWith("ts_to_string(")) {
          varType = "TSString*";
        } else if (inner.startsWith("node_process_argv(")) {
          varType = "TSArray*";
        }
      }
      // node_process_argv() and similar Value-returning builtins used as arrays
      if (!varType && objectName) {
        // leave null
      }
      if (!varType && callee.object.kind === "call_expression") {
        const emittedObj = this.emit(callee.object);
        if (emittedObj.startsWith("node_process_argv(") || emittedObj.startsWith("node_")) {
          // Treat process.argv-like as array for .slice/.length
          if (["slice", "push", "filter", "map", "join", "find", "some", "every",
               "forEach", "reduce", "includes", "indexOf", "concat", "pop"].includes(methodName)) {
            varType = "TSArray*";
          }
        }
      }

      // " ".repeat(n) / str.repeat(n)
      if (methodName === "repeat") {
        const strObj = this.emit(callee.object);
        let n = (node.arguments || []).map((a: CNode) => this.emit(a))[0] || "0";
        // Ensure count is a scalar int, not a string expression
        if (n.startsWith("ts_string_") || n.startsWith("ts_to_string(") || n.includes("/*__ts_str*/")) {
          n = `ts_to_number(${n})`;
        }
        // Parenthesize arithmetic expressions for cast
        const nExpr = (n.includes("+") || n.includes("-") || n.includes("*") || n.includes("/"))
          ? `(int32_t)(${n})`
          : `(int32_t)(${n})`;
        const sExpr = strObj.startsWith("ts_string_") || strObj.startsWith("ts_to_string(") ||
          strObj.includes("/*__ts_str*/") || strObj.startsWith("ts_string_new(")
          ? strObj
          : (callee.object.kind === "string_literal" ? strObj : `ts_to_string(${strObj})`);
        return `({ TSString* __rp_s = ${sExpr}; int32_t __rp_n = ${nExpr}; if (__rp_n < 0) __rp_n = 0; TSString* __rp_r = ts_string_new(""); for (int32_t __rpi = 0; __rpi < __rp_n; __rpi++) __rp_r = ts_string_concat(__rp_r, __rp_s); __rp_r; /*__ts_str*/ })`;
      }

      // TSString* / string / Value-as-string method calls
      // e.g. str.startsWith(x) → ts_string_starts_with(str, x)
      // Also handles Value-typed params from array callbacks (filter/find) that are really strings.
      {
        const stringMethodMap: Record<string, string> = {
          "startsWith": "ts_string_starts_with",
          "endsWith": "ts_string_ends_with",
          "includes": "ts_string_includes",
          "indexOf": "ts_string_index_of",
          "replace": "ts_string_replace",
          "substring": "ts_string_substring",
          "toLowerCase": "ts_string_to_lower",
          "toUpperCase": "ts_string_to_upper",
          "trim": "ts_string_trim",
          "charAt": "ts_string_char_at",
          // slice on strings: use substring (start, end) — handled specially below
        };
        const isKnownStringMethod = !!(stringMethodMap[methodName] || methodName === "split" || methodName === "slice");
        // Prefer array slice when object is clearly an array
        const clearlyArray =
          varType === "TSArray*" || varType === "array" || callee.objectType === "array" ||
          (callee.object.kind === "property_access" && this.isStructArrayMember(callee.object)) ||
          (callee.object.kind === "identifier" && objectName && this.varTypes.get(objectName)?.startsWith("TSArray"));
        const objIsString =
          !clearlyArray && (
          varType === "TSString*" || varType === "string" ||
          callee.objectType === "string" ||
          (callee.object.kind === "identifier" && objectName && this.varTypes.get(objectName)?.startsWith("TSString")) ||
          // chain temp / call result typed as string
          (callee.object.kind === "identifier" && objectName?.startsWith("__chain_") &&
            (this.varTypes.get(objectName!) === "TSString*" || this.varTypes.get(objectName!) === "string")) ||
          // charAt returns char but we may have stored as string chain
          (callee.object.kind === "identifier" && objectName?.startsWith("__chain_") &&
            methodName === "toUpperCase")
          );
        // Value + known string method → coerce via ts_to_string (array callback params, etc.)
        // Prefer string methods over array when method is string-only
        const stringOnlyMethods = new Set([
          "startsWith", "endsWith", "toLowerCase", "toUpperCase", "trim", "charAt",
          "replace", "substring", "split",
        ]);
        const valueAsString = isKnownStringMethod && !clearlyArray && (
          varType === "Value" ||
          callee.objectType === "Value" ||
          callee.objectType === "any" ||
          (!varType && stringOnlyMethods.has(methodName) && callee.object.kind === "identifier") ||
          (stringOnlyMethods.has(methodName) && callee.object.kind === "identifier")
        );
        if (objIsString || valueAsString) {
          let strObj = this.emit(callee.object);
          // charAt returns char in C — wrap single char into a string for chaining
          if (strObj.startsWith("ts_string_char_at(")) {
            // Keep as-is for now; toUpperCase on char isn't ideal — convert char to string
            strObj = `ts_string_new_len((char[]){${strObj}, 0}, 1)`;
          }
          // Coerce Value-typed receivers (and any non-string expression) to TSString*
          if (varType === "Value" || valueAsString) {
            if (strObj.startsWith("ts_string_new(") || strObj.startsWith("ts_to_string(") ||
                strObj.startsWith("ts_string_concat(") || strObj.startsWith("ts_string_new_len(") ||
                strObj.startsWith("ts_string_to_upper(") || strObj.startsWith("ts_string_to_lower(") ||
                strObj.startsWith("ts_string_trim(") || strObj.startsWith("ts_string_substring(") ||
                strObj.startsWith("ts_string_replace(") || strObj.includes("/*__ts_str*/") ||
                (objIsString && varType === "TSString*")) {
              // already string-like
            } else if (varType === "TSString*") {
              // typed string — leave
            } else {
              strObj = `ts_to_string(${strObj})`;
            }
          }
          const strArgs = (node.arguments || []).map((a: CNode) => {
            const emitted = this.emit(a);
            // string methods expect TSString* args (except index numbers)
            if (a.kind === "string_literal" || emitted.startsWith("ts_string_new(") ||
                emitted.startsWith("ts_to_string(") || emitted.startsWith("ts_string_concat(") ||
                emitted.includes("/*__ts_str*/")) {
              return emitted;
            }
            if (a.kind === "number_literal") return emitted;
            if (a.kind === "identifier") {
              const t = this.varTypes.get(a.name);
              if (t === "TSString*" || t === "string") return emitted;
              if (t === "double" || t === "number" || t === "int") return emitted;
              if (t === "Value") return `ts_to_string(${emitted})`;
            }
            if (emitted.startsWith("ts_value_string(")) {
              return emitted.replace(/^ts_value_string\((.+)\)$/, "$1");
            }
            // default: leave as-is for numeric args; wrap others
            if (methodName === "substring" || methodName === "slice" || methodName === "charAt" || methodName === "indexOf") {
              return emitted;
            }
            return `ts_to_string(${emitted})`;
          });
          if (methodName === "slice") {
            // string.slice(start, end?) → ts_string_substring
            while (strArgs.length < 2) strArgs.push("0x7fffffff");
            return `ts_string_substring(${strObj}, ${strArgs[0]}, ${strArgs[1]})`;
          }
          // word.slice(1) when word is Value — already coerced via strObj
          if (stringMethodMap[methodName]) {
            if (methodName === "substring") {
              while (strArgs.length < 2) strArgs.push("0x7fffffff");
            }
            // charAt: return as single-char string for chaining friendliness when assigned to TSString*
            if (methodName === "charAt") {
              const idx = strArgs[0] || "0";
              return `ts_string_new_len((char[]){ts_string_char_at(${strObj}, ${idx}), 0}, 1)`;
            }
            return `${stringMethodMap[methodName]}(${strObj}${strArgs.length > 0 ? ", " + strArgs.join(", ") : ""})`;
          }
          if (methodName === "split") {
            const sep = strArgs[0] || 'ts_string_new("")';
            return `ts_string_split(${strObj}, ${sep})`;
          }
        }
      }

      // TSArray* method calls: arr.push(x) → ts_array_push(arr, x)
      // Also when objectType is array, or chain temp is TSArray*, or Value that is clearly an array method.
      {
        const arrayMethodMap: Record<string, string> = {
          "push": "ts_array_push",
          "indexOf": "ts_array_index_of",
          "splice": "ts_array_splice",
          "pop": "ts_array_pop",
          "includes": "ts_array_index_of",
          "find": "ts_array_find",
          "concat": "ts_array_concat",
        };
        const arrayHigherMethods: Record<string, string> = {
          "filter": "ts_array_filter",
          "map": "ts_array_map",
          "join": "ts_array_join",
          "some": "ts_array_some",
          "every": "ts_array_every",
          "find": "ts_array_find",
          "reduce": "ts_array_reduce",
          "forEach": "ts_array_foreach",
          "slice": "ts_array_slice",
        };
        const isKnownArrayMethod = !!(arrayMethodMap[methodName] || arrayHigherMethods[methodName] || methodName === "slice");
        const objIsArray =
          varType === "TSArray*" || varType === "array" ||
          callee.objectType === "array" ||
          (callee.object.kind === "identifier" && objectName && this.varTypes.get(objectName)?.startsWith("TSArray")) ||
          (callee.object.kind === "property_access" && callee.object.propertyCType?.startsWith("TSArray")) ||
          (callee.object.kind === "property_access" && this.isStructArrayMember(callee.object)) ||
          (callee.object.kind === "identifier" && objectName?.startsWith("__chain_") &&
            this.varTypes.get(objectName!) === "TSArray*");
        // Value + known array method (e.g. self->_conflicts.concat, or Value-typed arrays)
        const valueAsArray = isKnownArrayMethod && (
          varType === "Value" ||
          callee.objectType === "Value" ||
          callee.objectType === "any" ||
          // struct field access that resolves via -> to TSArray* but varType is null for non-identifiers
          (callee.object.kind === "property_access")
        );
        if (objIsArray || (valueAsArray && isKnownArrayMethod)) {
          let arrObj = this.emit(callee.object);
          // Coerce Value → TSArray* when needed
          if (!objIsArray && (arrObj.startsWith("ts_hashmap_get(") || arrObj.startsWith("ts_value_") ||
              (varType === "Value"))) {
            arrObj = `((TSArray*)${arrObj}.as.object)`;
          }
          // struct field that was emitted as self->_conflicts (already TSArray*) — leave as-is
          // but if emit produced `.concat` style fallback later we won't reach here

          const wrapValueArg = (argNode: CNode, argStr: string): string => {
            if (argStr.startsWith("ts_value_") || argStr.startsWith("ts_null(") ||
                argStr.startsWith("ts_undefined(") || argStr.startsWith("ts_typeof(")) {
              return argStr;
            }
            if (argNode.kind === "string_literal") return `ts_value_string(${argStr})`;
            if (argNode.kind === "number_literal") return `ts_value_number(${argStr})`;
            if (argNode.kind === "boolean_literal") return `ts_value_boolean(${argStr})`;
            if (argNode.kind === "function_ref" || argNode.kind === "arrow_function" ||
                argNode.kind === "function_expression") {
              return argStr.startsWith("ts_value_function(") ? argStr : `ts_value_function((void*)${argStr})`;
            }
            if (argNode.kind === "identifier") {
              const t = this.varTypes.get(argNode.name);
              if (t === "double" || t === "number") return `ts_value_number(${argStr})`;
              if (t === "TSString*" || t === "string") return `ts_value_string(${argStr})`;
              if (t === "int" || t === "boolean") return `ts_value_boolean(${argStr})`;
              if (t === "Value") return argStr;
              if (t === "TSArray*") return `ts_value_array(${argStr})`;
              // Function pointer params (ActionHandler etc.)
              if (t && t.includes("(*)")) return `ts_value_function((void*)${argStr})`;
              if (t && t.endsWith("*") && !t.startsWith("TS")) return `ts_value_object((void*)${argStr})`;
            }
            // Bare function pointer expression
            if (argStr.includes("(*)") || /^\w+$/.test(argStr)) {
              const t = argNode.kind === "identifier" ? this.varTypes.get(argNode.name) : undefined;
              if (t && t.includes("(*)")) return `ts_value_function((void*)${argStr})`;
            }
            if (argStr.endsWith(")")) {
              if (argNode.kind === "call_expression") {
                const calleeName = argNode.callee?.name || argNode.callee?.property || "";
                if (/^[A-Z]/.test(calleeName) || calleeName.includes("_constructor")) {
                  return `ts_value_object((void*)${argStr})`;
                }
              }
            }
            if (argStr.includes("/*__ts_str*/") || argStr.startsWith("ts_string_new(") ||
                argStr.startsWith("ts_to_string(") || argStr.startsWith("ts_string_concat(") ||
                argStr.startsWith("ts_string_") || argStr.startsWith("ts_array_join(") ||
                argStr.startsWith("ts_json_stringify") ||
                /^(Command|Option|Argument)_/.test(argStr) ||
                argStr.startsWith("camelcase(") || argStr.startsWith("src_cli_commander_")) {
              return `ts_value_string(${argStr})`;
            }
            // Struct field TSString* (opt->short_, opt->long_)
            if (argStr.includes("->short_") || argStr.includes("->long_") ||
                argStr.includes("->flags") || argStr.includes("->description") ||
                argStr.includes("->_name") || argStr.includes("->_version") ||
                argStr.includes("->envVar")) {
              return `ts_value_string(${argStr})`;
            }
            return argStr;
          };

          const arrArgs = (node.arguments || []).map((a: CNode) => this.emit(a));
          if (arrayMethodMap[methodName]) {
            const mappedFn = arrayMethodMap[methodName];
            if (methodName === "includes") {
              const wrapped = node.arguments?.[0]
                ? wrapValueArg(node.arguments[0], arrArgs[0])
                : "ts_value_null()";
              return `(${mappedFn}(${arrObj}, ${wrapped}) >= 0)`;
            }
            if (methodName === "find") {
              // ts_array_find expects int(*)(Value) — unwrap ts_value_function
              let pred = arrArgs[0] || "NULL";
              if (pred.startsWith("ts_value_function((void*)")) {
                pred = pred.replace(/^ts_value_function\(\(void\*\)/, "").replace(/\)$/, "");
              }
              return `ts_array_find(${arrObj}, ${pred})`;
            }
            if (methodName === "push" && node.arguments && node.arguments.length > 0) {
              const wrappedArgs = node.arguments.map((a: CNode, i: number) => {
                let w = wrapValueArg(a, arrArgs[i]);
                // Pushing a whole TSArray* (e.g. from slice) → wrap as Value array
                if (w.startsWith("({") && (w.includes("TSArray*") || w.includes("__sl_dst") || w.includes("__sp_dst") || w.includes("__c_dst"))) {
                  w = `ts_value_array(${w})`;
                } else if (w.startsWith("ts_array_") || w.startsWith("ts_string_split(")) {
                  w = `ts_value_array(${w})`;
                }
                return w;
              });
              return `ts_array_push(${arrObj}, ${wrappedArgs[0] || "ts_value_null()"})`;
            }
            // Always unwrap ts_value_function for find even if hit via map default
            if (mappedFn === "ts_array_find") {
              let pred = arrArgs[0] || "NULL";
              if (pred.startsWith("ts_value_function((void*)")) {
                pred = pred.replace(/^ts_value_function\(\(void\*\)/, "").replace(/\)$/, "");
              }
              return `ts_array_find(${arrObj}, ${pred})`;
            }
            if (methodName === "concat") {
              // Runtime may not have ts_array_concat — synthesize with push loop is complex;
              // emit a simple form: ts_array_concat if available, else leave call
              const other = node.arguments?.[0]
                ? wrapValueArg(node.arguments[0], arrArgs[0])
                : "ts_value_array(ts_array_new())";
              // Prefer treating other as TSArray*
              let otherArr = arrArgs[0] || "ts_array_new()";
              if (otherArr.startsWith("ts_value_array(")) {
                otherArr = otherArr.replace(/^ts_value_array\((.+)\)$/, "$1");
              } else if (other.startsWith("ts_value_")) {
                otherArr = `((TSArray*)${other}.as.object)`;
              }
              // Inline concat using a compound statement returning a new array
              return `({ TSArray* __c_dst = ts_array_new(); for (int32_t __ci = 0; __ci < ${arrObj}->length; __ci++) ts_array_push(__c_dst, ts_array_get(${arrObj}, __ci)); for (int32_t __ci = 0; __ci < ${otherArr}->length; __ci++) ts_array_push(__c_dst, ts_array_get(${otherArr}, __ci)); __c_dst; })`;
            }
            // Default for remaining array methods (pop, indexOf, …)
            {
              let finalArgs = arrArgs;
              if (methodName === "find" || methodName === "indexOf") {
                finalArgs = arrArgs.map((a: string, i: number) => {
                  if (i === 0 && a.startsWith("ts_value_function((void*)")) {
                    return a.replace(/^ts_value_function\(\(void\*\)/, "").replace(/\)$/, "");
                  }
                  if (methodName === "indexOf" && i === 0) {
                    // wrap non-Value
                    return a.startsWith("ts_value_") ? a : `ts_value_string(ts_to_string(${a}))`;
                  }
                  return a;
                });
              }
              return `${mappedFn}(${arrObj}${finalArgs.length > 0 ? ", " + finalArgs.join(", ") : ""})`;
            }
          }
          // arr.slice(start[, end]) — synthesize (runtime may lack ts_array_slice)
          if (methodName === "slice") {
            const start = arrArgs[0] || "0";
            const end = arrArgs[1]; // optional
            // Coerce Value receivers (node_process_argv() returns Value)
            let src = arrObj;
            if (src.startsWith("node_process_argv(") || src.startsWith("ts_value_") ||
                src.startsWith("ts_hashmap_get(")) {
              src = `((TSArray*)${src}.as.object)`;
            }
            if (end !== undefined) {
              return `({ TSArray* __sl_src = ${src}; int32_t __sl_s = (int32_t)(${start}); int32_t __sl_e = (int32_t)(${end}); if (__sl_s < 0) __sl_s = 0; if (__sl_e < 0) __sl_e = __sl_src->length + __sl_e; if (__sl_e > __sl_src->length) __sl_e = __sl_src->length; TSArray* __sl_dst = ts_array_new(); for (int32_t __si = __sl_s; __si < __sl_e; __si++) ts_array_push(__sl_dst, ts_array_get(__sl_src, __si)); __sl_dst; })`;
            }
            return `({ TSArray* __sl_src = ${src}; int32_t __sl_s = (int32_t)(${start}); if (__sl_s < 0) __sl_s = 0; TSArray* __sl_dst = ts_array_new(); for (int32_t __si = __sl_s; __si < __sl_src->length; __si++) ts_array_push(__sl_dst, ts_array_get(__sl_src, __si)); __sl_dst; })`;
          }
          if (arrayHigherMethods[methodName] && methodName !== "slice") {
            // filter(Boolean) → predicate that truthiness-checks each element
            const arg0 = node.arguments?.[0];
            const arg0Emitted = arrArgs[0] || "";
            const isBooleanPredicate =
              methodName === "filter" && (
                (arg0?.kind === "identifier" && /^(Boolean|Boolean_)$/.test(arg0.name || "")) ||
                /^(Boolean|Boolean_)$/.test(arg0Emitted) ||
                arg0Emitted === 'ts_string_new("Boolean")' ||
                /Boolean/.test(arg0Emitted)
              );
            if (isBooleanPredicate) {
              return `ts_array_filter(${arrObj}, (int(*)(Value))ts_to_boolean)`;
            }
            // Pass function refs as raw function pointers (runtime expects C function pointers)
            let higherArgs = arrArgs.map((a: string, i: number) => {
              if (i === 0 && a.startsWith("ts_value_function((void*)")) {
                return a.replace(/^ts_value_function\(\(void\*\)/, "").replace(/\)$/, "");
              }
              return a;
            });
            // find/filter/some/every expect int(*)(Value) or Value(*)(Value) — unwrap ts_value_function
            if (["find", "filter", "some", "every", "map", "forEach", "reduce"].includes(methodName)) {
              higherArgs = higherArgs.map((a: string, i: number) => {
                if (i === 0 && a.startsWith("ts_value_function((void*)")) {
                  return a.replace(/^ts_value_function\(\(void\*\)/, "").replace(/\)$/, "");
                }
                return a;
              });
            }
            // some/every/filter with Option* callback — cast arr from Value if needed
            // join expects TSString* separator
            if (methodName === "join") {
              let sep = higherArgs[0] || 'ts_string_new(",")';
              if (sep.startsWith("ts_value_string(")) sep = sep.replace(/^ts_value_string\((.+)\)$/, "$1");
              else if (!sep.startsWith("ts_string_") && !sep.startsWith("ts_to_string(")) {
                sep = `ts_to_string(${sep})`;
              }
              return `ts_array_join(${arrObj}, ${sep})`;
            }
            // reduce needs initial value — if missing, use first element semantics via undefined
            if (methodName === "reduce") {
              const fn = higherArgs[0] || "NULL";
              // Extract raw function pointer if wrapped
              const fnRaw = fn.startsWith("ts_value_function((void*)")
                ? fn.replace(/^ts_value_function\(\(void\*\)/, "").replace(/\)$/, "")
                : fn;
              // Adapter: if reduce callback returns TSString*, wrap via a cast is hard;
              // emit as Value-returning by casting the function pointer
              const init = higherArgs[1] || "ts_value_undefined()";
              return `ts_array_reduce(${arrObj}, (Value(*)(Value,Value))${fnRaw}, ${init})`;
            }
            // find expects int(*)(Value) for predicate in our runtime — but TS find returns element;
            // our runtime.ts_array_find takes int(*predicate)(Value)
            if (methodName === "find" || methodName === "filter" || methodName === "some" || methodName === "every") {
              const fn = higherArgs[0] || "NULL";
              const fnRaw = fn.startsWith("ts_value_function((void*)")
                ? fn.replace(/^ts_value_function\(\(void\*\)/, "").replace(/\)$/, "")
                : fn;
              // Cast Option*/Argument* predicates to int(*)(Value) via thunk is hard;
              // emit a cast — runtime passes Value, callback may expect Option*
              // For Option* callbacks, wrap: (int(*)(Value))fn still works if layout matches (first field)
              // Better: cast through void*
              if (methodName === "filter" || methodName === "some" || methodName === "every" || methodName === "find") {
                return `${arrayHigherMethods[methodName]}(${arrObj}, (int(*)(Value))(void*)${fnRaw})`;
              }
              return `${arrayHigherMethods[methodName]}(${arrObj}, ${fnRaw})`;
            }
            // map: Value(*)(Value) — cast double-returning / Option*-taking callbacks
            if (methodName === "map") {
              const fn = higherArgs[0] || "NULL";
              const fnRaw = fn.startsWith("ts_value_function((void*)")
                ? fn.replace(/^ts_value_function\(\(void\*\)/, "").replace(/\)$/, "")
                : fn;
              // Wrap non-Value-returning mappers via cast (lossy but compiles)
              return `ts_array_map(${arrObj}, (Value(*)(Value))(void*)${fnRaw})`;
            }
            return `${arrayHigherMethods[methodName]}(${arrObj}${higherArgs.length > 0 ? ", " + higherArgs.join(", ") : ""})`;
          }
        }
      }

      // readline.Interface methods: rl.question / rl.close / rl.on / …
      const rlMethods = new Set([
        "question", "close", "on", "prompt", "setPrompt", "getPrompt",
        "write", "pause", "resume",
      ]);
      const looksLikeReadline =
        !!objectName && /^(rl|readline|interface)$/i.test(objectName);
      if (rlMethods.has(methodName) && callee.object?.kind === "identifier" && looksLikeReadline) {
        const self = this.emit(callee.object);
        const wrap = (a: CNode): string => {
          const emitted = this.emit(a);
          if (emitted.startsWith("ts_value_") || a.kind === "function_ref" ||
              a.kind === "arrow_function" || a.kind === "function_expression") return emitted;
          if (a.kind === "string_literal") return `ts_value_string(${emitted})`;
          if (a.kind === "number_literal") return `ts_value_number(${emitted})`;
          if (a.kind === "boolean_literal") return `ts_value_boolean(${emitted})`;
          if (a.kind === "identifier") {
            const t = this.varTypes.get(a.name);
            if (t === "double" || t === "number") return `ts_value_number(${emitted})`;
            if (t === "TSString*" || t === "string") return `ts_value_string(${emitted})`;
            if (t === "int" || t === "boolean") return `ts_value_boolean(${emitted})`;
            if (t === "Value") return emitted;
          }
          if (emitted.startsWith("node_") || emitted.startsWith("ts_")) return emitted;
          return `ts_value_string(ts_to_string(${emitted}))`;
        };
        const callArgs = (node.arguments || []).map(wrap);
        if (methodName === "close" || methodName === "prompt" ||
            methodName === "getPrompt" || methodName === "pause" || methodName === "resume") {
          return `node_readline_${methodName}(${self})`;
        }
        if (methodName === "setPrompt" || methodName === "write") {
          while (callArgs.length < 1) callArgs.push("ts_value_string(ts_string_new(\"\"))");
          return `node_readline_${methodName}(${self}, ${callArgs[0]})`;
        }
        if (methodName === "on") {
          while (callArgs.length < 2) callArgs.push("ts_value_null()");
          return `node_readline_on(${self}, ${callArgs[0]}, ${callArgs[1]})`;
        }
        if (methodName === "question") {
          // question(query, callback)
          while (callArgs.length < 2) callArgs.push("ts_value_null()");
          return `node_readline_question(${self}, ${callArgs[0]}, ${callArgs[1]})`;
        }
      }

      // EventEmitter instance methods: ee.on / ee.emit / ee.once / …
      // Prefer events over child_process when name looks like an emitter.
      const eeMethods = new Set([
        "on", "addListener", "once", "off", "removeListener",
        "prependListener", "prependOnceListener", "emit",
        "removeAllListeners", "listenerCount", "listeners", "rawListeners",
        "eventNames", "setMaxListeners", "getMaxListeners",
      ]);
      const looksLikeEmitter =
        !!objectName &&
        (/ee|emitter|event/i.test(objectName) ||
          this.varTypes.get(objectName) === "Value" && !/child|spawn|fork|dir|proc|cp|server|req|res|rl|readline|worker|port|parent|channel|bc/i.test(objectName));
      if (eeMethods.has(methodName) && callee.object?.kind === "identifier" && looksLikeEmitter) {
        const self = this.emit(callee.object);
        const wrap = (a: CNode): string => {
          const emitted = this.emit(a);
          if (emitted.startsWith("ts_value_") || a.kind === "function_ref" ||
              a.kind === "arrow_function" || a.kind === "function_expression") return emitted;
          if (a.kind === "string_literal") return `ts_value_string(${emitted})`;
          if (a.kind === "number_literal") return `ts_value_number(${emitted})`;
          if (a.kind === "boolean_literal") return `ts_value_boolean(${emitted})`;
          if (a.kind === "identifier") {
            const t = this.varTypes.get(a.name);
            if (t === "double" || t === "number") return `ts_value_number(${emitted})`;
            if (t === "TSString*" || t === "string") return `ts_value_string(${emitted})`;
            if (t === "int" || t === "boolean") return `ts_value_boolean(${emitted})`;
            if (t === "Value") return emitted;
          }
          if (emitted.startsWith("node_") || emitted.startsWith("ts_")) return emitted;
          return `ts_value_string(ts_to_string(${emitted}))`;
        };
        const callArgs = (node.arguments || []).map(wrap);

        if (methodName === "emit") {
          // node_events_emit(ee, event, (Value[]){...}, argc)
          const eventArg = callArgs[0] || `ts_value_string(ts_string_new(""))`;
          const rest = callArgs.slice(1);
          if (rest.length === 0) {
            return `node_events_emit(${self}, ${eventArg}, NULL, 0)`;
          }
          return `node_events_emit(${self}, ${eventArg}, (Value[]){${rest.join(", ")}}, ${rest.length})`;
        }
        if (methodName === "eventNames" || methodName === "getMaxListeners") {
          return `node_events_${methodName}(${self})`;
        }
        if (methodName === "removeAllListeners") {
          const ev = callArgs[0] || "ts_value_null()";
          return `node_events_removeAllListeners(${self}, ${ev})`;
        }
        if (methodName === "setMaxListeners") {
          while (callArgs.length < 1) callArgs.push("ts_value_number(10)");
          return `node_events_setMaxListeners(${self}, ${callArgs[0]})`;
        }
        // listenerCount / listeners / rawListeners: (event) only
        if (methodName === "listenerCount" || methodName === "listeners" || methodName === "rawListeners") {
          while (callArgs.length < 1) callArgs.push("ts_value_string(ts_string_new(\"\"))");
          return `node_events_${methodName}(${self}, ${callArgs[0]})`;
        }
        // on / once / off / addListener / removeListener / prepend*
        while (callArgs.length < 2) callArgs.push("ts_value_null()");
        return `node_events_${methodName}(${self}, ${callArgs[0]}, ${callArgs[1]})`;
      }

      // worker_threads: worker/port.postMessage, worker.on, worker.terminate, port.close/start
      // Also handle chained property access: channel.port1.on(...), channel.port2.postMessage(...)
      {
        // Detect property_access chains like channel.port1.on(...) where root looks like worker
        const isPropertyAccessWorker =
          callee.object?.kind === "property_access" &&
          callee.object.object?.kind === "identifier" &&
          (/worker|port|parent|channel|bc|w\d*$/i.test(callee.object.object.name || "") ||
           callee.object.object.name === "parentPort" ||
           this.importedSymbols.has(callee.object.object.name));
        const looksLikeWorker =
          varType === "Value" ||
          /worker|port|parent|channel|bc|w\d*$/i.test(objectName || "") ||
          objectName === "parentPort" ||
          isPropertyAccessWorker ||
          (callee.object?.kind === "identifier" &&
            (callee.object.name === "parentPort" ||
             this.importedSymbols.has(callee.object.name)));
        if (looksLikeWorker && (callee.object?.kind === "identifier" || isPropertyAccessWorker)) {
          const self = this.emit(callee.object);
          const wrapArg = (a: CNode): string => {
            const emitted = this.emit(a);
            if (emitted.startsWith("ts_value_") || a.kind === "function_ref" ||
                a.kind === "arrow_function" || a.kind === "function_expression" ||
                a.kind === "object_literal" || a.kind === "array_literal") return emitted;
            if (a.kind === "string_literal") return `ts_value_string(${emitted})`;
            if (a.kind === "number_literal") return `ts_value_number(${emitted})`;
            if (a.kind === "boolean_literal") return `ts_value_boolean(${emitted})`;
            return emitted;
          };
          if (methodName === "postMessage") {
            const v = node.arguments?.[0] ? wrapArg(node.arguments[0]) : "ts_value_null()";
            const t = node.arguments?.[1] ? wrapArg(node.arguments[1]) : "ts_value_null()";
            return `node_worker_threads_postMessage(${self}, ${v}, ${t})`;
          }
          if (methodName === "on" || methodName === "once" || methodName === "off" ||
              methodName === "addListener" || methodName === "removeListener") {
            // Prefer worker_threads when name looks like worker/port; leave child_process for child/spawn/fork
            if (!/child|spawn|fork|dir|proc|cp/i.test(objectName || "") ||
                /worker|port|parent/i.test(objectName || "") ||
                objectName === "parentPort") {
              const callArgs = (node.arguments || []).map(wrapArg);
              while (callArgs.length < 2) callArgs.push("ts_value_null()");
              return `node_worker_threads_${methodName}(${self}, ${callArgs[0]}, ${callArgs[1]})`;
            }
          }
          if (methodName === "terminate") {
            return `node_worker_threads_terminate(${self})`;
          }
          if (methodName === "close") {
            return `node_worker_threads_close(${self})`;
          }
          if (methodName === "start") {
            return `node_worker_threads_start(${self})`;
          }
          if (methodName === "ref" || methodName === "unref") {
            return `node_worker_threads_${methodName}(${self})`;
          }
        }
        // parentPort may be a property access result stored as Value — also handle
        // worker_threads.parentPort.postMessage via module getter already returning Value.
        if (methodName === "postMessage" && callee.object?.kind === "call_expression") {
          const self = this.emit(callee.object);
          const v = node.arguments?.[0] ? this.emit(node.arguments[0]) : "ts_value_null()";
          const vv = v.startsWith("ts_value_") ? v : v;
          return `node_worker_threads_postMessage(${self}, ${vv}, ts_value_null())`;
        }
      }

      // child.stdout.on / child.stderr.on / child.stdin.on / child.send
      // Must NOT match process.stdin.on — that is handled by node_process_stdin_on below.
      if (methodName === "on" &&
          callee.object?.kind === "property_access" &&
          callee.object.object?.kind === "identifier" &&
          callee.object.object.name !== "process" &&
          (callee.object.property === "stdout" || callee.object.property === "stderr" ||
           callee.object.property === "stdin")) {
        const childObj = this.emit(callee.object.object);
        const stream = callee.object.property;
        const callArgs = (node.arguments || []).map((a: CNode) => {
          const emitted = this.emit(a);
          if (emitted.startsWith("ts_value_") || a.kind === "function_ref" ||
              a.kind === "arrow_function" || a.kind === "function_expression") return emitted;
          if (a.kind === "string_literal") return `ts_value_string(${emitted})`;
          return emitted;
        });
        while (callArgs.length < 2) callArgs.push("ts_value_null()");
        return `node_child_process_stream_on(${childObj}, ts_value_string(ts_string_new("${stream}")), ${callArgs[0]}, ${callArgs[1]})`;
      }
      if (methodName === "on" &&
          callee.object?.kind === "identifier" &&
          (varType === "Value" || objectName === "dir" || objectName === "forked" ||
           objectName === "child" || objectName === "proc" || /child|spawn|fork/i.test(objectName || ""))) {
        const childObj = this.emit(callee.object);
        const callArgs = (node.arguments || []).map((a: CNode) => {
          const emitted = this.emit(a);
          if (emitted.startsWith("ts_value_") || a.kind === "function_ref") return emitted;
          if (a.kind === "string_literal") return `ts_value_string(${emitted})`;
          return emitted;
        });
        while (callArgs.length < 2) callArgs.push("ts_value_null()");
        // Avoid stealing http server / other Value.on if clearly not a child
        // Prefer child_process_on for known patterns
        return `node_child_process_on(${childObj}, ${callArgs[0]}, ${callArgs[1]})`;
      }
      if (methodName === "send" &&
          callee.object?.kind === "identifier" &&
          (varType === "Value" || /fork|child/i.test(objectName || ""))) {
        const childObj = this.emit(callee.object);
        const msg = node.arguments?.[0] ? this.emit(node.arguments[0]) : "ts_value_null()";
        const msgWrapped = msg.startsWith("ts_value_") ? msg : `ts_value_string(ts_to_string(${msg}))`;
        // object literals already return Value
        const finalMsg = (node.arguments?.[0]?.kind === "object_literal" || msg.startsWith("ts_value_"))
          ? msg : msgWrapped;
        return `node_child_process_send(${childObj}, ${finalMsg})`;
      }

      // process.stdin/stdout/stderr method calls
      // e.g. process.stdin.on(...) → node_process_stdin_on(...)
      //      process.stdout.write(...) → node_process_stdout_write(...)
      if (callee.object?.kind === "property_access" &&
          callee.object.object?.kind === "identifier" &&
          callee.object.object.name === "process") {
        const stream = callee.object.property; // stdin | stdout | stderr
        if (stream === "stdin" || stream === "stdout" || stream === "stderr") {
          const wrapArg = (a: CNode): string => {
            const emitted = this.emit(a);
            if (emitted.startsWith("ts_value_") || a.kind === "function_ref") return emitted;
            if (a.kind === "string_literal") return `ts_value_string(${emitted})`;
            if (a.kind === "number_literal") return `ts_value_number(${emitted})`;
            if (a.kind === "boolean_literal") return `ts_value_boolean(${emitted})`;
            if (a.kind === "identifier") {
              const t = this.varTypes.get(a.name);
              if (t === "double" || t === "number") return `ts_value_number(${emitted})`;
              if (t === "TSString*" || t === "string") return `ts_value_string(${emitted})`;
              if (t === "int" || t === "boolean") return `ts_value_boolean(${emitted})`;
              if (t === "Value") return emitted;
            }
            // string-producing helpers (ts_string_new / concat / to_string)
            if (emitted.startsWith("ts_string_") || emitted.startsWith("ts_to_string(")) {
              return `ts_value_string(${emitted})`;
            }
            return emitted;
          };

          if (methodName === "on") {
            const callArgs = (node.arguments || []).map(wrapArg);
            while (callArgs.length < 2) callArgs.push("ts_value_null()");
            return `node_process_${stream}_on(${callArgs.join(", ")})`;
          }

          // WriteStream methods on stdout/stderr
          if (stream === "stdout" || stream === "stderr") {
            if (methodName === "write") {
              const callArgs = (node.arguments || []).map(wrapArg);
              while (callArgs.length < 1) callArgs.push("ts_value_string(ts_string_new(\"\"))");
              return `node_process_${stream}_write(${callArgs[0]})`;
            }
            if (methodName === "cursorTo") {
              const callArgs = (node.arguments || []).map(wrapArg);
              while (callArgs.length < 2) callArgs.push("ts_value_number(0)");
              return `node_process_${stream}_cursorTo(${callArgs[0]}, ${callArgs[1]})`;
            }
            if (methodName === "moveCursor") {
              const callArgs = (node.arguments || []).map(wrapArg);
              while (callArgs.length < 2) callArgs.push("ts_value_number(0)");
              return `node_process_${stream}_moveCursor(${callArgs[0]}, ${callArgs[1]})`;
            }
            if (methodName === "clearScreenDown") {
              return `node_process_${stream}_clearScreenDown()`;
            }
            if (methodName === "clearLine") {
              const callArgs = (node.arguments || []).map(wrapArg);
              while (callArgs.length < 1) callArgs.push("ts_value_number(0)");
              return `node_process_${stream}_clearLine(${callArgs[0]})`;
            }
          }
        }
      }

      // data.toString() when data is Value (stream chunk) → ts_to_string
      // Do NOT steal typed class instances (Point* → Point_toString below).
      if (methodName === "toString") {
        const isClassPtr =
          !!(varType && varType.endsWith("*") && !varType.startsWith("TS") &&
             !/Promise/i.test(varType) && varType !== "Url*");
        if (!isClassPtr) {
          const obj = this.emit(callee.object);
          if (objectName && this.varTypes.get(objectName) === "Value") {
            return `ts_to_string(${obj})`;
          }
          if (obj.startsWith("ts_value_") || obj.startsWith("ts_hashmap_get(") ||
              obj.includes("node_process_") || obj.includes(".as.object")) {
            return `ts_to_string(${obj})`;
          }
          if (varType === "Value" || !varType) {
            if (!obj.startsWith("ts_url_")) {
              return `ts_to_string(${obj})`;
            }
          }
        }
      }

      // http.Server.listen / net.Server.listen — handle before class dispatch
      // (checker may report type name "Server" which is not a real C struct here)
      if (methodName === "listen" && (varType === "Value" || objectName === "server" ||
          /server/i.test(objectName || "") ||
          (callee.object && this.emit(callee.object).includes("node_http")) ||
          (callee.checkerTypeName && /Server/i.test(callee.checkerTypeName)))) {
        const serverArg = this.emit(callee.object);
        const callArgs = (node.arguments || []).map((a: CNode) => {
          const emitted = this.emit(a);
          if (emitted.startsWith("ts_value_") || a.kind === "function_ref" ||
              a.kind === "arrow_function" || a.kind === "function_expression") return emitted;
          if (a.kind === "number_literal") return `ts_value_number(${emitted})`;
          if (a.kind === "string_literal") return `ts_value_string(${emitted})`;
          if (a.kind === "identifier") {
            const t = this.varTypes.get(a.name);
            if (t === "double" || t === "number") return `ts_value_number(${emitted})`;
            if (t === "TSString*" || t === "string") return `ts_value_string(${emitted})`;
            if (t === "Value") return emitted;
          }
          return `ts_value_number(${emitted})`;
        });
        while (callArgs.length < 2) callArgs.push("ts_value_null()");
        return `node_http_server_listen(${serverArg}, ${callArgs.join(", ")})`;
      }

      // Known commander / class methods on Value-typed receivers (structural types lose class name)
      // e.g. command.getName() where command is the formatHelp structural parameter
      if ((varType === "Value" || !varType || varType === "any") && objectName) {
        const commandMethods = new Set([
          "getName", "getDescription", "getVersion", "getAlias",
          "version", "description", "name", "parse", "parseAsync", "opts",
          "option", "requiredOption", "argument", "command", "action",
          "help", "outputHelp", "helpInformation", "alias", "addOption",
          "addArgument", "addCommand", "allowUnknownOption", "allowExcessArguments",
          "hook", "exitOverride", "configureOutput", "hidden",
        ]);
        const optionMethods = new Set([
          "default", "preset", "conflicts", "implies", "env", "argParser",
          "makeOptionMandatory", "hideHelp", "choices", "name", "attributeName",
          "isBoolean", "parseFlags",
        ]);
        const argumentMethods = new Set([
          "name", "default", "argParser", "choices", "argRequired", "argOptional",
        ]);
        let className: string | null = null;
        if (callee.checkerTypeName && /^[A-Z]/.test(callee.checkerTypeName) &&
            !callee.checkerTypeName.startsWith("TS") && !callee.checkerTypeName.includes(" ") &&
            !callee.checkerTypeName.includes("{") &&
            // Runtime Value wrappers — not real C class structs
            !/^(Server|IncomingMessage|ClientRequest|Socket|Agent|Buffer|URL|Url|Headers|Response|Request|WritableStream|ReadableStream|TransformStream|WebSocket|WebSocketServer)$/i.test(
              callee.checkerTypeName.replace(/\*$/, "").replace(/<.*>/, ""))) {
          // Strip TypeScript generics: WritableStreamDefaultWriter<any> → invalid C
          const rawName = callee.checkerTypeName.replace(/\*$/, "").replace(/<.*>/, "");
          if (/[<>]/.test(rawName) || !/^[A-Za-z_][A-Za-z0-9_]*$/.test(rawName)) {
            className = null;
          } else {
            className = rawName;
          }
        } else if (commandMethods.has(methodName) &&
                   /program|cmd|command|this/i.test(objectName)) {
          className = "Command";
        } else if (optionMethods.has(methodName) && /opt|option/i.test(objectName)) {
          className = "Option";
        } else if (argumentMethods.has(methodName) && /arg|argument/i.test(objectName)) {
          className = "Argument";
        } else if (commandMethods.has(methodName) && (objectName === "c" || objectName === "command" ||
                   objectName.startsWith("__chain_"))) {
          // formatHelp callbacks use short names like `c`
          className = "Command";
        }
        const checkerBase = callee.checkerTypeName?.replace(/\*$/, "").replace(/<.*>/, "");
        if (className && (commandMethods.has(methodName) || optionMethods.has(methodName) ||
            argumentMethods.has(methodName) || className === checkerBase)) {
          const selfCast = varType === "Value" || !varType || varType === "any"
            ? `((${className}*)${objectName}.as.object)`
            : objectName;
          // Prefer hardcoded signatures for known methods — checker overloads
          // (e.g. argument's parse-fn overload) often pick the wrong param types.
          const knownTypes = this.knownClassMethodParamTypes(className, methodName);
          let expectedParamTypes = knownTypes.length > 0
            ? knownTypes
            : this.parseExpectedParamTypes(callee.propertyCType);
          const args = (node.arguments || []).map((a: CNode, idx: number) => {
            const emitted = this.emit(a);
            const expectedType = idx < expectedParamTypes.length ? expectedParamTypes[idx] : "";
            // Function-pointer params for action/hook
            if (expectedType.includes("(*)") || (methodName === "action" && idx === 0) ||
                (methodName === "hook" && idx === 1)) {
              let fnPtr = emitted;
              if (fnPtr.startsWith("ts_value_function((void*)")) {
                fnPtr = fnPtr.replace(/^ts_value_function\(\(void\*\)/, "").replace(/\)$/, "");
              } else if (fnPtr.startsWith("ts_value_function(")) {
                fnPtr = fnPtr.replace(/^ts_value_function\(/, "").replace(/\)$/, "");
              }
              const castType = expectedType.includes("(*)")
                ? expectedType
                : "Value (*)(TSArray*)";
              return `((${castType})${fnPtr})`;
            }
            if (expectedType === "TSString*" || (!expectedType && a.kind === "string_literal")) {
              if (a.kind === "string_literal") return emitted;
              if (emitted.startsWith("ts_string_new(") || emitted.startsWith("ts_to_string(") ||
                  emitted.startsWith("ts_string_concat(")) return emitted;
              if (emitted.startsWith("ts_value_string(")) return emitted.replace(/^ts_value_string\((.+)\)$/, "$1");
              return `ts_to_string(${emitted})`;
            }
            if (expectedType === "TSArray*") {
              if (emitted.startsWith("ts_value_array(")) return emitted.replace(/^ts_value_array\((.+)\)$/, "$1");
              if (emitted.startsWith("ts_array_") || emitted.startsWith("ts_string_split(")) return emitted;
              if (emitted.startsWith("node_process_argv(") || emitted.startsWith("node_") ||
                  emitted.startsWith("ts_value_") || emitted.startsWith("ts_hashmap_get(")) {
                return `((TSArray*)${emitted}.as.object)`;
              }
              return `((TSArray*)${emitted}.as.object)`;
            }
            if (expectedType === "Value" || (!expectedType && (a.kind === "object_literal" || a.kind === "undefined_literal"))) {
              if (emitted.startsWith("ts_value_") || emitted.startsWith("node_")) return emitted;
              if (a.kind === "string_literal") return `ts_value_string(${emitted})`;
              if (a.kind === "undefined_literal" || a.kind === "null_literal") return emitted;
              if (emitted.startsWith("ts_string_new(") || emitted.startsWith("ts_string_concat(") ||
                  emitted.startsWith("ts_to_string(") || emitted.includes("/*__ts_str*/")) {
                return `ts_value_string(${emitted})`;
              }
            }
            // Untyped: still coerce node_process_argv for parse()
            if (methodName === "parse" || methodName === "parseAsync") {
              if (idx === 0 && (emitted.startsWith("node_process_argv(") || emitted.startsWith("node_"))) {
                return `((TSArray*)${emitted}.as.object)`;
              }
            }
            if (a.kind === "string_literal") return emitted; // keep TSString* for untyped
            return emitted;
          });
          while (args.length < expectedParamTypes.length) {
            args.push(this.defaultPadForType(expectedParamTypes[args.length]));
          }
          // When no signature, still avoid over-padding (opts() has 0 args)
          const allArgsStr = args.join(", ");
          const allArgs = allArgsStr ? `${selfCast}, ${allArgsStr}` : selfCast;
          // Sanitize reserved method names
          const cMethod = methodName === "default" ? "$default" : methodName;
          return `${className}_${cMethod}(${allArgs})`;
        }
      }

      // Date method calls - e.g., d1.toISOString() → date_toISOString_ts(d1)
      const dateMethods = ["toISOString", "toDateString", "toTimeString", "toLocaleString",
                           "getFullYear", "getMonth", "getDate", "getDay",
                           "getHours", "getMinutes", "getSeconds", "getMilliseconds", "getTime"];
      if (dateMethods.includes(methodName)) {
        // Extract the date value from the object
        let dateArg: string;
        if (callee.object.kind === "identifier") {
          dateArg = callee.object.name;
        } else if (callee.object.kind === "new_expression") {
          // new Date(x) → extract x
          dateArg = this.emit(callee.object.arguments?.[0] || { kind: "number_literal", value: 0 });
        } else {
          dateArg = this.emit(callee.object);
        }
        const extraArgs = (node.arguments || []).map((a: CNode) => this.emit(a)).join(", ");
        const allArgs = extraArgs ? `${dateArg}, ${extraArgs}` : dateArg;
        return `date_${methodName}_ts(${allArgs})`;
      }

      // Promise.then / .catch / .finally
      if (methodName === "then" || methodName === "catch" || methodName === "finally" ||
          /Promise/i.test(String(callee.checkerTypeName || "")) ||
          /Promise/i.test(String(varType || ""))) {
        const obj = this.emit(callee.object);
        const args = (node.arguments || []).map((a: CNode) => this.emit(a));
        if (methodName === "then") {
          const a0 = args[0] || "ts_value_undefined()";
          const a1 = args[1] || "ts_value_undefined()";
          return `ts_promise_then(${obj}, ${a0}, ${a1})`;
        }
        if (methodName === "catch") {
          const a0 = args[0] || "ts_value_undefined()";
          return `ts_promise_catch(${obj}, ${a0})`;
        }
        if (methodName === "finally") {
          const a0 = args[0] || "ts_value_undefined()";
          return `ts_promise_finally(${obj}, ${a0})`;
        }
        // Other methods on Promise-typed receiver: no-op
        return `((void)(${obj}), ts_value_undefined())`;
      }

      // Function-pointer fields: opt.parseArg(value, prev) → opt->parseArg(value, prev)
      // NOT Option_parseArg(opt, value, prev) which doesn't exist
      const functionPointerFields = new Set([
        "parseArg", "_action", "_argParser", "writeOut", "writeErr",
      ]);
      if (functionPointerFields.has(methodName) &&
          (varType && varType.endsWith("*") && !varType.startsWith("TS") ||
           varType === "Value" || objectName)) {
        const selfExpr = objectName
          ? (varType === "Value" ? `((${(callee.checkerTypeName || "Option").replace(/\*$/, "")}*)${objectName}.as.object)` : objectName)
          : this.emit(callee.object);
        const callArgs = (node.arguments || []).map((a: CNode) => {
          const e = this.emit(a);
          // parseArg(string, Value) — keep string as TSString*, previous as Value
          return e;
        });
        // For Value-typed self, extract pointer
        let receiver = selfExpr;
        if (varType === "Value" && objectName) {
          const cls = (callee.checkerTypeName || "Option").replace(/\*$/, "").replace(/<.*>/, "");
          if (!/Promise/i.test(cls)) {
            receiver = `((${cls}*)${objectName}.as.object)`;
          }
        }
        // Field name may be sanitized (parseArg stays parseArg)
        const field = methodName === "default" ? "default_" : methodName;
        if (callArgs.length === 0) {
          return `${receiver}->${field}()`;
        }
        // _action is ActionHandler = (...args: any[]) — variadic, must use generic Value(*)() cast
        // to avoid argument-count mismatch with the declared Value (*)(TSArray*) type
        if (methodName === "_action") {
          const rawArgs = callArgs.map((e: string, i: number) => {
            // Wrap args as Value for the generic function pointer call
            const a = node.arguments?.[i];
            if (e.startsWith("ts_value_") || e.startsWith("ts_null(") || e.startsWith("ts_undefined(")) return e;
            if (a?.kind === "string_literal") return `ts_value_string(${e})`;
            if (a?.kind === "number_literal") return `ts_value_number(${e})`;
            if (a?.kind === "boolean_literal") return `ts_value_boolean(${e})`;
            if (a?.kind === "identifier") {
              const t = this.varTypes.get(a.name);
              if (t === "TSString*" || t === "string") return `ts_value_string(${e})`;
              if (t === "TSArray*") return `ts_value_array(${e})`;
              if (t === "double" || t === "number") return `ts_value_number(${e})`;
              if (t === "int" || t === "boolean") return `ts_value_boolean(${e})`;
              if (t === "Value") return e;
              if (t && t.endsWith("*") && !t.startsWith("TS")) return `ts_value_object((void*)${e})`;
            }
            if (e.startsWith("ts_array_") || e.includes("ts_array_new")) return `ts_value_array(${e})`;
            if (e.startsWith("ts_string_") || e.startsWith("ts_to_string(")) return `ts_value_string(${e})`;
            return e;
          });
          return `(((Value(*)())(${receiver}->${field}))(${rawArgs.join(", ")}))`;
        }
        // Wrap args appropriately for known signatures
        const finalArgs = callArgs.map((e: string, i: number) => {
          if (methodName === "parseArg") {
            // (TSString*, Value)
            if (i === 0) {
              if (e.startsWith("ts_string_") || e.startsWith("ts_to_string(") || e.includes("/*__ts_str*/")) return e;
              return e.startsWith("ts_value_") ? `ts_to_string(${e})` : e;
            }
            // Value previous
            if (e.startsWith("ts_value_") || e.startsWith("ts_hashmap_get(") || e.startsWith("node_")) return e;
            if (e.startsWith("ts_string_")) return `ts_value_string(${e})`;
            return e;
          }
          return e;
        });
        return `${receiver}->${field}(${finalArgs.join(", ")})`;
      }

      if (varType && varType.endsWith("*") && !varType.startsWith("TS") && !/Promise/i.test(varType)) {
        // This looks like a class pointer type (e.g., Command*)
        const className = varType.replace("*", "");
        // Prefer hardcoded signatures for known methods — checker overloads often wrong.
        const knownTypes = this.knownClassMethodParamTypes(className, methodName);
        let expectedParamTypes = knownTypes.length > 0
          ? knownTypes
          : this.parseExpectedParamTypes(callee.propertyCType);
        const args = (node.arguments || []).map((a: CNode, idx: number) => {
          const emitted = this.emit(a);
          const expectedType = idx < expectedParamTypes.length ? expectedParamTypes[idx] : "";
          // Function-pointer params (action/hook callbacks): unwrap ts_value_function
          // and cast to the expected C function-pointer type.
          if (expectedType.includes("(*)") || methodName === "action" || (methodName === "hook" && idx === 1)) {
            let fnPtr = emitted;
            if (fnPtr.startsWith("ts_value_function((void*)")) {
              fnPtr = fnPtr.replace(/^ts_value_function\(\(void\*\)/, "").replace(/\)$/, "");
            } else if (fnPtr.startsWith("ts_value_function(")) {
              fnPtr = fnPtr.replace(/^ts_value_function\(/, "").replace(/\)$/, "");
            }
            const castType = expectedType.includes("(*)")
              ? expectedType
              : "Value (*)(TSArray*)";
            return `((${castType})${fnPtr})`;
          }
          if (expectedType === "TSString*" ||
              // Known Command methods with string params even without signature
              ((methodName === "_exit" || methodName === "exit") && idx >= 1)) {
            if (a.kind === "string_literal") return emitted;
            if (emitted.startsWith("ts_string_new(") || emitted.startsWith("ts_string_concat(") ||
                emitted.startsWith("ts_to_string(") || emitted.startsWith("ts_url_") ||
                emitted.startsWith("ts_string_") || emitted.includes("/*__ts_str*/") ||
                emitted.startsWith("ts_json_stringify")) return emitted;
            if (emitted.startsWith("ts_value_string(")) return emitted.replace(/^ts_value_string\((.+)\)$/, "$1");
            if (a.kind === "identifier") {
              const t = this.varTypes.get(a.name);
              if (t === "TSString*" || t === "string") return emitted;
            }
            // Struct string fields
            if (emitted.includes("->_version") || emitted.includes("->_name") ||
                emitted.includes("->flags") || emitted.includes("->description") ||
                emitted.includes("->message") || emitted.includes("->short_") ||
                emitted.includes("->long_")) {
              return emitted;
            }
            // Don't wrap if already TSString*-looking
            if (/^(Command|Option|Argument)_/.test(emitted)) return emitted;
            return `ts_to_string(${emitted})`;
          }
          if (expectedType === "int" || expectedType === "boolean") {
            if (a.kind === "boolean_literal" || a.kind === "number_literal") return emitted;
            if (a.kind === "identifier") {
              const t = this.varTypes.get(a.name);
              if (t === "int" || t === "boolean") return emitted;
            }
            return `ts_to_boolean(${emitted})`;
          }
          if (expectedType === "TSArray*") {
            if (emitted.startsWith("ts_value_array(")) return emitted.replace(/^ts_value_array\((.+)\)$/, "$1");
            if (a.kind === "identifier" && this.varTypes.get(a.name) === "TSArray*") return emitted;
            if (emitted.startsWith("ts_array_") || emitted.startsWith("ts_string_split(")) return emitted;
            if (emitted.startsWith("node_process_argv(") || emitted.startsWith("node_") ||
                emitted.startsWith("ts_value_") || emitted.startsWith("ts_hashmap_get(")) {
              return `((TSArray*)${emitted}.as.object)`;
            }
            return `((TSArray*)${emitted}.as.object)`;
          }
          if (expectedType === "Value") {
            if (emitted.startsWith("ts_value_") || emitted.startsWith("node_")) return emitted;
            if (a.kind === "string_literal") return `ts_value_string(${emitted})`;
            if (a.kind === "number_literal") return `ts_value_number(${emitted})`;
            if (a.kind === "boolean_literal") return `ts_value_boolean(${emitted})`;
            if (a.kind === "undefined_literal" || a.kind === "null_literal") return emitted;
            if (a.kind === "function_ref" || a.kind === "arrow_function" || a.kind === "function_expression") {
              return emitted.startsWith("ts_value_function(") ? emitted : `ts_value_function((void*)${emitted})`;
            }
            if (a.kind === "identifier") {
              const t = this.varTypes.get(a.name);
              if (t === "TSString*" || t === "string") return `ts_value_string(${emitted})`;
              if (t === "double" || t === "number") return `ts_value_number(${emitted})`;
              if (t === "int" || t === "boolean") return `ts_value_boolean(${emitted})`;
              if (t === "TSArray*") return `ts_value_array(${emitted})`;
              if (t && t.endsWith("*") && !t.startsWith("TS")) return `ts_value_object((void*)${emitted})`;
              if (t === "Value") return emitted;
            }
            if (emitted.startsWith("ts_string_new(") || emitted.startsWith("ts_string_concat(") ||
                emitted.startsWith("ts_to_string(") || emitted.includes("/*__ts_str*/")) {
              return `ts_value_string(${emitted})`;
            }
          }
          return emitted;
        });
        // Pad missing arguments with type-correct defaults
        while (args.length < expectedParamTypes.length) {
          args.push(this.defaultPadForType(expectedParamTypes[args.length]));
        }
        const allArgsStr = args.join(", ");
        const selfExpr = objectName || this.emit(callee.object);
        const allArgs = allArgsStr ? `${selfExpr}, ${allArgsStr}` : selfExpr;
        const cMethod = methodName === "default" ? "$default" : methodName;
        return `${className}_${cMethod}(${allArgs})`;
      }

      // Method dispatch for Value-typed variables backed by struct types
      // Uses checkerTypeName from the visitor to determine the actual class
      if (varType === "Value" && callee.checkerTypeName && /^[A-Z]/.test(callee.checkerTypeName) &&
          !/Promise|<|>|WritableStream|ReadableStream|TransformStream/.test(callee.checkerTypeName)) {
        const className = callee.checkerTypeName.replace(/\*$/, "").replace(/<.*>/, "");
        if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(className)) {
          // fall through — invalid C identifier from generics/unions
        } else {
        const selfCast = `((${className}*)${objectName}.as.object)`;
        // Only use expected param types if propertyCType is a real function pointer type
        const hasSignature = callee.propertyCType && callee.propertyCType.includes("(*)");
        const expectedParamTypes = hasSignature ? this.parseExpectedParamTypes(callee.propertyCType) : [];
        const args = (node.arguments || []).map((a: CNode, idx: number) => {
          const emitted = this.emit(a);
          const expectedType = expectedParamTypes.length > 0 && idx < expectedParamTypes.length ? expectedParamTypes[idx] : "";
          // If expected type is TSString*, keep as TSString* (don't wrap in Value)
          if (expectedType === "TSString*") {
            if (a.kind === "string_literal") return emitted;
            if (emitted.startsWith("ts_string_new(") || emitted.startsWith("ts_string_concat(") ||
                emitted.startsWith("ts_to_string(") || emitted.startsWith("ts_url_")) return emitted;
            if (emitted.startsWith("ts_value_string(")) return emitted.replace(/^ts_value_string\((.+)\)$/, "$1");
            if (a.kind === "identifier") {
              const t = this.varTypes.get(a.name);
              if (t === "TSString*" || t === "string") return emitted;
            }
            return `ts_to_string(${emitted})`;
          }
          if (expectedType === "int" || expectedType === "boolean") {
            if (a.kind === "boolean_literal") return emitted;
            if (a.kind === "identifier") {
              const t = this.varTypes.get(a.name);
              if (t === "int" || t === "boolean") return emitted;
            }
            if (emitted.startsWith("ts_value_")) return emitted;
            return `ts_to_boolean(${emitted})`;
          }
          if (expectedType === "double" || expectedType === "number") {
            if (a.kind === "number_literal") return emitted;
            return `ts_to_number(${emitted})`;
          }
          if (expectedType === "TSArray*") {
            if (emitted.startsWith("ts_value_array(")) return emitted.replace(/^ts_value_array\((.+)\)$/, "$1");
            return emitted;
          }
          if (expectedType === "Value") {
            // Already Value, don't re-wrap
            if (emitted.startsWith("ts_value_") || emitted.startsWith("node_")) return emitted;
          }
          // Default: wrap in Value (safe fallback when type is unknown)
          if (a.kind === "string_literal") return `ts_value_string(${emitted})`;
          if (a.kind === "number_literal") return `ts_value_number(${emitted})`;
          if (a.kind === "boolean_literal") return `ts_value_boolean(${emitted})`;
          if (a.kind === "identifier") {
            const t = this.varTypes.get(a.name);
            if (t === "TSString*" || t === "string") return `ts_value_string(${emitted})`;
            if (t === "double" || t === "number") return `ts_value_number(${emitted})`;
            if (t === "int" || t === "boolean") return `ts_value_boolean(${emitted})`;
          }
          if (emitted.startsWith("ts_string_new(") || emitted.startsWith("ts_string_concat(")) return `ts_value_string(${emitted})`;
          if (emitted.startsWith("ts_value_") || emitted.startsWith("node_")) return emitted;
          if (emitted.startsWith("ts_string_") || emitted.startsWith("ts_to_string("))
            return `ts_value_string(${emitted})`;
          return emitted;
        });
        // Only pad if we have a reliable signature — use type-correct defaults
        if (hasSignature) {
          while (args.length < expectedParamTypes.length) {
            args.push(this.defaultPadForType(expectedParamTypes[args.length]));
          }
        }
        const allArgsStr = args.join(", ");
        const allArgs = allArgsStr ? `${selfCast}, ${allArgsStr}` : selfCast;
        return `${className}_${methodName}(${allArgs})`;
        } // end valid className
      }

      // Method dispatch for Value-typed variables backed by struct types
      // e.g., program.version("1.0.0") where program is Value wrapping Command*
      // The propertyCType has the function pointer signature: ReturnType (*)(SelfType, ArgTypes...)
      if (varType === "Value" && callee.propertyCType && callee.propertyCType.includes("(*)")) {
        const match = callee.propertyCType.match(/\(([^)]*)\s*\(\*\)\s*\(([^)]*)\)/);
        if (match) {
          const paramTypes = this.splitCParamList(match[2]);
          const selfType = paramTypes[0]; // e.g., "Command*"
          const className = selfType?.replace(/\s*\*$/, "").trim();
          if (className && /^[A-Z]/.test(className) && !className.startsWith("TS")) {
            // Cast self from Value to struct pointer
            const selfCast = `((${selfType})${objectName}.as.object)`;
            const hasSignature = callee.propertyCType && callee.propertyCType.includes("(*)") && callee.propertyCType !== "Value";
            const expectedParamTypes = hasSignature ? this.parseExpectedParamTypes(callee.propertyCType) : [];
            const args = (node.arguments || []).map((a: CNode, idx: number) => {
              const emitted = this.emit(a);
              const expectedType = idx < expectedParamTypes.length ? expectedParamTypes[idx] : "Value";
              // If expected type is TSString*, keep as TSString* (don't wrap in Value)
              if (expectedType === "TSString*") {
                if (a.kind === "string_literal") return emitted;
                if (emitted.startsWith("ts_string_new(") || emitted.startsWith("ts_string_concat(") ||
                    emitted.startsWith("ts_to_string(") || emitted.startsWith("ts_url_")) return emitted;
                if (emitted.startsWith("ts_value_string(")) return emitted.replace(/^ts_value_string\((.+)\)$/, "$1");
                if (a.kind === "identifier") {
                  const t = this.varTypes.get(a.name);
                  if (t === "TSString*" || t === "string") return emitted;
                }
                return `ts_to_string(${emitted})`;
              }
              if (expectedType === "int" || expectedType === "boolean") {
                if (a.kind === "boolean_literal") return emitted;
                if (a.kind === "identifier") {
                  const t = this.varTypes.get(a.name);
                  if (t === "int" || t === "boolean") return emitted;
                }
                if (emitted.startsWith("ts_value_")) return `ts_to_boolean(${emitted})`;
                return `ts_to_boolean(${emitted})`;
              }
              if (expectedType === "double" || expectedType === "number") {
                if (a.kind === "number_literal") return emitted;
                return `ts_to_number(${emitted})`;
              }
              if (expectedType === "TSArray*") {
                if (emitted.startsWith("ts_value_array(")) return emitted.replace(/^ts_value_array\((.+)\)$/, "$1");
                if (a.kind === "identifier" && this.varTypes.get(a.name) === "TSArray*") return emitted;
                if (emitted.startsWith("ts_array_") || emitted.startsWith("ts_string_split(")) return emitted;
                // node_process_argv() and other Value-returning builtins
                if (emitted.startsWith("node_process_argv(") || emitted.startsWith("node_") ||
                    emitted.startsWith("ts_value_") || emitted.startsWith("ts_hashmap_get(")) {
                  return `((TSArray*)${emitted}.as.object)`;
                }
                // Value wrapping array
                return `((TSArray*)${emitted}.as.object)`;
              }
              // Default: wrap in Value
              if (a.kind === "string_literal") return `ts_value_string(${emitted})`;
              if (a.kind === "number_literal") return `ts_value_number(${emitted})`;
              if (a.kind === "boolean_literal") return `ts_value_boolean(${emitted})`;
              if (a.kind === "identifier") {
                const t = this.varTypes.get(a.name);
                if (t === "TSString*" || t === "string") return `ts_value_string(${emitted})`;
                if (t === "double" || t === "number") return `ts_value_number(${emitted})`;
                if (t === "int" || t === "boolean") return `ts_value_boolean(${emitted})`;
                if (t && t.endsWith("*") && !t.startsWith("TS")) return `ts_value_object((void*)${emitted})`;
                if (t === "Value") return emitted;
                if (t === "TSArray*") return `ts_value_array(${emitted})`;
              }
              if (emitted.startsWith("ts_string_new(") || emitted.startsWith("ts_string_concat(")) return `ts_value_string(${emitted})`;
              if (emitted.startsWith("ts_value_") || emitted.startsWith("node_")) return emitted;
              return `ts_value_string(ts_to_string(${emitted}))`;
            });
            // Pad missing arguments with type-correct defaults
            while (args.length < expectedParamTypes.length) {
              args.push(this.defaultPadForType(expectedParamTypes[args.length]));
            }
            const allArgsStr = args.join(", ");
            const allArgs = allArgsStr ? `${selfCast}, ${allArgsStr}` : selfCast;
            return `${className}_${methodName}(${allArgs})`;
          }
        }
      }
    }

    // super() call — base class initialization, skip (fields set by this.field = value)
    if (callee.kind === "identifier" && callee.name === "super") {
      return "/* super() skipped */";
    }

    // Hashmap .get(key) / .set(key, value) / .has(key) on property_access OR Value identifiers
    if (callee.kind === "property_access" &&
        (callee.property === "get" || callee.property === "set" || callee.property === "has")) {
      const objIsMap =
        callee.object.kind === "property_access" ||
        (callee.object.kind === "identifier" && (
          this.varTypes.get(callee.object.name) === "Value" ||
          this.varTypes.get(callee.object.name) === "TSHashMap*" ||
          /map|opts|config|values|option/i.test(callee.object.name)
        ));
      if (objIsMap) {
        const obj = this.emit(callee.object);
        // Value-typed map variables: optionValues is Value wrapping TSHashMap*
        const objName = callee.object.kind === "identifier" ? callee.object.name : "";
        const objType = objName ? this.varTypes.get(objName) : undefined;
        let mapExpr: string;
        if (objType === "TSHashMap*") {
          mapExpr = obj;
        } else if (obj.startsWith("ts_hashmap_get(") || obj.startsWith("ts_value_") ||
                   objType === "Value" || obj.includes("->")) {
          mapExpr = `((TSHashMap*)${obj}.as.object)`;
        } else {
          // Bare identifier of Value (e.g. optionValues) — always go through .as.object
          mapExpr = `((TSHashMap*)${obj}.as.object)`;
        }
        const asKey = (raw: string, argNode?: CNode): string => {
          if (raw.startsWith("ts_string_new(") || raw.startsWith("ts_to_string(") ||
              raw.startsWith("ts_string_concat(") || raw.includes("/*__ts_str*/") ||
              raw.startsWith("ts_string_") || /^(Command|Option|Argument)_/.test(raw) ||
              raw.startsWith("camelcase(") || raw.startsWith("formatOptionFlags(") ||
              raw.startsWith("src_cli_commander_option_camelcase(")) return raw;
          if (raw.startsWith("ts_value_string(")) return raw.replace(/^ts_value_string\((.+)\)$/, "$1");
          // Strip accidental ts_to_string around TSString*
          const stripped = raw.replace(/^ts_to_string\((.+)\)$/, "$1");
          if (stripped !== raw && (
              stripped.startsWith("ts_string_") || /^(Command|Option|Argument)_/.test(stripped) ||
              stripped.startsWith("camelcase(") || stripped.includes("/*__ts_str*/"))) {
            return stripped;
          }
          if (argNode?.kind === "identifier") {
            const t = this.varTypes.get(argNode.name);
            if (t === "TSString*" || t === "string") return raw;
            if (t === "Value") return `ts_to_string(${raw})`;
          }
          if (argNode?.kind === "call_expression") {
            // Option_attributeName(opt) etc. already TSString*
            if (/^(Option|Command|Argument)_/.test(raw) || raw.startsWith("camelcase(") ||
                raw.startsWith("src_cli_commander_option_camelcase(")) return raw;
          }
          // Don't wrap TSString* identifiers
          if (argNode?.kind === "identifier" && this.varTypes.get(argNode.name) === "TSString*") return raw;
          // Only wrap if it looks like Value
          if (raw.startsWith("ts_hashmap_get(") || raw.startsWith("ts_value_") || raw.startsWith("ts_array_get(")) {
            return `ts_to_string(${raw})`;
          }
          // Default: if already looks like pointer/string expr, leave
          if (raw.includes("->") || raw.startsWith("(")) return raw;
          return raw; // assume TSString* for attr etc.
        };
        if (callee.property === "get") {
          const key = node.arguments?.[0] ? this.emit(node.arguments[0]) : 'ts_string_new("")';
          const getMap = objType === "TSHashMap*" ? obj :
            (mapExpr.includes(".as.object") ? mapExpr : `((TSHashMap*)${obj}.as.object)`);
          return `ts_hashmap_get(${getMap}, ${asKey(key, node.arguments?.[0])})`;
        }
        if (callee.property === "has") {
          const key = node.arguments?.[0] ? this.emit(node.arguments[0]) : 'ts_string_new("")';
          // Force .as.object for Value maps (never cast Value struct directly)
          const safeMap = objType === "TSHashMap*" ? obj :
            (mapExpr.includes(".as.object") ? mapExpr : `((TSHashMap*)${obj}.as.object)`);
          return `ts_hashmap_has(${safeMap}, ${asKey(key, node.arguments?.[0])})`;
        }
        if (callee.property === "set") {
          const key = node.arguments?.[0] ? this.emit(node.arguments[0]) : 'ts_string_new("")';
          let val = node.arguments?.[1] ? this.emit(node.arguments[1]) : 'ts_value_null()';
          if (!val.startsWith("ts_value_") && !val.startsWith("ts_null(") && !val.startsWith("ts_undefined(")) {
            if (node.arguments?.[1]?.kind === "string_literal") val = `ts_value_string(${val})`;
            else if (node.arguments?.[1]?.kind === "number_literal") val = `ts_value_number(${val})`;
            else if (node.arguments?.[1]?.kind === "boolean_literal") val = `ts_value_boolean(${val})`;
            else if (node.arguments?.[1]?.kind === "identifier") {
              const t = this.varTypes.get(node.arguments[1].name);
              if (t === "TSString*" || t === "string") val = `ts_value_string(${val})`;
              else if (t === "TSArray*") val = `ts_value_array(${val})`;
              else if (t === "double" || t === "number") val = `ts_value_number(${val})`;
              else if (t === "int" || t === "boolean") val = `ts_value_boolean(${val})`;
              else if (t && t.endsWith("*") && !t.startsWith("TS")) val = `ts_value_object((void*)${val})`;
              else if (t === "Value") { /* keep */ }
              else if (!t && /^[a-z]/.test(node.arguments[1].name)) {
                // untyped identifier — if looks like struct pointer name (opt, cmd, arg)
                if (/^(opt|cmd|arg|self)$/i.test(node.arguments[1].name) ||
                    this.varTypes.get(node.arguments[1].name)?.endsWith("*")) {
                  const vt = this.varTypes.get(node.arguments[1].name);
                  if (vt && vt.endsWith("*") && !vt.startsWith("TS")) {
                    val = `ts_value_object((void*)${val})`;
                  }
                }
              }
            } else if (val.startsWith("ts_string_") || val.startsWith("ts_to_string(") ||
                       val.includes("/*__ts_str*/") || val.includes("->short_") ||
                       val.includes("->long_") || val.includes("->flags")) {
              val = `ts_value_string(${val})`;
            } else if (val.includes("->") === false && /^[a-zA-Z_][\w]*$/.test(val)) {
              // bare identifier that might be Option*
              const t = this.varTypes.get(val);
              if (t && t.endsWith("*") && !t.startsWith("TS")) {
                val = `ts_value_object((void*)${val})`;
              }
            }
          }
          // Direct TSHashMap* receiver (map.set) — mapExpr may already be bare
          const setMap = objType === "TSHashMap*" ? obj :
            (mapExpr.includes(".as.object") ? mapExpr : `((TSHashMap*)${obj}.as.object)`);
          // Also handle when object is identifier typed TSHashMap*
          const finalMap = (objType === "TSHashMap*" || obj.startsWith("ts_hashmap_new("))
            ? obj
            : setMap;
          return `ts_hashmap_set(${finalMap}, ${asKey(key, node.arguments?.[0])}, ${val})`;
        }
        if (callee.property === "get") {
          // already handled above — but ensure Option* cast when assigned
        }
      }
    }

    // `in` operator: key in obj → ts_hashmap_has
    // Handled as binary in some paths; also property "in" isn't a method.
    // Element access comparison optionValues[attr] == 1 is handled in emitBinary/emitElementAccess

    const calleeStr = this.emit(callee);
    // Call through Value-held function: ts_hashmap_get(...)(args) or Value identifier hook(...)
    if (calleeStr.startsWith("ts_hashmap_get(") ||
        (callee.kind === "identifier" && this.varTypes.get(callee.name) === "Value" &&
         !calleeStr.startsWith("ts_") && !calleeStr.startsWith("node_") &&
         !calleeStr.startsWith("Command_") && !calleeStr.startsWith("Option_") &&
         !calleeStr.startsWith("Argument_") && !calleeStr.startsWith("src_"))) {
      const rawArgs = (node.arguments || []).map((a: CNode) => {
        const e = this.emit(a);
        // Wrap concrete types into Value for ActionHandler-style callbacks
        if (e.startsWith("ts_value_") || e.startsWith("ts_null(") || e.startsWith("ts_undefined(")) return e;
        if (a.kind === "string_literal") return `ts_value_string(${e})`;
        if (a.kind === "number_literal") return `ts_value_number(${e})`;
        if (a.kind === "boolean_literal") return `ts_value_boolean(${e})`;
        if (a.kind === "identifier") {
          const t = this.varTypes.get(a.name);
          if (t === "TSString*" || t === "string") return `ts_value_string(${e})`;
          if (t === "TSArray*") return `ts_value_array(${e})`;
          if (t === "double" || t === "number") return `ts_value_number(${e})`;
          if (t === "int" || t === "boolean") return `ts_value_boolean(${e})`;
          if (t === "Value") return e;
          if (t && t.endsWith("*") && !t.startsWith("TS")) return `ts_value_object((void*)${e})`;
        }
        if (e.startsWith("ts_array_") || e.includes("ts_array_new") || e.includes("__sp_dst") || e.includes("__sl_dst")) {
          return `ts_value_array(${e})`;
        }
        if (e.startsWith("ts_string_") || e.startsWith("ts_to_string(")) return `ts_value_string(${e})`;
        return e;
      });
      // writeOut/writeErr style: void(*)(TSString*) — pass TSString*, not Value
      if (calleeStr.startsWith("ts_hashmap_get(") && rawArgs.length === 1) {
        let a0 = rawArgs[0];
        // Unwrap accidental ts_value_string(TSString*)
        if (a0.startsWith("ts_value_string(")) {
          a0 = a0.replace(/^ts_value_string\((.+)\)$/, "$1");
        }
        // Keep TSString* producers as-is
        if (!(a0.startsWith("ts_string_") || a0.startsWith("ts_to_string(") ||
              a0.includes("/*__ts_str*/") || /^(Command|Option|Argument)_/.test(a0) ||
              a0.startsWith("src_cli_") || a0.includes("->") || a0 === "str")) {
          // if it's a Value, convert
          if (a0.startsWith("ts_value_") || a0.startsWith("ts_hashmap_get(")) {
            a0 = `ts_to_string(${a0})`;
          }
        }
        return `(((void(*)(TSString*))(${calleeStr}).as.function)(${a0}))`;
      }
      // Zero-arg: TAG_FUNCTION call, or StreamBody identity (destructured getWriter)
      if (rawArgs.length === 0) {
        return `ts_value_call0(${calleeStr})`;
      }
      // ActionHandler: Value(*)(TSArray*) roughly — use variadic cast
      return `(((Value(*)())(${calleeStr}).as.function)(${rawArgs.join(", ")}))`;
    }
    // Imported function call with optional args (formatHelp(cmd) → pad config)
    if (callee.kind === "identifier") {
      let callName = calleeStr;
      // Resolve imported symbol mangled name
      const imported = this.importedSymbols.get(callee.name);
      if (imported) callName = imported;
      let args = (node.arguments || []).map((a: CNode) => this.emit(a));
      // formatHelp(command, config?) — pad missing config
      if (callName.includes("formatHelp") || callee.name === "formatHelp") {
        while (args.length < 2) args.push("ts_value_undefined()");
        // Ensure first arg is Value
        if (args[0] && !args[0].startsWith("ts_value_") &&
            (args[0] === "self" || args[0].startsWith("(("))) {
          if (args[0] === "self") {
            args[0] = `((Value){.tag = TAG_OBJECT, .as.object = self})`;
          }
        }
      }
      // Local same-module helpers: formatOptionFlags → mangled name
      if (callee.name === "formatOptionFlags" || callee.name === "formatArguments" ||
          callee.name === "formatHelp" || callee.name === "camelcase") {
        // Prefer mangled if available via imports; else prefix current module heuristically
        if (!imported) {
          // Try common mangling: src_cli_commander_help_formatOptionFlags
          if (callee.name === "formatOptionFlags") {
            callName = "src_cli_commander_help_formatOptionFlags";
          } else if (callee.name === "formatArguments") {
            callName = "src_cli_commander_help_formatArguments";
          } else if (callee.name === "formatHelp") {
            callName = "src_cli_commander_help_formatHelp";
          }
          // camelcase stays unmangled when defined in same file
        }
      }
      // Coerce string args for known helpers
      if (callName.includes("camelcase") || callee.name === "camelcase") {
        args = args.map((a: string) => {
          if (a.startsWith("ts_string_") || a.startsWith("ts_to_string(") ||
              a.includes("/*__ts_str*/") || a.startsWith("ts_string_replace(")) return a;
          return a;
        });
      }
      return `${callName}(${args.join(", ")})`;
    }
    const args = (node.arguments || []).map((a: CNode) => this.emit(a)).join(", ");
    return `${calleeStr}(${args})`;
  }

  private emitPropertyAccess(node: CNode): string {

    const object = this.emit(node.object);

    // Special handling for Buffer static methods (Buffer.from, Buffer.alloc, etc.)
    if ((node.object.kind === "string_literal" && node.object.value === "Buffer") ||
        (node.object.kind === "identifier" && node.object.name === "Buffer")) {
      // Map method names to C function names
      const methodMap: Record<string, string> = {
        from: "ts_buffer_from_string",
        alloc: "ts_buffer_alloc",
        allocUnsafe: "ts_buffer_allocUnsafe",
        isBuffer: "ts_buffer_isBuffer",
        concat: "ts_buffer_concat",
      };
      return methodMap[node.property] || `ts_buffer_${node.property}`;
    }

    // Array.isArray as bare property (before call) — return a marker; emitCall handles the call
    if (node.object.kind === "identifier" &&
        (node.object.name === "Array" || node.object.name === "Array_") &&
        node.property === "isArray") {
      return "/*Array_isArray*/";
    }

    // WritableStream.getWriter as property (destructuring / bound method)
    if (node.property === "getWriter") {
      return `ts_writable_stream_get_writer(${object})`;
    }
    // Math.max / Math.min as property — emitCall will handle Math.max(...)
    if (node.object.kind === "identifier" && node.object.name === "Math") {
      return `ts_math_${node.property}`;
    }

    // Namespace import property access: `import * as X from "..."` → X.prop resolves to mangled name
    // e.g., commander.program → src_cli_commander_index_program
    // Node builtin: `import * as wt from "worker_threads"` → node:worker_threads → node_worker_threads_X
    if (node.object.kind === "identifier") {
      const nsModulePath = this.namespaceModulePaths.get(node.object.name);
      if (nsModulePath) {
        if (nsModulePath.startsWith("node:")) {
          const mod = nsModulePath.slice("node:".length);
          const funcName = `node_${mod}_${node.property}`;
          this._lastBuiltinCall = funcName;
          const zeroArgGetters = new Set([
            "argv", "env", "pid", "platform", "hostname", "totalmem", "freemem", "arch",
            "EOL", "devNull", "stdin", "stdout", "stderr",
            "defaultMaxListeners",
            "isMainThread", "parentPort", "workerData", "threadId", "threadName",
            "isInternalThread", "SHARE_ENV", "resourceLimits", "locks",
          ]);
          if (zeroArgGetters.has(node.property)) {
            return `${funcName}()`;
          }
          if ((node.property === "EventEmitter" && mod === "events") ||
              (node.property === "Worker" && mod === "worker_threads") ||
              (node.property === "MessageChannel" && mod === "worker_threads") ||
              (node.property === "MessagePort" && mod === "worker_threads") ||
              (node.property === "BroadcastChannel" && mod === "worker_threads")) {
            return funcName;
          }
          return funcName;
        }
        const prefix = filePathToMangledPrefix(nsModulePath);
        const mangledName = `${prefix}_${node.property}`;
        return mangledName;
      }
    }

    // Special handling for Node built-in module namespace imports
    // e.g., fs.readFileSync → node_fs_readFileSync
    // Zero-arg getters used as properties (process.argv, process.pid, os.platform)
    // must emit as function calls: node_process_argv()
    if (node.object.kind === "identifier") {
      const moduleName = node.object.name;
      const builtinModules = ["fs", "path", "process", "os", "http", "net", "child_process", "events", "readline", "assert", "crypto", "worker_threads"];
      if (builtinModules.includes(moduleName)) {
        const funcName = `node_${moduleName}_${node.property}`;
        this._lastBuiltinCall = funcName;
        // Properties that are zero-arg C functions — call them immediately
        // (method calls like process.cwd() go through emitCall instead)
        // Zero-arg property getters (no call parens in TS, but C functions)
        const zeroArgGetters = new Set([
          "argv", "env", "pid", "platform", "hostname", "totalmem", "freemem", "arch",
          "EOL", "devNull", "stdin", "stdout", "stderr",
          "defaultMaxListeners",
          "isMainThread", "parentPort", "workerData", "threadId", "threadName",
          "isInternalThread", "SHARE_ENV", "resourceLimits", "locks",
        ]);
        if (zeroArgGetters.has(node.property)) {
          return `${funcName}()`;
        }
        // EventEmitter as bare property (events.EventEmitter) — return ctor function name;
        // new events.EventEmitter() is handled in emitNew.
        if (node.property === "EventEmitter" && moduleName === "events") {
          return `node_events_EventEmitter`;
        }
        if (moduleName === "worker_threads" &&
            (node.property === "Worker" || node.property === "MessageChannel" ||
             node.property === "MessagePort" || node.property === "BroadcastChannel")) {
          return funcName;
        }
        return funcName;
      }
    }

    // Built-in property access
    if (node.property === "length") {
      if (node.objectType === "string") return `${object}->length`;
      if (node.objectType === "array") return `${object}->length`;
      // Value wrapping string/array/buffer, or TSString*/TSArray* identifiers
      if (node.object.kind === "identifier") {
        const ot = this.varTypes.get(node.object.name);
        if (ot === "TSString*" || ot === "string" || ot === "TSArray*" || ot === "array") {
          return `${object}->length`;
        }
        if (ot === "Value") {
          // Buffer instances are Value — use buffer length API when name suggests buffer
          // (must run before array cast; Buffer is not TSArray)
          if (/buf|buffer|file|chunk|data|body|blob/i.test(node.object.name)) {
            return `ts_buffer_length(${object})`;
          }
          // Prefer array length via as.object (arrays store length on TSArray)
          return `((TSArray*)${object}.as.object)->length`;
        }
      }
      // Struct field of type TSArray* (self->commands, self->options, …)
      if (node.object.kind === "property_access") {
        const field = node.object.property;
        if (["options", "commands", "arguments", "args", "processedArgs",
             "_aliases", "_preActionHooks", "_postActionHooks", "_conflicts",
             "argChoices"].includes(field || "")) {
          return `${object}->length`;
        }
        // Value field used as array — rare
        if (object.includes("->") && !object.startsWith("ts_")) {
          // Could be TSArray* field already emitted as self->commands
          return `${object}->length`;
        }
      }
      // ts_hashmap_get / Value expressions used as arrays — but prefer
      // struct field access when object is already a casted Class*
      // e.g. ((Command*)command.as.object)->commands  is NOT a Value
      if ((object.startsWith("ts_hashmap_get(") || object.startsWith("ts_value_")) &&
          !object.includes("->")) {
        return `((TSArray*)${object}.as.object)->length`;
      }
      // ((Command*)x.as.object) property already handled as struct via objectType
      // Fall through: if object looks like a cast to struct pointer, use ->length
      if (/\(\([A-Z][A-Za-z0-9_]*\*\)/.test(object) || object.includes("->")) {
        // Will be handled by struct field path below or default
      }
      // ts_to_string / string-producing expressions
      if (object.startsWith("ts_to_string(") || object.startsWith("ts_string_") ||
          object.includes("/*__ts_str*/") ||
          /^(Command|Option|Argument)_\w+\(/.test(object)) {
        return `${object}->length`;
      }
    }

    // Date method calls - return function names for method calls
    const dateMethods = ["toISOString", "toDateString", "toTimeString", "toLocaleString",
                         "getFullYear", "getMonth", "getDate", "getDay",
                         "getHours", "getMinutes", "getSeconds", "getMilliseconds", "getTime"];
    if (dateMethods.includes(node.property)) {
      return `date_${node.property}`;
    }

    // Response object properties (FetchResponse) — helpers accept Value and check type_tag
    // Prefer helpers for known response property names on identifiers (getRes.status, etc.)
    // Skip nested access like req.headers.host
    const isValueObject =
      node.objectType === "any" ||
      node.objectType === "unknown" ||
      (node.object.kind === "identifier" && this.varTypes.get(node.object.name) === "Value") ||
      (node.object.kind === "property_access"); // nested access like req.headers.host

    // res.body → stream-like Value wrapping the response body
    if (node.property === "body" && node.object.kind === "identifier") {
      const objType = this.varTypes.get(node.object.name);
      if (objType === "Value" || /res|response/i.test(node.object.name)) {
        return `ts_fetch_response_body(${object})`;
      }
    }

    if (node.object.kind === "identifier" || !isValueObject) {
      if (node.property === "status") {
        return `ts_fetch_response_status(${object})`;
      }
      if (node.property === "statusText") {
        return `ts_fetch_response_statusText(${object})`;
      }
      // .url on a top-level Value is usually a FetchResponse (not IncomingMessage nested)
      if (node.property === "url" && node.object.kind === "identifier") {
        const objName = node.object.name;
        const objType = this.varTypes.get(objName);
        // IncomingMessage params are often named req — keep hashmap for those
        if (/^req$/i.test(objName) || objName === "request" || objName === "incoming") {
          // fall through to hashmap get below
        } else if (objType === "Value") {
          return `ts_fetch_response_url(${object})`;
        } else if (objType && objType !== "Value") {
          return `ts_fetch_response_url(${object})`;
        } else if (!objType && /res|response/i.test(objName) && !/^req/i.test(objName)) {
          return `ts_fetch_response_url(${object})`;
        }
        // unknown Value / req → hashmap
      }
      if (node.property === "url" && !isValueObject &&
          !(node.object.kind === "identifier" && /^req$/i.test(node.object.name))) {
        return `ts_fetch_response_url(${object})`;
      }
    }

    // Blob properties (only when object looks like a Blob, not Event.type)
    if (node.property === "size") {
      const on = node.object.kind === "identifier" ? node.object.name : "";
      const ot = node.object.kind === "identifier" ? this.varTypes.get(on) : undefined;
      if (/blob/i.test(on) || /Blob/i.test(String(node.objectType || "")) ||
          (ot === "Value" && /blob/i.test(on))) {
        return `ts_blob_size(${object})`;
      }
    }
    if (node.property === "type") {
      const on = node.object.kind === "identifier" ? node.object.name : "";
      const ot = node.object.kind === "identifier" ? this.varTypes.get(on) : undefined;
      // Prefer Blob only for clear blob receivers; Event/WebSocketEvent use hashmap "type"
      if (/blob/i.test(on) || /Blob/i.test(String(node.objectType || ""))) {
        return `ts_blob_type(${object})`;
      }
      // Value event objects: event.type → hashmap
      if (ot === "Value" || /event|ev|err|error/i.test(on) ||
          /WebSocketEvent|Event/i.test(String(node.objectType || ""))) {
        // fall through to hashmap get below
      } else if (ot && ot !== "Value") {
        return `ts_blob_type(${object})`;
      }
    }

    // WebSocket readyState / event handler getters
    if (node.property === "readyState") {
      const ot = node.object.kind === "identifier" ? this.varTypes.get(node.object.name) : undefined;
      const on = node.object.kind === "identifier" ? node.object.name : "";
      if (ot === "Value" || /ws|socket|wss/i.test(on) || /WebSocket/i.test(String(node.objectType || ""))) {
        return `ts_websocket_readyState(${object})`;
      }
    }
    if (node.property === "onopen" || node.property === "onmessage" ||
        node.property === "onerror" || node.property === "onclose") {
      const ot = node.object.kind === "identifier" ? this.varTypes.get(node.object.name) : undefined;
      const on = node.object.kind === "identifier" ? node.object.name : "";
      if (ot === "Value" || /ws|socket|wss/i.test(on) || /WebSocket/i.test(String(node.objectType || ""))) {
        return `ts_websocket_get_handler(${object}, ts_string_new("${node.property}"))`;
      }
    }

    // URL properties — helpers accept Value and check type_tag
    const urlProps: Record<string, string> = {
      href: "ts_url_href",
      protocol: "ts_url_protocol",
      host: "ts_url_host",
      hostname: "ts_url_hostname",
      port: "ts_url_port",
      pathname: "ts_url_pathname",
      search: "ts_url_search",
      hash: "ts_url_hash",
      origin: "ts_url_origin",
    };
    // Prefer URL helpers for known URL property names when object is a Value
    // (e.g. const url = new URL(...); url.pathname)
    // Skip nested property access like req.headers.host (object is property_access)
    if (urlProps[node.property] && node.object.kind === "identifier") {
      const objType = this.varTypes.get(node.object.name);
      if (objType === "Value" || objType === "Url*" || !isValueObject) {
        // For Value variables named like url, or non-hashmap objects
        if (objType === "Value" || !isValueObject) {
          // Heuristic: only use URL helper if name suggests URL or type is not generic Value from request
          if (objType !== "Value" || /url/i.test(node.object.name)) {
            return `${urlProps[node.property]}(${object})`;
          }
        }
      }
    }
    if (urlProps[node.property] && !isValueObject && node.objectType !== "any") {
      return `${urlProps[node.property]}(${object})`;
    }

    // Buffer .length already handled above (ts_buffer_length) for Value + buffer-like names.

    // Method names used as bare property access (callee of call is property_access) —
    // return object only for string so emitCall can rewrite; for methods we still need the name.
    // Method calls on strings (object alone when used as value)
    if (node.objectType === "string" &&
        !["startsWith", "endsWith", "includes", "indexOf", "replace", "substring",
          "toLowerCase", "toUpperCase", "trim", "charAt", "slice", "split",
          "toString", "concat"].includes(node.property)) {
      return `${object}`;
    }

    // Check if object is a typed struct pointer (class instance) — use -> directly
    // This MUST come before the hashmap check to prevent self.as.object on typed pointers
    if (node.object.kind === "identifier") {
      const varType = this.varTypes.get(node.object.name);
      if ((varType && varType.endsWith("*") && !varType.startsWith("TS")) || node.objectType === "class") {
        const prop = sanitizeCIdentifier(node.property);
        return `${object}->${prop}`;
      }
    }

    // Known Command/Option/Argument fields on Value-typed variables (structural types)
    // Prefer struct field access over hashmap when the name looks like a commander instance
    if (node.object.kind === "identifier") {
      const on = node.object.name;
      const ot = this.varTypes.get(on);
      const looksLikeCommand =
        (ot === "Value" || ot === "any" || !ot) &&
        /^(command|cmd|program|c|this)$/i.test(on);
      const commandArrayFields = new Set([
        "options", "commands", "arguments", "args", "processedArgs",
        "_aliases", "_preActionHooks", "_postActionHooks",
      ]);
      const commandStringFields = new Set([
        "_name", "_description", "_version", "_versionFlags", "_versionDescription",
      ]);
      const commandValueFields = new Set(["_opts", "_outputConfig", "_implies"]);
      if (looksLikeCommand && commandArrayFields.has(node.property)) {
        return `((Command*)${on}.as.object)->${sanitizeCIdentifier(node.property)}`;
      }
      if (looksLikeCommand && commandStringFields.has(node.property)) {
        return `((Command*)${on}.as.object)->${sanitizeCIdentifier(node.property)}`;
      }
      if (looksLikeCommand && commandValueFields.has(node.property)) {
        return `((Command*)${on}.as.object)->${sanitizeCIdentifier(node.property)}`;
      }
      // Option fields on opt/o
      if ((ot === "Value" || !ot) && /^(opt|o|option)$/i.test(on)) {
        const optFields = new Set([
          "flags", "description", "required", "optional", "variadic", "mandatory",
          "short", "long", "negate", "hidden", "defaultValue", "presetArg",
        ]);
        if (optFields.has(node.property)) {
          const prop = sanitizeCIdentifier(node.property);
          return `((Option*)${on}.as.object)->${prop}`;
        }
      }
    }

    // error.message / err.message: Error is stored as TAG_STRING message text
    // (ts_error_new). Prefer the string itself; fall back to object.message if present.
    if (node.property === "message" && node.object.kind === "identifier" &&
        /^(error|err|e)$/i.test(node.object.name)) {
      return `({ Value __em = ${object}; (__em.tag == TAG_STRING) ? __em : ((__em.tag == TAG_OBJECT && __em.as.object) ? ts_hashmap_get((TSHashMap*)__em.as.object, ts_string_new("message")) : ts_value_string(ts_string_new(""))); })`;
    }

    // Worker/MessagePort/Worker are C structs wrapped in Value, not hashmaps
    // Must come before the generic Value hashmap handler below
    // Only match variables whose names suggest they are Worker instances (not MessageChannel hashmaps)
    if (node.object.kind === "identifier" && this.varTypes.get(node.object.name) === "Value" &&
        /worker|parentPort/i.test(node.object.name || "")) {
      const getterMap: Record<string, string> = {
        threadId: "node_worker_threads_get_threadId",
        threadName: "node_worker_threads_get_threadName",
      };
      const getter = getterMap[node.property];
      if (getter) {
        return `${getter}(${object})`;
      }
    }

    // Value / object hashmap property access: req.url → ts_hashmap_get(...)
    // Only for actual Value types (tagged union wrapping hashmap objects)
    if ((isValueObject || node.objectType === "any" || node.objectType === "map") && node.objectType !== "class") {
      // object may already be a Value expression or nested hashmap get
      if (object.startsWith("ts_hashmap_get(") || object.startsWith("ts_value_") ||
          (node.object.kind === "identifier" && this.varTypes.get(node.object.name) === "Value") ||
          node.objectType === "any" || node.objectType === "unknown" || node.objectType === "map") {
        // If object is Value, extract the hashmap pointer
        const mapExpr = `((TSHashMap*)${object}.as.object)`;
        return `ts_hashmap_get(${mapExpr}, ts_string_new("${node.property}"))`;
      }
    }

    // Value type — hashmap access
    if (node.object.kind === "identifier") {
      const varType = this.varTypes.get(node.object.name);
      if (varType === "Value") {
        return `ts_hashmap_get((TSHashMap*)${object}.as.object, ts_string_new("${node.property}"))`;
      }
      // Fallback for remaining pointer types (TSString*, TSArray*, etc.)
      if (varType && varType.endsWith("*")) {
        const prop = sanitizeCIdentifier(node.property);
        return `${object}->${prop}`;
      }
    }

    return `${object}.${node.property}`;
  }

  private emitElementAccess(node: CNode): string {
    const object = this.emit(node.object);
    const index = this.emit(node.index);

    if (node.objectType === "array") {
      return `ts_array_get(${object}, ${index})`;
    }
    if (node.objectType === "string" ||
        (node.object?.kind === "identifier" && this.varTypes.get(node.object.name) === "TSString*") ||
        object.startsWith("ts_string_") || object.startsWith("ts_to_string(")) {
      // Return single-char string (not char) so concat works
      return `ts_string_new_len((char[]){ts_string_char_at(${object}, (int32_t)(${index})), 0}, 1)`;
    }

    // Value / object with string key → hashmap get
    const objName = node.object?.kind === "identifier" ? node.object.name : "";
    const objType = objName ? this.varTypes.get(objName) : undefined;
    const idxIsNumeric =
      node.index?.kind === "number_literal" ||
      (node.index?.kind === "identifier" &&
        (this.varTypes.get(node.index.name) === "double" ||
         this.varTypes.get(node.index.name) === "number" ||
         this.varTypes.get(node.index.name) === "int"));
    if (!idxIsNumeric && (objType === "Value" || objType === "any" || objType === "TSHashMap*" ||
        object.startsWith("ts_value_") || object.startsWith("ts_hashmap_get(") ||
        /optionValues|opts|config|map|dict/i.test(objName))) {
      let keyStr = index;
      if (index.startsWith("ts_value_string(")) keyStr = index.replace(/^ts_value_string\((.+)\)$/, "$1");
      else if (node.index?.kind === "identifier" && this.varTypes.get(node.index.name) === "TSString*") {
        keyStr = index;
      } else if (node.index?.kind === "string_literal" || index.startsWith("ts_string_new(")) {
        keyStr = index;
      } else if (!index.startsWith("ts_to_string(") && !index.startsWith("ts_string_")) {
        // leave TSString* as-is; only wrap Value
        if (node.index?.kind === "identifier" && this.varTypes.get(node.index.name) === "Value") {
          keyStr = `ts_to_string(${index})`;
        }
      }
      const mapExpr = objType === "TSHashMap*" ? object : `((TSHashMap*)${object}.as.object)`;
      return `ts_hashmap_get(${mapExpr}, ${keyStr})`;
    }

    // Array-like Value
    if (objType === "Value" || objType === "TSArray*" || node.objectType === "array") {
      const arr = objType === "TSArray*" ? object : `((TSArray*)${object}.as.object)`;
      return `ts_array_get(${arr}, (int32_t)(${index}))`;
    }

    return `ts_array_get(((TSArray*)${object}.as.object), (int32_t)(${index}))`;
  }

  private emitCast(node: CNode): string {
    const expr = this.emit(node.expression);
    const targetType = node.targetType;
    // Value cast from struct pointer → wrap in tagged union
    if (targetType === "Value") {
      return `((Value){.tag = TAG_OBJECT, .as.object = ${expr}})`;
    }
    return `((${targetType})${expr})`;
  }

  private emitNew(node: CNode): string {
    const className = node.className;
    const args = (node.arguments || []).map((a: CNode) => this.emit(a)).join(", ");

    // events.EventEmitter / EventEmitter
    if (className === "EventEmitter" ||
        className === "events.EventEmitter" ||
        className.endsWith(".EventEmitter")) {
      return `node_events_EventEmitter()`;
    }

    // worker_threads: Worker / MessageChannel / BroadcastChannel / MessagePort
    if (className === "Worker" || className === "worker_threads.Worker" ||
        className.endsWith(".Worker")) {
      const a0 = node.arguments?.[0]
        ? (() => {
            const e = this.emit(node.arguments![0]);
            if (e.startsWith("ts_value_")) return e;
            if (node.arguments![0].kind === "string_literal") return `ts_value_string(${e})`;
            return e;
          })()
        : 'ts_value_string(ts_string_new(""))';
      const a1 = node.arguments?.[1]
        ? (() => {
            const e = this.emit(node.arguments![1]);
            return e.startsWith("ts_value_") || node.arguments![1].kind === "object_literal"
              ? e : e;
          })()
        : "ts_value_null()";
      return `node_worker_threads_Worker(${a0}, ${a1})`;
    }
    if (className === "MessageChannel" || className === "worker_threads.MessageChannel" ||
        className.endsWith(".MessageChannel")) {
      return `node_worker_threads_MessageChannel()`;
    }
    if (className === "MessagePort" || className === "worker_threads.MessagePort" ||
        className.endsWith(".MessagePort")) {
      return `node_worker_threads_MessagePort()`;
    }
    if (className === "BroadcastChannel" || className === "worker_threads.BroadcastChannel" ||
        className.endsWith(".BroadcastChannel")) {
      const a0 = node.arguments?.[0]
        ? (() => {
            const e = this.emit(node.arguments![0]);
            if (e.startsWith("ts_value_")) return e;
            if (node.arguments![0].kind === "string_literal") return `ts_value_string(${e})`;
            return `ts_value_string(ts_to_string(${e}))`;
          })()
        : 'ts_value_string(ts_string_new(""))';
      return `node_worker_threads_BroadcastChannel(${a0})`;
    }

    // Handle Date constructor - return timestamp as double
    if (className === "Date") {
      if (args) {
        // new Date(timestamp) or new Date("string")
        return args;
      }
      return `date_now_ts()`;
    }

    // Handle Blob constructor
    if (className === "Blob") {
      return `ts_blob_new()`;
    }

    // Handle Buffer constructor
    if (className === "Buffer") {
      if (node.arguments && node.arguments.length > 0) {
        const arg0 = this.emit(node.arguments[0]);
        // Buffer(string) or Buffer(number)
        if (node.arguments[0].kind === "string_literal") {
          return `ts_buffer_from_string(${arg0})`;
        }
        return `ts_buffer_from_string(ts_to_string(${arg0}))`;
      }
      return `ts_buffer_new(0)`;
    }

    // Handle Headers constructor
    if (className === "Headers") {
      return `ts_headers()`;
    }

    // WebSocket client: new WebSocket(url)
    if (className === "WebSocket") {
      const raw = node.arguments?.[0] ? this.emit(node.arguments[0]) : 'ts_string_new("")';
      let urlArg = this.asTSString(raw, node.arguments?.[0]);
      if (urlArg.startsWith("ts_hashmap_get(") || urlArg.startsWith("ts_value_") ||
          urlArg.startsWith("node_")) {
        urlArg = `ts_to_string(${urlArg})`;
      }
      return `ts_websocket_new(${urlArg})`;
    }

    // WebSocketServer: new WebSocketServer() — attach via Response(wss) for HTTP upgrade
    if (className === "WebSocketServer") {
      return `ts_websocket_server_new()`;
    }

    // WritableStream → StreamBody-backed chunk collector for Response streaming
    if (className === "WritableStream") {
      return `ts_writable_stream_new()`;
    }

    // Handle URL constructor — supports new URL(url) and new URL(url, base)
    // Returns Value (not TSString*)
    if (className === "URL") {
      const raw0 = node.arguments?.[0] ? this.emit(node.arguments[0]) : 'ts_string_new("")';
      // asTSString on Value (ts_hashmap_get / Value || string) must yield TSString*
      let urlArg = this.asTSString(raw0, node.arguments?.[0]);
      // Guard: if still a Value expression, coerce
      if (urlArg.startsWith("ts_hashmap_get(") || urlArg.startsWith("ts_value_") ||
          urlArg.startsWith("node_")) {
        urlArg = `ts_to_string(${urlArg})`;
      }
      if (node.arguments && node.arguments.length >= 2) {
        // new URL(path, base) — resolve relative to base
        const raw1 = this.emit(node.arguments[1]);
        let baseArg = this.asTSString(raw1, node.arguments[1]);
        if (baseArg.startsWith("ts_hashmap_get(") || baseArg.startsWith("ts_value_") ||
            baseArg.startsWith("node_")) {
          baseArg = `ts_to_string(${baseArg})`;
        }
        // Simple resolution: concat base + path (runtime can refine)
        return `ts_url_new(ts_string_concat(${baseArg}, ${urlArg}))`;
      }
      return `ts_url_new(${urlArg})`;
    }

    // Handle Response constructor — new Response(body[, init])
    // Emits ts_response_new so headers/status/streaming (string[]) are preserved.
    if (className === "Response") {
      const wrapBody = (arg: CNode | undefined): string => {
        if (!arg) return `ts_value_string(ts_string_new(""))`;
        const body = this.emit(arg);
        if (body.startsWith("ts_value_") ||
            body.startsWith("node_fs_readFile") ||
            body.startsWith("ts_buffer_") ||
            body.startsWith("ts_response_new(") ||
            body.startsWith("ts_writable_stream_") ||
            body.startsWith("ts_websocket_") ||
            body.startsWith("ts_array_")) {
          return body;
        }
        if (arg.kind === "string_literal") return `ts_value_string(${body})`;
        if (body.startsWith("ts_string_new(") || body.startsWith("ts_string_concat(") ||
            body.startsWith("ts_string_new_len(")) {
          return `ts_value_string(${body})`;
        }
        if (arg.kind === "array_literal") {
          return `ts_value_array(${body})`;
        }
        if (arg.kind === "identifier") {
          const t = this.varTypes.get(arg.name);
          if (t === "TSArray*" || t === "array") return `ts_value_array(${body})`;
          if (t === "TSString*" || t === "string") return `ts_value_string(${body})`;
          if (t === "Value" || t === "any" || t === "unknown" || !t) return body;
        }
        return `ts_value_string(ts_to_string(${body}))`;
      };
      const wrapInit = (arg: CNode | undefined): string => {
        if (!arg) return `ts_value_null()`;
        const init = this.emit(arg);
        if (init.startsWith("ts_value_")) return init;
        if (arg.kind === "object_literal" || init.includes("ts_hashmap_new()")) {
          return `ts_value_object(${init})`;
        }
        if (arg.kind === "identifier") {
          const t = this.varTypes.get(arg.name);
          if (t === "Value") return init;
        }
        return `ts_value_object(${init})`;
      };
      const bodyArg = wrapBody(node.arguments?.[0]);
      const initArg = wrapInit(node.arguments?.[1]);
      return `ts_response_new(${bodyArg}, ${initArg})`;
    }

    // Handle Error constructors (Error, TypeError, RangeError, etc.)
    if (className === "Error" || className === "TypeError" || className === "RangeError" ||
        className === "SyntaxError" || className === "ReferenceError") {
      const msg = node.arguments?.[0] ? this.emit(node.arguments[0]) : 'ts_string_new("")';
      return `ts_error_new(${msg})`;
    }

    // Map / HashMap → runtime hashmap
    if (className === "Map" || className === "HashMap" || className === "WeakMap") {
      return `ts_hashmap_new()`;
    }

    // Allocate struct + call constructor
    const cArgs = (node.arguments || []).map((a: CNode) => this.emit(a));
    // Pad missing arguments for known constructors
    const knownCtors: Record<string, string[]> = {
      "Command": ["ts_string_new(\"\")", "ts_value_undefined()"],
      "Option": ["ts_string_new(\"\")", "ts_string_new(\"\")"],
      "Argument": ["ts_string_new(\"\")", "ts_string_new(\"\")"],
      "CommanderError": ["0", "ts_string_new(\"\")", "ts_string_new(\"\")"],
      "InvalidArgumentError": ["ts_string_new(\"\")"],
    };
    const defaults = knownCtors[className];
    if (defaults) {
      while (cArgs.length < defaults.length) {
        cArgs.push(defaults[cArgs.length]);
      }
    }
    return `${className}_constructor(${cArgs.join(", ")})`;
  }

  private emitArrayLiteral(node: CNode): string {
    if (!node.elements || node.elements.length === 0) {
      return "ts_array_new()";
    }

    // Spread of a single array: [...argv] → shallow copy, not nested single-element array
    if (node.elements.length === 1) {
      const e = node.elements[0];
      // Spread element may appear as identifier of array type
      if (e.kind === "identifier") {
        const t = this.varTypes.get(e.name);
        if (t === "TSArray*" || t === "array") {
          const src = this.emit(e);
          return `({ TSArray* __sp_src = ${src}; TSArray* __sp_dst = ts_array_new(); for (int32_t __spi = 0; __spi < __sp_src->length; __spi++) ts_array_push(__sp_dst, ts_array_get(__sp_src, __spi)); __sp_dst; })`;
        }
        if (t === "Value") {
          const src = this.emit(e);
          return `({ TSArray* __sp_src = ((TSArray*)${src}.as.object); TSArray* __sp_dst = ts_array_new(); for (int32_t __spi = 0; __spi < __sp_src->length; __spi++) ts_array_push(__sp_dst, ts_array_get(__sp_src, __spi)); __sp_dst; })`;
        }
      }
      // Spread of array-producing call
      if (e.kind === "call_expression" || e.kind === "property_access") {
        const val = this.emit(e);
        if (val.startsWith("ts_string_split(") || val.startsWith("ts_array_") ||
            val.includes("__sl_dst") || val.includes("__c_dst") || val.includes("__sp_dst")) {
          return val; // already TSArray*
        }
      }
    }

    const elements = node.elements
      .map((e: CNode) => {
        let val = this.emit(e);
        // Wrap in Value constructor if needed
        if (e.kind === "number_literal") {
          val = `ts_value_number(${val})`;
        } else if (e.kind === "string_literal") {
          val = `ts_value_string(${val})`;
        } else if (e.kind === "boolean_literal") {
          val = `ts_value_boolean(${val})`;
        } else if (e.kind === "identifier") {
          const varType = this.varTypes.get(e.name);
          if (varType === "double") val = `ts_value_number(${val})`;
          else if (varType === "TSString*") val = `ts_value_string(${val})`;
          else if (varType === "int") val = `ts_value_boolean(${val})`;
          else if (varType === "TSArray*") val = `ts_value_array(${val})`;
          else if (varType && varType.endsWith("*") && !varType.startsWith("TS")) val = `ts_value_object((void*)${val})`;
          else if (varType === "Value") val = val;
        }
        return val;
      })
      .join(", ");
    return `ts_value_array(ts_array_from_values((Value[]){${elements}}, ${node.elements.length}))`;
  }

  private emitObjectLiteral(node: CNode): string {
    // Object literals → Value wrapping a hash map
    if (!node.properties || node.properties.length === 0) {
      return "ts_value_object(ts_hashmap_new())";
    }

    // If properties look like failed spreads (unknown keys), treat values as source maps to merge
    const looksLikeBrokenSpread = node.properties.length > 0 &&
      node.properties.every((p: any) => p.key === "unknown");
    if (looksLikeBrokenSpread) {
      const sources = node.properties.map((p: any) => this.emit(p.value));
      if (sources.length === 2) {
        return `({ Value __s0 = ${sources[0]}; Value __s1 = ${sources[1]}; ts_to_boolean(__s1) ? __s1 : (ts_to_boolean(__s0) ? __s0 : ts_value_object(ts_hashmap_new())); })`;
      }
      if (sources.length === 1) return sources[0];
      return sources[sources.length - 1];
    }

    const entries = node.properties
      .map((p: any) => {
        if (p.spread || p.key === "..." ) {
          return null;
        }
        const key = p.key;
        if (key === "unknown" || key === undefined) return null;
        let value = this.emit(p.value);
        // Wrap value in appropriate Value constructor
        if (p.value.kind === "string_literal") {
          value = `ts_value_string(${value})`;
        } else if (p.value.kind === "number_literal") {
          value = `ts_value_number(${value})`;
        } else if (p.value.kind === "boolean_literal") {
          value = `ts_value_boolean(${value})`;
        } else if (p.value.kind === "null_literal" || p.value.kind === "undefined_literal") {
          // already ts_value_null / ts_value_undefined
        } else if (p.value.kind === "identifier") {
          const varType = this.varTypes.get(p.value.name);
          if (varType === "double" || varType === "number") {
            value = `ts_value_number(${value})`;
          } else if (varType === "TSString*" || varType === "string") {
            value = `ts_value_string(${value})`;
          } else if (varType === "int" || varType === "boolean") {
            value = `ts_value_boolean(${value})`;
          } else if (varType === "TSArray*") {
            value = `ts_value_array(${value})`;
          } else if (varType === "TSHashMap*") {
            value = `ts_value_object((void*)${value})`;
          } else if (varType === "Value") {
            // keep
          } else if (!varType) {
            // untyped — wrap arrays/known patterns
            if (value.startsWith("ts_array_") || value.includes("TSArray*") ||
                value.includes("__sl_dst") || value.includes("__sp_dst")) {
              value = `ts_value_array(${value})`;
            }
          }
        }
        // If value is a function call that returns TSString*, wrap it
        if (value.startsWith("ts_json_stringify(") || value.startsWith("ts_json_stringify_indent(") ||
            value.startsWith("ts_string_new(") || value.startsWith("ts_string_concat(") ||
            value.startsWith("ts_to_string(") || value.startsWith("ts_number_to_string(") ||
            value.startsWith("ts_url_")) {
          value = `ts_value_string(${value})`;
        }
        // Array expressions
        if (!value.startsWith("ts_value_") &&
            (value.startsWith("ts_array_") || value.includes("__sl_dst") || value.includes("__sp_dst") ||
             value.includes("__c_dst") || value.startsWith("ts_string_split("))) {
          value = `ts_value_array(${value})`;
        }
        // Nested object/array literals already return Value
        if (!value.startsWith("ts_value_") && !value.startsWith("ts_null(") &&
            !value.startsWith("ts_undefined(") && p.value.kind === "call_expression") {
          // leave as-is if already Value-producing calls; TSString* calls handled above
        }
        return `ts_hashmap_set(map, ts_string_new("${key}"), ${value})`;
      })
      .filter((e: string | null) => e != null)
      .join(";\n  ");

    if (!entries || entries.length === 0) {
      return "ts_value_object(ts_hashmap_new())";
    }
    return `ts_value_object(({ TSHashMap* map = ts_hashmap_new(); ${entries}; map; }))`;
  }

  private emitArrowFunction(node: CNode): string {
    // Fallback if not hoisted — should not happen after visitor change
    if (node.name) {
      return `ts_value_function((void*)${node.name})`;
    }
    return `ts_value_null()`;
  }

  private emitFunctionExpression(node: CNode): string {
    if (node.name) {
      return `ts_value_function((void*)${node.name})`;
    }
    return `ts_value_null()`;
  }

  /** Unwrap parenthesized nodes to get the underlying kind */
  private unwrapNode(node?: CNode): CNode | undefined {
    if (!node) return undefined;
    let n: CNode = node;
    while (n.kind === "parenthesized" && n.expression) {
      n = n.expression;
    }
    return n;
  }

  private unwrapKind(node?: CNode): string | undefined {
    return this.unwrapNode(node)?.kind;
  }

  /** Coerce right side of Value || / ?? to TSString* when needed */
  private coerceLogicalRight(emitted: string, node?: CNode): string {
    if (emitted.startsWith("ts_string_new(") || emitted.startsWith("ts_string_concat(") ||
        emitted.startsWith("ts_to_string(") || emitted.startsWith("ts_number_to_string(")) {
      return emitted;
    }
    if (node?.kind === "string_literal") return emitted;
    if (emitted.startsWith("ts_value_") || emitted.startsWith("ts_hashmap_get(")) {
      return `ts_to_string(${emitted})`;
    }
    return emitted;
  }

  /** Coerce an emitted expression to TSString* */
  private asTSString(emitted: string, node?: CNode): string {
    // Strip accidental outer ts_to_string around already-string producers
    const stripDouble = (s: string): string => {
      // Match balanced ts_to_string(...) including nested GNU stmts
      if (!s.startsWith("ts_to_string(")) return s;
      // Find matching close paren
      let depth = 0;
      for (let i = "ts_to_string".length; i < s.length; i++) {
        if (s[i] === "(") depth++;
        else if (s[i] === ")") {
          depth--;
          if (depth === 0) {
            if (i !== s.length - 1) return s; // trailing junk
            const inner = s.slice("ts_to_string(".length, i);
            if (inner.startsWith("ts_string_") || inner.startsWith("ts_json_stringify") ||
                inner.startsWith("ts_array_join(") || inner.startsWith("ts_to_string(") ||
                /^(Command|Option|Argument)_/.test(inner) || inner.includes("/*__ts_str*/") ||
                inner.startsWith("camelcase(") || inner.startsWith("src_cli_commander_") ||
                inner.startsWith("({") || (inner.includes("->") && !inner.startsWith("ts_hashmap"))) {
              return inner;
            }
            return s;
          }
        }
      }
      return s;
    };
    emitted = stripDouble(emitted);

    // Already TSString* — including ts_json_stringify which returns TSString*
    if (emitted.startsWith("ts_string_new(") || emitted.startsWith("ts_string_concat(") ||
        emitted.startsWith("ts_number_to_string(") || emitted.startsWith("ts_to_string(") ||
        emitted.startsWith("ts_url_") || /^ts_string_/.test(emitted) ||
        emitted.startsWith("ts_array_join(") || emitted.startsWith("camelcase(") ||
        emitted.startsWith("formatOptionFlags(") || emitted.startsWith("src_cli_commander_") ||
        emitted.startsWith("ts_json_stringify") ||
        /^(Command|Option|Argument)_\w+\(/.test(emitted) ||
        emitted.includes("/*__ts_str*/")) {
      return emitted;
    }
    // Already a TSString*-producing GNU statement expr (Value || string)
    if (emitted.includes("/*__ts_str*/") ||
        (emitted.includes("TSString* __or_r") && emitted.includes("__or_r;"))) {
      return emitted;
    }
    // Ternary already producing TSString* (from Value || string fallback)
    if (emitted.includes("ts_to_boolean(") && emitted.includes("? ts_to_string(")) {
      return emitted;
    }
    if (emitted.startsWith("(") && (emitted.includes("ts_string_new(") || emitted.includes("ts_to_string(") || emitted.includes("/*__ts_str*/"))) {
      return emitted;
    }
    if (node?.kind === "string_literal") return emitted;
    if (node?.kind === "call_expression") {
      // Class methods returning string (Argument_name, Command_getName, …)
      if (/^(Command|Option|Argument)_/.test(emitted) || emitted.startsWith("camelcase(") ||
          emitted.startsWith("formatOptionFlags(") || emitted.startsWith("src_cli_")) {
        return emitted;
      }
    }
    if (emitted.startsWith("ts_value_") || emitted.startsWith("ts_hashmap_get(") ||
        (node?.kind === "identifier" && this.varTypes.get(node.name) === "Value")) {
      return `ts_to_string(${emitted})`;
    }
    // property_access on typed struct fields
    if (node?.kind === "property_access") {
      const t = this.resolveTargetType(node);
      const prop = node.property as string;
      if (t === "TSString*" || t === "string") return emitted;
      if (t === "double" || t === "number" || t === "int") {
        return `ts_number_to_string((double)(${emitted}))`;
      }
      // Known string fields even when type resolution failed
      if (this.isKnownStringStructField(prop) ||
          emitted.includes("->_version") || emitted.includes("->_name") ||
          emitted.includes("->flags") || emitted.includes("->description") ||
          emitted.includes("->short_") || emitted.includes("->long_") ||
          emitted.includes("->message") || emitted.includes("->code") ||
          emitted.includes("->defaultValueDescription") || emitted.includes("->envVar")) {
        return emitted;
      }
      if (t === "Value" || t === "any" || !t) {
        // Known numeric fields only — never cast string pointers to double
        if (this.isKnownNumericStructField(prop) || prop === "length") {
          return `ts_number_to_string((double)(${emitted}))`;
        }
        // Value/hashmap — stringify
        if (emitted.startsWith("ts_hashmap_get(") || emitted.startsWith("ts_value_")) {
          return `ts_to_string(${emitted})`;
        }
        // Unknown bare field: prefer leave as-is if it looks like a pointer field
        // (string fields are common on commander structs)
        if (/->\w+$/.test(emitted) && !emitted.startsWith("ts_hashmap")) {
          return emitted;
        }
        return `ts_to_string(${emitted})`;
      }
      return emitted;
    }
    if (node?.kind === "identifier") {
      const t = this.varTypes.get(node.name);
      if (t === "TSString*" || t === "string") return emitted;
      if (t === "double" || t === "number" || t === "int") {
        return `ts_number_to_string((double)(${emitted}))`;
      }
    }
    // binary_expression / parenthesized GNU that already yields TSString*
    if (node?.kind === "binary_expression" && (node.operator === "||" || node.operator === "??" || node.operator === "+")) {
      return emitted;
    }
    if (emitted.startsWith("({") && emitted.includes("TSString*")) return emitted;
    return `ts_to_string(${emitted})`;
  }

  private emitConditional(node: CNode): string {
    let condition = this.emit(node.condition);
    // Already a scalar boolean expression — never double-wrap
    const alreadyBool =
      condition.startsWith("ts_to_boolean(") || condition.startsWith("!!(") ||
      condition.startsWith("!") || condition.startsWith("ts_hashmap_has(") ||
      condition.startsWith("Option_isBoolean(") || condition.startsWith("ts_string_") ||
      condition.startsWith("ts_array_index_of(") || condition.includes(" >= 0)");
    // int struct fields — leave alone
    const isIntField = /->(variadic|required|optional|hidden|mandatory|negate|_hidden|_helpEnabled|_exitOverride|_allowUnknownOption|_allowExcessArguments|_defaultCommand)\b/.test(condition) &&
      !condition.startsWith("ts_to_boolean(");
    // Coerce Value conditions only
    if (!alreadyBool && !isIntField && (
        condition.startsWith("ts_value_") || condition.startsWith("ts_hashmap_get(") ||
        (node.condition?.kind === "identifier" && this.varTypes.get(node.condition.name) === "Value") ||
        // bare Value field access (not already wrapped)
        (/->(defaultValue|presetArg|_opts|_implies)\b/.test(condition) &&
         !condition.startsWith("ts_to_boolean(") && !condition.startsWith("!")))) {
      condition = `ts_to_boolean(${condition})`;
    }
    let trueExpr = this.emit(node.trueExpr);
    let falseExpr = this.emit(node.falseExpr);
    return `(${condition} ? ${trueExpr} : ${falseExpr})`;
  }

  private emitTemplate(node: CNode): string {
    if (!node.templateSpans || node.templateSpans.length === 0) {
      return `ts_string_new("${this.escapeString(node.head)}")`;
    }

    const parts: string[] = [`ts_string_new("${this.escapeString(node.head)}")`];
    for (const span of node.templateSpans) {
      // Coerce interpolated expression to TSString* (Value / number / string)
      const raw = this.emit(span.expression);
      let exprStr: string;
      // Numeric expressions (int32_t fields, binary arithmetic) → number_to_string
      if (span.expression?.kind === "number_literal" ||
          (span.expression?.kind === "identifier" &&
           (this.varTypes.get(span.expression.name) === "double" ||
            this.varTypes.get(span.expression.name) === "number" ||
            this.varTypes.get(span.expression.name) === "int")) ||
          (span.expression?.kind === "binary_expression" &&
           ["+", "-", "*", "/", "%"].includes(span.expression.operator || "") &&
           span.expression.leftType !== "string") ||
          (span.expression?.kind === "property_access" &&
           (this.resolveTargetType(span.expression) === "double" ||
            this.resolveTargetType(span.expression) === "int" ||
            raw.includes("->length")))) {
        exprStr = `ts_number_to_string((double)(${raw}))`;
      } else {
        exprStr = this.asTSString(raw, span.expression);
        // If asTSString still produced ts_to_string(int), fix
        if (exprStr.startsWith("ts_to_string(") &&
            (raw.includes("->length") || raw.match(/^\w+$/) && this.varTypes.get(raw) === "int")) {
          exprStr = `ts_number_to_string((double)(${raw}))`;
        }
      }
      const literal = this.escapeString(span.literal);
      parts.push(`ts_string_concat(${exprStr}, ts_string_new("${literal}"))`);
    }

    return parts.reduce((acc, part) => `ts_string_concat(${acc}, ${part})`);
  }

  private emitBlockBody(node: CNode): string {
    return (node.statements || [])
      .map((s: any) => `    ${s}`)
      .join("\n");
  }

  /** Emit a console.log argument as a raw Value (for multi-arg inspect) */
  private emitConsoleLogArgRaw(a: CNode): string {
    const emitted = this.emit(a);
    // Already a Value constructor
    if (emitted.startsWith("ts_value_") || emitted.startsWith("ts_null(") ||
        emitted.startsWith("ts_undefined(") || emitted.startsWith("ts_typeof(")) {
      return emitted;
    }
    // int32_t / double length helpers — never pass raw int to ts_to_string
    if (emitted.startsWith("ts_buffer_length(") || emitted.startsWith("ts_blob_size(") ||
        /->length\s*$/.test(emitted) ||
        emitted.match(/^\(\(TSArray\*\)[^)]+\)->length$/) ||
        emitted.match(/^ts_math_/)) {
      return `ts_value_number((double)(${emitted}))`;
    }
    // Buffer toString helpers return TSString*
    if (emitted.startsWith("ts_buffer_toString_") || emitted.startsWith("ts_buffer_toString")) {
      return `ts_value_string(${emitted})`;
    }
    // Buffer APIs returning Value
    if (emitted.startsWith("ts_buffer_") && !emitted.startsWith("ts_buffer_isBuffer")) {
      // isBuffer returns int; others like from/alloc/slice/concat return Value
      if (emitted.startsWith("ts_buffer_isBuffer(")) {
        return `ts_value_boolean(${emitted})`;
      }
      return emitted;
    }
    // Builtin functions returning Value (object-like responses, fetch, etc.)
    // But NOT ts_json_stringify* (returns TSString*) or node_* returning primitives
    if (emitted.startsWith("ts_fetch_")) {
      return emitted;
    }
    // ts_json_stringify / ts_json_stringify_indent return TSString* — wrap as Value string
    if (emitted.startsWith("ts_json_stringify(") || emitted.startsWith("ts_json_stringify_indent(")) {
      return `ts_value_string(${emitted})`;
    }
    // ts_json_parse returns Value
    if (emitted.startsWith("ts_json_parse(")) {
      return emitted;
    }
    // node_process_pid() returns int — wrap as Value number
    if (emitted.match(/^node_\w+_pid\s*\(/)) {
      return `ts_value_number((double)${emitted})`;
    }
    // node_os_totalmem/freemem/uptime return double — wrap as Value number
    if (emitted.match(/^node_\w+_(totalmem|freemem|uptime)\s*\(/)) {
      return `ts_value_number(${emitted})`;
    }
    // Date functions returning double (now, parse) — wrap as Value number
    if (emitted.match(/^date_(now_ts|parse_ts)\b/)) {
      return `ts_value_number(${emitted})`;
    }
    // Date getter functions returning int — wrap as Value number
    if (emitted.match(/^date_(getFullYear|getMonth|getDate|getDay|getHours|getMinutes|getSeconds|getMilliseconds|getTime)\b/)) {
      return `ts_value_number((double)${emitted})`;
    }
    // Date toString functions returning TSString* — wrap as Value string
    if (emitted.match(/^date_(toISOString|toDateString|toTimeString|toLocaleString)\b/)) {
      return `ts_value_string(${emitted})`;
    }
    // ts_blob_size returns double — wrap as Value number
    if (emitted.startsWith("ts_blob_size(")) {
      return `ts_value_number(${emitted})`;
    }
    // ts_blob_type returns TSString* — wrap as Value string
    if (emitted.startsWith("ts_blob_type(")) {
      return `ts_value_string(${emitted})`;
    }
    // ts_url_* properties return TSString* — wrap as Value string
    if (emitted.match(/^ts_url_(href|protocol|host|hostname|port|pathname|search|hash|origin|toString)\(/)) {
      return `ts_value_string(${emitted})`;
    }
    // Other node_* calls returning Value — pass as-is
    if (emitted.startsWith("node_")) {
      return emitted;
    }
    // String literal → wrap as Value string
    if (a.kind === "string_literal") return `ts_value_string(${emitted})`;
    // Number literal → wrap as Value number
    if (a.kind === "number_literal") return `ts_value_number(${emitted})`;
    // Boolean literal → wrap as Value boolean
    if (a.kind === "boolean_literal") return emitted ? "ts_value_boolean(1)" : "ts_value_boolean(0)";
    // Numeric binary expressions — need Value wrapping
    if (a.kind === "binary_expression" && a.leftType === "number") {
      const compOps = ["<", ">", "<=", ">=", "==", "===", "!=", "!=="];
      if (compOps.includes(a.operator)) {
        return `ts_value_boolean(${emitted})`;
      }
      return `ts_value_number(${emitted})`;
    }
    // Identifier with known type
    if (a.kind === "identifier") {
      const t = this.varTypes.get(a.name);
      if (t === "double" || t === "number") return `ts_value_number(${emitted})`;
      if (t === "TSString*" || t === "string") return `ts_value_string(${emitted})`;
      if (t === "int" || t === "boolean") return `ts_value_boolean(${emitted})`;
      if (t === "Value") return emitted;
    }
    // property_access .length → int (array/string/buffer)
    if (a.kind === "property_access" && a.property === "length") {
      return `ts_value_number((double)(${emitted}))`;
    }
    // property_access that already produced a number helper
    if (a.kind === "property_access" &&
        (emitted.startsWith("ts_buffer_length(") || emitted.startsWith("ts_blob_size(") ||
         emitted.startsWith("ts_fetch_response_status(") || /->length\s*$/.test(emitted))) {
      return `ts_value_number((double)(${emitted}))`;
    }
    // Array/object literals already return Value
    if (a.kind === "array_literal" || a.kind === "object_literal") return emitted;
    // Template expressions produce TSString* via ts_string_concat — wrap as Value string
    if (a.kind === "template_expression") return `ts_value_string(${emitted})`;
    // ts_to_string / ts_string_concat / ts_string_new / ts_number_to_string return TSString* — wrap as Value
    if (emitted.startsWith("ts_to_string(") || emitted.startsWith("ts_string_concat(") ||
        emitted.startsWith("ts_string_new(") || emitted.startsWith("ts_number_to_string(") ||
        emitted.startsWith("ts_url_toString(")) {
      return `ts_value_string(${emitted})`;
    }
    // Function / method calls — use return type when known
    if (a.kind === "call_expression") {
      // Math / numeric helpers
      if (emitted.startsWith("ts_math_") || emitted.startsWith("ts_to_number(") ||
          emitted.match(/^date_(now_ts|parse_ts|get)/)) {
        return `ts_value_number((double)(${emitted}))`;
      }
      // Class methods: Point_distance / Point_toString / Command_getName …
      const classCall = emitted.match(/^([A-Za-z_][A-Za-z0-9_]*)_([A-Za-z_][A-Za-z0-9_$]*)\s*\(/);
      if (classCall) {
        const method = classCall[2];
        // Prefer propertyCType return type from callee
        const propCType = a.callee?.kind === "property_access" ? a.callee.propertyCType : undefined;
        let retType = "";
        if (propCType) {
          const m = propCType.match(/^([^(]+)\s*\(\*\)/);
          if (m) retType = m[1].trim();
          else if (!propCType.includes("(*)")) retType = propCType;
        }
        if (retType === "TSString*" || retType === "string" ||
            method === "toString" || method === "name" || method === "getName" ||
            method === "getDescription" || method === "getVersion" || method === "getAlias" ||
            method === "description" || method === "flags" || method === "attributeName" ||
            method === "helpInformation") {
          return `ts_value_string(${emitted})`;
        }
        if (retType === "boolean" ||
            ((method.startsWith("is") || method.startsWith("has") || method === "equals") &&
             retType !== "double" && retType !== "number" && retType !== "TSString*")) {
          return `ts_value_boolean(${emitted})`;
        }
        if (retType === "double" || retType === "number" || retType === "int" ||
            method === "distance" || method === "length" || method === "size" ||
            method === "getTime" || method === "valueOf") {
          return `ts_value_number((double)(${emitted}))`;
        }
        if (retType === "Value") return emitted;
        // Typed class pointer return (Foo*) → object Value
        if (retType.endsWith("*") && !retType.startsWith("TS")) {
          return `ts_value_object((void*)${emitted})`;
        }
      }
      // TSString* producers
      if (emitted.startsWith("ts_string_") || emitted.startsWith("ts_to_string(") ||
          emitted.startsWith("ts_number_to_string(") || emitted.includes("/*__ts_str*/")) {
        return `ts_value_string(${emitted})`;
      }
      // Default: assume Value-returning (node_*, ts_fetch_*, etc.)
      return emitted;
    }
    // Fallback: convert to Value string
    return `ts_value_string(ts_to_string(${emitted}))`;
  }

  /** Emit a console.log argument as a TSString* for concatenation */
  private emitConsoleLogArg(a: CNode): string {
    const emitted = this.emit(a);
    // Already a string literal
    if (a.kind === "string_literal") return emitted;
    // Already a Value - convert to string
    if (emitted.startsWith("ts_value_") || emitted.startsWith("ts_null(") ||
        emitted.startsWith("ts_undefined(") || emitted.startsWith("ts_typeof(")) {
      return `ts_to_string(${emitted})`;
    }
    // Already a TSString* (including toString() results) — wrap as Value string
    if (emitted.startsWith("ts_string_concat(") || emitted.startsWith("ts_string_new(") ||
        emitted.startsWith("ts_number_to_string(") ||
        emitted.startsWith("ts_url_toString(")) {
      return `ts_value_string(${emitted})`;
    }
    // ts_to_string returns TSString* — wrap as Value string
    if (emitted.startsWith("ts_to_string(")) {
      return `ts_value_string(${emitted})`;
    }
    // Number - convert to string
    if (a.kind === "number_literal") {
      return `ts_number_to_string(${emitted})`;
    }
    // Binary expression with number type - check if it's a comparison (returns int/boolean)
    if (a.kind === "binary_expression" && a.leftType === "number") {
      const compOps = ["<", ">", "<=", ">=", "==", "===", "!=", "!=="];
      if (compOps.includes(a.operator)) {
        return `ts_to_string(ts_value_boolean(${emitted}))`;
      }
      return `ts_number_to_string(${emitted})`;
    }
    // Boolean - convert to string
    if (a.kind === "boolean_literal") {
      return emitted ? `ts_string_new("true")` : `ts_string_new("false")`;
    }
    // Identifier with known type - convert based on type
    if (a.kind === "identifier") {
      const varType = this.varTypes.get(a.name) || this.varTypes.get(emitted);
      if (varType === "double" || varType === "number") {
        return `ts_number_to_string(${emitted})`;
      }
      if (varType === "TSString*" || varType === "string") {
        return emitted;
      }
      if (varType === "int" || varType === "boolean") {
        return `ts_to_string(ts_value_boolean(${emitted}))`;
      }
    }
    // Function call result that returns TSString* - convert to string directly
    if (emitted.match(/^date_(toISOString|toDateString|toTimeString|toLocaleString)/)) {
      return emitted;
    }
    // Function call result that returns double - convert to string
    if (emitted.match(/^date_(getFullYear|getMonth|getDate|getDay|getHours|getMinutes|getSeconds|getMilliseconds|getTime)_ts/)) {
      return `ts_number_to_string(${emitted})`;
    }
    // Function calls returning double - wrap in ts_value_number then ts_to_string
    if (emitted.match(/^date_(now_ts|parse_ts)/)) {
      return `ts_to_string(ts_value_number(${emitted}))`;
    }
    // Function calls returning int (pid) - as number string
    if (emitted.match(/^node_\w+_pid\s*\(/)) {
      return `ts_number_to_string((double)${emitted})`;
    }
    // confirm() returns int (0/1)
    if (emitted.match(/^ts_confirm\s*\(/)) {
      return `ts_to_string(ts_value_boolean(${emitted}))`;
    }
    // prompt() returns Value (string or null)
    if (emitted.match(/^ts_prompt\s*\(/)) {
      return `ts_to_string(${emitted})`;
    }
    // totalmem/freemem/uptime return double
    if (emitted.match(/^node_\w+_(totalmem|freemem|uptime)\s*\(/)) {
      return `ts_number_to_string(${emitted})`;
    }
    // Other node_*() calls returning Value
    if (emitted.match(/^node_\w+_\w+\s*\(/)) {
      return `ts_to_string(${emitted})`;
    }
    // ts_json_stringify returns TSString* - wrap in ts_value_string then ts_to_string
    if (emitted.startsWith("ts_json_stringify(") || emitted.startsWith("ts_json_stringify_indent(")) {
      return `ts_to_string(ts_value_string(${emitted}))`;
    }
    // ts_json_parse returns Value - convert to string
    if (emitted.startsWith("ts_json_parse(")) {
      return `ts_to_string(${emitted})`;
    }
    // Response properties
    if (emitted.startsWith("ts_fetch_response_status(")) {
      return `ts_number_to_string(${emitted})`;
    }
    if (emitted.startsWith("ts_fetch_response_statusText(") ||
        emitted.startsWith("ts_fetch_response_url(")) {
      return emitted;
    }
    // Blob properties
    if (emitted.startsWith("ts_blob_size(")) {
      return `ts_number_to_string(${emitted})`;
    }
    if (emitted.startsWith("ts_blob_type(")) {
      return emitted;
    }
    // URL properties (all return TSString*)
    if (emitted.match(/^ts_url_(href|protocol|host|hostname|port|pathname|search|hash|origin|toString)\(/)) {
      return emitted;
    }
    // Variable or other - convert via ts_to_string
    return `ts_to_string(${emitted})`;
  }

  /** Check if a property_access node accesses a known TSArray* struct member (e.g., self->commands) */
  private isStructArrayMember(node: CNode): boolean {
    if (!node || node.kind !== "property_access") return false;
    const knownArrayProps = new Set(["options", "commands", "arguments", "_aliases", "_preActionHooks", "_postActionHooks", "args", "processedArgs", "items"]);
    if (knownArrayProps.has(node.property)) {
      // Check if the object is a struct pointer (class instance)
      const objName = node.object?.kind === "identifier" ? node.object.name : null;
      if (objName) {
        const objType = this.varTypes.get(objName);
        if (objType && objType.endsWith("*") && !objType.startsWith("TS")) return true;
      }
      // Or check checkerTypeName
      if (node.object?.checkerTypeName && /^[A-Z]/.test(node.object.checkerTypeName)) return true;
    }
    return false;
  }

  /** Struct fields that are TSString* (commander Option/Command/Argument, etc.) */
  private isKnownStringStructField(prop: string | undefined): boolean {
    if (!prop) return false;
    return [
      "short_", "short", "long_", "long", "flags", "description", "name", "_name",
      "code", "message", "nestedError", "defaultValueDescription", "envVar",
      "_version", "_versionFlags", "_versionDescription", "_description", "attr",
      "data", "type", "href", "protocol", "host", "hostname", "port", "pathname",
      "search", "hash", "origin", "statusText", "url",
    ].includes(prop);
  }

  /** Struct fields that are double/int scalars */
  private isKnownNumericStructField(prop: string | undefined): boolean {
    if (!prop) return false;
    return [
      "x", "y", "width", "height", "radius", "score", "count", "exitCode",
      "required", "optional", "variadic", "negate", "mandatory", "hidden",
      "_hidden", "_defaultCommand", "_exitOverride", "_allowUnknownOption",
      "_allowExcessArguments", "_helpEnabled", "isDefault", "status", "size",
      "pid", "length",
    ].includes(prop);
  }

  private escapeString(str: string): string {
    return str
      .replace(/\\/g, "\\\\")
      .replace(/"/g, '\\"')
      .replace(/\n/g, "\\n")
      .replace(/\r/g, "\\r")
      .replace(/\t/g, "\\t");
  }

  /** Count expected non-self parameters from a function pointer type string like "Value (*)(Command*, TSString*, TSString*)" */
  private countExpectedParams(propertyCType: string | undefined): number {
    return this.parseExpectedParamTypes(propertyCType).length;
  }

  /** Default C expression for a missing optional parameter of the given type */
  private defaultPadForType(cType: string): string {
    const t = (cType || "Value").trim();
    if (t === "TSString*" || t === "string") return "((TSString*)NULL)";
    if (t === "TSArray*") return "((TSArray*)NULL)";
    if (t === "TSHashMap*") return "((TSHashMap*)NULL)";
    if (t === "int" || t === "boolean") return "0";
    if (t === "double" || t === "number") return "0.0";
    if (t === "void*" || t === "void *") return "NULL";
    if (t.endsWith("*") && !t.startsWith("TS") && /^[A-Z]/.test(t)) return `((${t})NULL)`;
    if (t.includes("(*)")) return "NULL";
    return "ts_value_undefined()";
  }

  /**
   * Hardcoded param types (excluding self) for known class methods when the
   * type checker loses method signatures on chain temps (__chain_N).
   */
  private knownClassMethodParamTypes(className: string, methodName: string): string[] {
    if (className === "Command") {
      if (methodName === "argument") return ["TSString*", "TSString*", "Value"];
      if (methodName === "option" || methodName === "requiredOption") {
        return ["TSString*", "TSString*", "Value"];
      }
      if (methodName === "action") return ["Value (*)(TSArray*)"];
      if (methodName === "hook") return ["TSString*", "Value (*)(TSArray*)"];
      if (methodName === "version") return ["TSString*", "TSString*", "TSString*"];
      if (methodName === "name" || methodName === "description" || methodName === "alias") {
        return ["TSString*"];
      }
      if (methodName === "command") return ["TSString*", "Value"];
      if (methodName === "parse" || methodName === "parseAsync") return ["TSArray*", "Value"];
    }
    if (className === "Option" || className === "Argument") {
      if (methodName === "default") return ["Value", "TSString*"];
      if (methodName === "argParser") return ["Value (*)(TSString*, Value)"];
      if (methodName === "choices") return ["TSArray*"];
    }
    return [];
  }

  /** Parse expected parameter types (excluding self) from a function pointer type */
  private parseExpectedParamTypes(propertyCType: string | undefined): string[] {
    if (!propertyCType || !propertyCType.includes("(*)")) return [];
    // Match both "Ret (*)(params)" and "Ret (*name)(params)"
    const match = propertyCType.match(/\(\*[^)]*\)\s*\(([^)]*)\)/) ||
                  propertyCType.match(/\(\*\)\s*\(([^)]*)\)/);
    if (!match) return [];
    // Split params carefully — function-pointer params themselves contain commas inside (*)
    let params = this.splitCParamList(match[1]);
    // C "void" alone means zero parameters
    if (params.length === 1 && params[0] === "void") return [];
    params = params.filter(p => p !== "void");
    // Skip first param if it looks like a self pointer (ClassName*, not TSString*/TSArray*/Value*)
    const first = params[0] || "";
    const firstParamIsSelf =
      params.length > 0 &&
      /^[A-Z][A-Za-z0-9_]*\s*\*$/.test(first) &&
      !first.startsWith("TS") &&
      first !== "Value*" &&
      first !== "void*";
    return firstParamIsSelf ? params.slice(1) : params;
  }

  /** Split a C parameter list, respecting nested parentheses */
  private splitCParamList(s: string): string[] {
    const out: string[] = [];
    let depth = 0;
    let cur = "";
    for (const ch of s) {
      if (ch === "(") depth++;
      if (ch === ")") depth--;
      if (ch === "," && depth === 0) {
        const t = cur.trim();
        if (t.length > 0) out.push(t);
        cur = "";
      } else {
        cur += ch;
      }
    }
    const t = cur.trim();
    if (t.length > 0) out.push(t);
    return out;
  }
}
