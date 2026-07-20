import type { CNode, ImportedSymbolInfo } from "./c-emitter.js";
import { ExpressionEmitter, sanitizeCIdentifier } from "./expression-emitter.js";

export class StatementEmitter {
  private exprEmitter = new ExpressionEmitter();

  /** Register a local/global variable type for expression emission (console.log wrapping, etc.) */
  declareVar(name: string, type: string): void {
    this.exprEmitter.declareVar(name, type);
  }

  /** Set imported symbols mapping for identifier resolution */
  setImportedSymbols(symbols: Map<string, ImportedSymbolInfo>): void {
    this.exprEmitter.setImportedSymbols(symbols);
  }

  /** Set namespace module paths for `import * as X` resolution */
  setNamespaceModulePaths(paths: Map<string, string>): void {
    for (const [name, modulePath] of paths) {
      this.exprEmitter.setNamespaceModulePath(name, modulePath);
    }
  }

  emit(node: CNode): string {
    switch (node.kind) {
      case "function_decl":
        return this.emitFunctionDecl(node);
      case "variable_decl":
        return this.emitVariableDecl(node);
      case "assignment":
        return this.emitAssignment(node);
      case "if_statement":
        return this.emitIfStatement(node);
      case "while_statement":
        return this.emitWhileStatement(node);
      case "do_while_statement":
        return this.emitDoWhileStatement(node);
      case "for_statement":
        return this.emitForStatement(node);
      case "for_of_statement":
        return this.emitForOfStatement(node);
      case "for_in_statement":
        return this.emitForInStatement(node);
      case "return_statement":
        return this.emitReturnStatement(node);
      case "expression_statement":
        return this.emitExpressionStatement(node);
      case "block":
        return this.emitBlock(node);
      case "try_statement":
        return this.emitTryStatement(node);
      case "throw_statement":
        return this.emitThrowStatement(node);
      case "switch_statement":
        return this.emitSwitchStatement(node);
      case "break_statement":
        return "break;";
      case "continue_statement":
        return "continue;";
      case "module_level_code":
        return this.emitModuleLevelCode(node);
      default:
        return `/* unsupported: ${node.kind} */`;
    }
  }

  /** Emit a parameter declaration, handling function pointer types correctly */
  private emitParam(p: any): string {
    const name = sanitizeCIdentifier(p.name);
    const type = p.type;

    // Function pointer types contain (*), e.g. "void (*)(Value)"
    // C requires the name inside: "void (*resolve)(Value)"
    const match = type.match(/^(.+)\s*\(\*\)\s*\(([^)]*)\)$/);
    if (match) {
      const [_, returnType, paramList] = match;
      return `${returnType} (*${name})(${paramList || ""})`;
    }

    return `${type} ${name}`;
  }

  emitForwardDeclaration(node: CNode): string {
    const paramStr = node.params
      ? node.params.map((p: any) => this.emitParam(p)).join(", ")
      : "void";
    // Rename 'main' to avoid conflict with C's main
    const funcName = node.name === "main" ? "entry" : node.name;
    return `${node.returnType || "void"} ${funcName}(${paramStr});`;
  }

  emitClassMethods(node: CNode): string {
    const lines: string[] = [];
    for (const method of node.methods || []) {
      lines.push(this.emitFunctionDecl(method));
      lines.push('');
    }
    return lines.join('\n');
  }

  /** Current function return type — used by emitReturnStatement for coercion */
  private currentReturnType = "void";

  private emitFunctionDecl(node: CNode): string {
    // Register param types so property access / assignments coerce correctly
    if (node.params) {
      for (const p of node.params) {
        if (p?.name && p?.type) this.exprEmitter.declareVar(p.name, p.type);
      }
    }
    // Constructors don't declare `self` in params but allocate it in body;
    // register it early so property access inside the body resolves correctly.
    if ((node.name || "").endsWith("_constructor")) {
      const className = (node.returnType || "").replace(/\*$/, "");
      if (className) {
        this.exprEmitter.declareVar("self", `${className}*`);
      }
    }
    const params = node.params
      ? node.params.map((p: any) => this.emitParam(p)).join(", ")
      : "void";
    const prevRet = this.currentReturnType;
    this.currentReturnType = node.returnType || "void";
    const body = node.body ? this.emitBlock(node.body) : ";";
    this.currentReturnType = prevRet;
    // Rename 'main' to avoid conflict with C's main
    const funcName = node.name === "main" ? "entry" : node.name;
    // For non-void functions, insert a default return before closing brace
    if (node.body && node.returnType && node.returnType !== "void") {
      const defaultRet = node.returnType === "Value" ? "return ts_value_undefined();" : `return (${node.returnType}){0};`;
      const lastBrace = body.lastIndexOf("}");
      if (lastBrace > 0) {
        const patched = body.slice(0, lastBrace) + " " + defaultRet + " " + body.slice(lastBrace);
        return `${node.returnType || "void"} ${funcName}(${params}) ${patched}`;
      }
    }
    return `${node.returnType || "void"} ${funcName}(${params}) ${body}`;
  }

  private emitVariableDecl(node: CNode): string {
    // Never emit invalid C types (generics, raw TS type strings)
    let type = node.type || "Value";
    if (type.includes("<") || type.includes(">") || type.includes("|") ||
        (type.startsWith("struct ") && /[^a-zA-Z0-9_\s*]/.test(type))) {
      type = "Value";
    }

    let initExpr = node.init ? this.exprEmitter.emit(node.init) : "";
    // File-scope statics need zero-init when no initializer (C defaults, but be explicit for clarity)
    let init = initExpr ? ` = ${initExpr}` : (node.isStatic ? " = {0}" : "");
    // For scalar statics, {0} works; for pointers too. Prefer typed zero:
    if (!initExpr && node.isStatic) {
      if (type === "double" || type === "number" || type === "int" || type === "boolean") {
        init = " = 0";
      } else if (type === "Value") {
        init = " = {0}"; // zero-init Value
      } else {
        init = " = {0}";
      }
    }
    const qualifier = node.isStatic ? "static " : "";
    if (node.name) {
      this.exprEmitter.declareVar(node.name, type);
    }

    // Coerce property access / Value init into declared primitive types
    if (node.init && initExpr) {
      // Already TSString* (e.g. Value || "fallback" GNU stmt expr)
      const alreadyString =
        initExpr.includes("/*__ts_str*/") ||
        initExpr.includes("TSString* __or_r") ||
        initExpr.startsWith("ts_string_new(") ||
        initExpr.startsWith("ts_string_concat(") ||
        initExpr.startsWith("ts_to_string(") ||
        initExpr.startsWith("ts_number_to_string(") ||
        initExpr.startsWith("ts_url_");

      if (type === "int" || type === "boolean") {
        if (initExpr.startsWith("ts_hashmap_get(") || initExpr.startsWith("ts_value_") ||
            initExpr.includes("ts_fetch_") ||
            (initExpr.includes(".as.object") && !alreadyString)) {
          init = ` = ts_to_boolean(${initExpr})`;
        }
      } else if (type === "double" || type === "number") {
        if (initExpr.startsWith("ts_hashmap_get(") || initExpr.startsWith("ts_value_")) {
          init = ` = ts_to_number(${initExpr})`;
        }
      } else if (type === "TSString*" || type === "string") {
        // Already a TSString*-producing call (Command_getName, ts_string_*, …)
        const alreadyTsString =
          alreadyString ||
          initExpr.startsWith("ts_string_") ||
          initExpr.startsWith("ts_to_string(") ||
          initExpr.startsWith("ts_url_") ||
          initExpr.startsWith("camelcase(") ||
          initExpr.startsWith("formatOptionFlags(") ||
          initExpr.startsWith("src_cli_commander_help_formatOptionFlags(") ||
          /^(Command|Option|Argument)_\w+\(/.test(initExpr) ||
          initExpr.includes("/*__ts_str*/");
        if (!alreadyTsString &&
            (initExpr.startsWith("ts_hashmap_get(") || initExpr.startsWith("ts_value_") ||
             initExpr.startsWith("ts_array_get(") || initExpr.startsWith("ts_array_find(") ||
             (initExpr.includes(".as.object") && !initExpr.startsWith("ts_to_string(")))) {
          init = ` = ts_to_string(${initExpr})`;
        }
        // chain temps / method results already TSString* — no wrap needed
      } else if (type === "TSHashMap*") {
        if (initExpr.startsWith("ts_hashmap_new(") || initExpr.startsWith("ts_hashmap_")) {
          // ok
        } else if (initExpr.includes("Map_constructor") || initExpr === "Map_constructor()") {
          init = ` = ts_hashmap_new()`;
        } else if (initExpr.startsWith("ts_value_") || initExpr.startsWith("ts_hashmap_get(")) {
          init = ` = ((TSHashMap*)${initExpr}.as.object)`;
        }
      } else if (type === "TSArray*") {
        if (initExpr.startsWith("ts_value_array(")) {
          init = ` = ${initExpr.replace(/^ts_value_array\((.+)\)$/, "$1")}`;
        } else if (initExpr.startsWith("ts_value_") || initExpr.startsWith("ts_hashmap_get(")) {
          init = ` = ((TSArray*)${initExpr}.as.object)`;
        }
        // ts_string_split / ts_array_* already return TSArray*
      } else if (type.endsWith("*") && !type.startsWith("TS") && type !== "Value*") {
        // Struct pointer from hashmap get
        if (initExpr.startsWith("ts_hashmap_get(") || initExpr.startsWith("ts_array_get(") ||
            initExpr.startsWith("ts_value_")) {
          init = ` = ((${type})${initExpr}.as.object)`;
        }
      } else if (type === "Value") {
        // Wrap concrete C types into Value
        if (initExpr.startsWith("ts_string_split(")) {
          init = ` = ts_value_array(${initExpr})`;
        } else if (initExpr.startsWith("ts_array_new(") || initExpr.startsWith("ts_array_from_") ||
                   initExpr.startsWith("ts_array_filter(") || initExpr.startsWith("ts_array_map(")) {
          init = ` = ts_value_array(${initExpr})`;
        } else if (
          // Already Value-returning runtime helpers — do not wrap
          initExpr.startsWith("ts_url_new(") || initExpr.startsWith("ts_url_") ||
          initExpr.startsWith("ts_buffer_") || initExpr.startsWith("ts_blob_") ||
          initExpr.startsWith("ts_headers") || initExpr.startsWith("ts_fetch") ||
          initExpr.startsWith("ts_json_") || initExpr.startsWith("ts_value_") ||
          initExpr.startsWith("node_") || initExpr.startsWith("ts_error_")
        ) {
          // keep as-is
        } else if (initExpr.startsWith("ts_string_new(") || initExpr.startsWith("ts_string_concat(") ||
            initExpr.startsWith("ts_to_string(") || initExpr.includes("/*__ts_str*/") ||
            initExpr.startsWith("ts_string_to_upper(") || initExpr.startsWith("ts_string_to_lower(") ||
            initExpr.startsWith("ts_string_trim(") || initExpr.startsWith("ts_string_substring(") ||
            initExpr.startsWith("ts_string_replace(") || initExpr.startsWith("ts_string_new_len(") ||
            initExpr.startsWith("ts_number_to_string(") || initExpr.startsWith("ts_buffer_toString_")) {
          init = ` = ts_value_string(${initExpr})`;
        } else if (initExpr.includes("_constructor(")) {
          init = ` = ts_value_object((void*)${initExpr})`;
        } else if (initExpr.startsWith("ts_string_starts_with(") || initExpr.startsWith("ts_string_ends_with(") ||
                   initExpr.startsWith("ts_string_includes(") || initExpr.startsWith("ts_string_equals(") ||
                   initExpr.startsWith("ts_string_index_of(")) {
          init = ` = ts_value_boolean(${initExpr})`;
        }
        // If target is Value but init is a struct pointer (e.g., Command* from imported var),
        // wrap in Value
        const isStructPtr = initExpr.match(/^[A-Z][a-zA-Z_]+\s*\*$/) ||
          initExpr.match(/^[a-z_]+_[a-z_]+$/) && !initExpr.startsWith("ts_") &&
          !initExpr.startsWith("node_") && !initExpr.startsWith("Value") &&
          !initExpr.startsWith("(") && !initExpr.startsWith("{") &&
          !initExpr.includes("->") && !initExpr.includes("ts_value_") &&
          !initExpr.includes("ts_hashmap_") && !initExpr.includes("/*__ts_str*/") &&
          !initExpr.includes("TSString*") && !initExpr.includes("as.object") &&
          !initExpr.includes("malloc");
        if (isStructPtr && !initExpr.startsWith("ts_value_") && !initExpr.startsWith("ts_hashmap_")) {
          init = ` = ((Value){.tag = TAG_OBJECT, .as.object = (void*)${initExpr}})`;
        }
      }
    }

    // If the initializer is a builtin call that returns Value but the variable is TSString*,
    // wrap with TS_EXTRACT_STRING
    if (node.init && type === "TSString*" && init.includes("node_")) {
      init = ` = TS_EXTRACT_STRING(${init.substring(3)})`; // Remove " = " and wrap
    }

    // Register variable type for console.log wrapping
    this.exprEmitter.declareVar(node.name, type);
    const cName = sanitizeCIdentifier(node.name);
    return `${qualifier}${type} ${cName}${init};`;
  }

  private emitAssignment(node: CNode): string {
    return `${this.exprEmitter.emit(node)};`;
  }

  /** Coerce condition expressions that yield Value into a C scalar via ts_to_boolean */
  private asCondition(cond: string, node?: CNode): string {
    if (!cond) return cond;
    // Struct int fields: arg->variadic, opt->hidden, self->required — leave alone
    if (/->(variadic|required|optional|hidden|mandatory|negate|_hidden|_helpEnabled|_exitOverride|_allowUnknownOption|_allowExcessArguments|_defaultCommand)\b/.test(cond)) {
      return cond;
    }
    // TSString* / TSArray* pointer fields — null check, NOT ts_to_boolean
    if (/->(short_|long_|flags|description|_name|_description|_version|envVar|argChoices|_conflicts)\b/.test(cond) ||
        (node?.kind === "property_access" && (() => {
          // resolve via emitter when possible
          return false;
        })())) {
      // short_/long_/argChoices etc. are pointers — truthy if non-null
      if (!cond.startsWith("ts_to_boolean(") && !cond.startsWith("!")) {
        // leave as pointer for if(ptr)
        return cond;
      }
    }
    // Already a C scalar boolean / int expression — leave alone
    if (cond.startsWith("ts_to_boolean(") ||
        cond.startsWith("ts_string_starts_with(") || cond.startsWith("ts_string_ends_with(") ||
        cond.startsWith("ts_string_includes(") || cond.startsWith("ts_string_equals(") ||
        cond.startsWith("ts_string_index_of(") || cond.startsWith("ts_array_some(") ||
        cond.startsWith("ts_array_every(") || cond.startsWith("ts_array_index_of(") ||
        cond.startsWith("ts_hashmap_has(") ||
        (cond.includes(" >= 0)") && cond.includes("ts_array_index_of")) ||
        /^\(/.test(cond) && (cond.includes(" || ") || cond.includes(" && "))) {
      return cond;
    }
    // Class method returning int (Option_isBoolean) — leave alone
    if (/^(Option|Command|Argument)_is\w+\(/.test(cond) ||
        (/^(Option|Command|Argument)_\w+\(/.test(cond) && /isBoolean|isDefault/.test(cond))) {
      return cond;
    }
    // Function pointer / action fields — null check
    if (/->_action\b/.test(cond) || /->_argParser\b/.test(cond)) {
      return cond;
    }
    // Already boolean-coerced
    if (cond.startsWith("!") || cond.startsWith("!!")) {
      // bare !Value still needs wrapping — handle below if needed
    }
    // Property access: decide by field type — but only for typed struct pointers.
    // Value/hashmap property access always emits ts_hashmap_get → needs ts_to_boolean.
    if (node?.kind === "property_access") {
      if (cond.startsWith("ts_hashmap_get(") || cond.startsWith("ts_value_")) {
        if (cond.startsWith("!")) return `!ts_to_boolean(${cond.slice(1)})`;
        return `ts_to_boolean(${cond})`;
      }
      const prop = node.property || "";
      // Value fields on structs
      if (["defaultValue", "presetArg", "_opts", "_implies", "_outputConfig"].includes(prop)) {
        if (cond.startsWith("!")) {
          return `!ts_to_boolean(${cond.slice(1)})`;
        }
        return `ts_to_boolean(${cond})`;
      }
      // int fields on structs
      if (["variadic", "required", "optional", "hidden", "mandatory", "negate",
           "_hidden", "_helpEnabled", "_exitOverride", "_allowUnknownOption",
           "_allowExcessArguments", "_defaultCommand"].includes(prop)) {
        return cond;
      }
      // pointer fields (TSString*, TSArray*, function pointers)
      if (["short", "long", "flags", "description", "_name", "_description",
           "_version", "envVar", "argChoices", "_conflicts", "_action",
           "options", "commands", "arguments"].includes(prop)) {
        return cond; // null pointer check
      }
    }
    // Value-producing expressions cannot be used directly in if/while
    if (cond.startsWith("ts_hashmap_get(") || cond.startsWith("ts_value_") ||
        cond.startsWith("ts_fetch_") || cond.includes("ts_fetch_") ||
        cond.startsWith("ts_null(") || cond.startsWith("ts_undefined(") ||
        (node?.kind === "identifier" && this.exprEmitter.getVarType(node.name) === "Value") ||
        (node?.kind === "call_expression" && !this.isScalarCall(cond)) ||
        (node?.kind === "parenthesized" && this.needsBoolCoerce(this.exprEmitter.emit(node.expression), node.expression))) {
      // Avoid double-wrapping
      if (cond.startsWith("ts_to_boolean(")) return cond;
      // !ts_hashmap_get(...) → !ts_to_boolean(...)
      if (cond.startsWith("!") && !cond.startsWith("!ts_to_boolean(")) {
        const inner = cond.slice(1);
        if (this.needsBoolCoerce(inner, undefined)) {
          return `!ts_to_boolean(${inner})`;
        }
      }
      return `ts_to_boolean(${cond})`;
    }
    // Unary ! that already produced !ts_to_boolean or !ts_hashmap_has — leave
    if (cond.startsWith("!") || cond.startsWith("ts_hashmap_has(") || cond.startsWith("Option_isBoolean(")) {
      return cond;
    }
    return cond;
  }

  private isScalarCall(cond: string): boolean {
    return cond.startsWith("ts_string_starts_with(") || cond.startsWith("ts_string_ends_with(") ||
      cond.startsWith("ts_string_includes(") || cond.startsWith("ts_string_equals(") ||
      cond.startsWith("ts_string_index_of(") || cond.startsWith("ts_array_some(") ||
      cond.startsWith("ts_array_every(") || cond.startsWith("ts_array_index_of(") ||
      cond.startsWith("ts_to_boolean(");
  }

  private needsBoolCoerce(cond: string, node?: CNode): boolean {
    if (!cond) return false;
    if (cond.startsWith("ts_to_boolean(")) return false;
    if (cond.startsWith("ts_hashmap_get(") || cond.startsWith("ts_value_") ||
        cond.startsWith("ts_fetch_") || cond.includes("ts_fetch_") ||
        cond.startsWith("ts_null(") || cond.startsWith("ts_undefined(")) {
      return true;
    }
    if (node?.kind === "property_access") return true;
    if (node?.kind === "identifier" && this.exprEmitter.getVarType(node.name) === "Value") return true;
    if (node?.kind === "call_expression") {
      // Many calls return Value (fetch helpers, hashmap methods)
      const emitted = cond;
      if (emitted.startsWith("ts_") || emitted.startsWith("node_")) return true;
    }
    return false;
  }

  private emitIfStatement(node: CNode): string {
    const condition = this.asCondition(this.exprEmitter.emit(node.condition), node.condition);
    const thenBody = this.emitBlock(node.then);
    const elseBody = node.else ? ` else ${this.emitBlock(node.else)}` : "";
    return `if (${condition}) ${thenBody}${elseBody}`;
  }

  private emitWhileStatement(node: CNode): string {
    const condition = this.asCondition(this.exprEmitter.emit(node.condition), node.condition);
    const body = this.emitBlock(node.body);
    return `while (${condition}) ${body}`;
  }

  private emitDoWhileStatement(node: CNode): string {
    const condition = this.asCondition(this.exprEmitter.emit(node.condition), node.condition);
    const body = this.emitBlock(node.body);
    return `do ${body} while (${condition});`;
  }

  private emitForStatement(node: CNode): string {
    const init = node.init ? this.emit(node.init).replace(/;$/, "") : "";
    const condition = node.condition
      ? this.asCondition(this.exprEmitter.emit(node.condition), node.condition)
      : "";
    const update = node.update ? this.exprEmitter.emit(node.update) : "";
    const body = this.emitBlock(node.body);
    return `for (${init}; ${condition}; ${update}) ${body}`;
  }

  private emitForOfStatement(node: CNode): string {
    // for...of → simplified to index-based for loop
    let iterable = this.exprEmitter.emit(node.iterable);
    // Coerce Value-typed iterables (hashmap get / method results) to TSArray*
    if (iterable.startsWith("ts_hashmap_get(") || iterable.startsWith("ts_value_") ||
        (node.iterable?.kind === "identifier" && this.exprEmitter.getVarType(node.iterable.name) === "Value")) {
      iterable = `((TSArray*)${iterable}.as.object)`;
    } else if (iterable.startsWith("ts_value_array(")) {
      iterable = iterable.replace(/^ts_value_array\((.+)\)$/, "$1");
    }
    const iterVar = node.iterVar;
    const varName = iterVar?.name || "i";
    let varType = iterVar?.type || "Value";
    if (varType === "string") varType = "TSString*";
    if (varType === "number") varType = "double";
    if (varType === "boolean") varType = "int";
    // Function pointer types (ActionHandler) are not valid C declarators as written —
    // store hooks as Value and cast on call
    if (varType.includes("(*)") || varType.includes("ActionHandler")) {
      varType = "Value";
    }
    // Register so method calls inside the body (e.g. token.startsWith) dispatch correctly
    this.exprEmitter.declareVar(varName, varType);
    const body = this.emitBlock(node.body);

    // ts_array_get returns Value; coerce to the target type if needed
    const rawGet = this.exprEmitter.emit({ kind: "element_access", object: { kind: "identifier", name: "__iter" }, index: { kind: "identifier", name: "__i" }, objectType: "array" });
    let initExpr = rawGet;
    if (varType === "TSString*" || varType === "string") {
      initExpr = `ts_to_string(${rawGet})`;
    } else if (varType === "double" || varType === "number") {
      initExpr = `ts_to_number(${rawGet})`;
    } else if (varType === "int" || varType === "boolean") {
      initExpr = `ts_to_boolean(${rawGet})`;
    } else if (varType.endsWith("*") && !varType.startsWith("TS") && !varType.includes("(")) {
      // Struct pointer types (Command*, Option*, Argument*, etc.)
      initExpr = `((${varType})${rawGet}.as.object)`;
    }

    return `{
  TSArray* __iter = ${iterable};
  for (int32_t __i = 0; __i < __iter->length; __i++) {
    ${varType} ${varName} = ${initExpr};
${body}
  }
}`;
  }

  private emitForInStatement(node: CNode): string {
    // for...in → iterate over object keys (simplified)
    const iterable = this.exprEmitter.emit(node.iterable);
    const iterVar = node.iterVar;
    const varName = iterVar?.name || "key";

    return `/* for...in: ${iterable} — requires runtime iteration support */`;
  }

  private emitReturnStatement(node: CNode): string {
    if (node.value) {
      let emitted = this.exprEmitter.emit(node.value);
      const ret = this.currentReturnType;
      // Coerce Value → TSString* when function returns string
      if ((ret === "TSString*" || ret === "string") &&
          (emitted.startsWith("ts_array_get(") || emitted.startsWith("ts_hashmap_get(") ||
           emitted.startsWith("ts_value_") || emitted.startsWith("ts_array_find(") ||
           emitted.startsWith("ts_array_reduce("))) {
        emitted = `ts_to_string(${emitted})`;
      }
      // Coerce Class* → Value when function returns Value (chainable methods already Class*)
      if (ret === "Value" && emitted === "self") {
        emitted = `((Value){.tag = TAG_OBJECT, .as.object = self})`;
      }
      // Returning null/undefined from Class* function → NULL pointer
      if ((ret.endsWith("*") && !ret.startsWith("TS") && ret !== "Value*") &&
          (emitted === "ts_value_null()" || emitted === "ts_value_undefined()" ||
           emitted.startsWith("ts_value_null(") || emitted.startsWith("ts_null("))) {
        emitted = "NULL";
      }
      // Returning Value from Class* function — unwrap object pointer if possible
      if ((ret.endsWith("*") && !ret.startsWith("TS") && ret !== "Value*") &&
          emitted.startsWith("ts_value_object(")) {
        // leave or unwrap
      }
      if ((ret.endsWith("*") && !ret.startsWith("TS") && ret !== "Value*") &&
          emitted.startsWith("((Value)")) {
        // wrong wrap of self was already handled; null case above
      }
      // Coerce Value field wrap: self->_opts is already Value — don't wrap again wrong
      if (ret === "Value" && emitted.startsWith("((Value){.tag = TAG_OBJECT, .as.object = self->_opts})")) {
        emitted = `self->_opts`;
      }
      // int-returning functions should not return Value expressions without coerce
      if ((ret === "int" || ret === "boolean") &&
          (emitted.startsWith("ts_value_") || emitted.startsWith("ts_hashmap_get("))) {
        emitted = `ts_to_boolean(${emitted})`;
      }
      // Value-returning closures that call void/int exit helpers
      if (ret === "Value" && (
          emitted.includes("__exit(") || emitted.includes("_exit(") ||
          /InvalidArgumentError__exit|CommanderError__exit|Command__exit/.test(emitted))) {
        // void-ish exit: evaluate for side effect and return undefined
        if (emitted.startsWith("return ")) {
          // shouldn't happen
        } else {
          emitted = `((void)(${emitted}), ts_value_undefined())`;
        }
      }
      return `return ${emitted};`;
    }
    // Bare return in Value-returning closures must still return a Value
    if (this.currentReturnType === "Value") {
      return "return ts_value_undefined();";
    }
    return "return;";
  }

  private emitExpressionStatement(node: CNode): string {
    return `${this.exprEmitter.emit(node.expression)};`;
  }

  private emitBlock(node: CNode): string {
    const stmts = (node.statements || [])
      .map((s: CNode) => this.emit(s))
      .join("\n  ");
    return `{\n  ${stmts}\n}`;
  }

  private emitTryStatement(node: CNode): string {
    const tryBody = this.emitBlock(node.tryBlock);
    if (!node.catchClause) {
      return `TS_TRY ${tryBody}`;
    }
    const errorVar = node.catchClause.errorVar || "err";
    const catchBody = this.emitBlock({ kind: "block", statements: node.catchClause.body });
    return `TS_TRY ${tryBody} TS_CATCH {\n  Value ${errorVar} = _ts_current_error.error_value;\n${catchBody}\n}`;
  }

  private emitThrowStatement(node: CNode): string {
    const val = node.value;
    let emitted = this.exprEmitter.emit(val);
    // TS_THROW expects Value
    if (!emitted.startsWith("ts_value_") && !emitted.startsWith("ts_null(") &&
        !emitted.startsWith("ts_undefined(") && !emitted.startsWith("ts_error_")) {
      if (val?.kind === "string_literal") {
        emitted = `ts_value_string(${emitted})`;
      } else if (val?.kind === "number_literal") {
        emitted = `ts_value_number(${emitted})`;
      } else if (val?.kind === "boolean_literal") {
        emitted = `ts_value_boolean(${emitted})`;
      } else if (emitted.startsWith("ts_string_new(") || emitted.startsWith("ts_string_concat(") ||
                 emitted.startsWith("ts_to_string(")) {
        emitted = `ts_value_string(${emitted})`;
      } else if (emitted.includes("_constructor(") ||
                 /^(CommanderError|InvalidArgumentError|Error)_constructor\(/.test(emitted) ||
                 (val?.kind === "new_expression")) {
        emitted = `ts_value_object((void*)${emitted})`;
      } else if (/^[A-Z][A-Za-z0-9_]*_constructor\(/.test(emitted)) {
        emitted = `ts_value_object((void*)${emitted})`;
      } else {
        emitted = `ts_value_string(ts_to_string(${emitted}))`;
      }
    }
    return `TS_THROW(${emitted});`;
  }

  private emitSwitchStatement(node: CNode): string {
    const expr = this.exprEmitter.emit(node.expression);
    const cases = (node.cases || [])
      .map((c: any) => {
        const test = c.test ? `case ${this.exprEmitter.emit(c.test)}` : "default";
        const stmts = (c.statements || []).map((s: CNode) => `    ${this.emit(s)}`).join("\n");
        return `  ${test}:\n${stmts}\n    break;`;
      })
      .join("\n");
    return `switch (${expr}) {\n${cases}\n}`;
  }

  private emitModuleLevelCode(node: CNode): string {
    return node.expression
      ? this.exprEmitter.emit(node.expression)
      : "";
  }
}
