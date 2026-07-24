import * as ts from "typescript";
import {
  type ResolvedType,
  type ResolvedField,
  type ResolvedInterface,
  type ResolvedClass,
  type ResolvedEnum,
  type VTableEntry,
  type GenericSpecialization,
  TS_NUMBER,
  TS_BOOLEAN,
  TS_VOID,
  TS_NULL,
  TS_UNDEFINED,
  TS_NEVER,
  TS_ANY,
  TS_UNKNOWN,
  tsString,
  tsArray,
} from "./type-info.js";

export class TypeMapper {
  private checker: ts.TypeChecker;
  private specializationCache = new Map<string, GenericSpecialization>();

  constructor(checker: ts.TypeChecker) {
    this.checker = checker;
  }

  mapType(tsType: ts.Type): ResolvedType {
    const typeStr = this.checker.typeToString(tsType);

    // Primitives
    if (typeStr === "number") return TS_NUMBER;
    if (typeStr === "string") return tsString();
    if (typeStr === "boolean") return TS_BOOLEAN;
    if (typeStr === "void") return TS_VOID;
    if (typeStr === "null") return TS_NULL;
    if (typeStr === "undefined") return TS_UNDEFINED;
    if (typeStr === "never") return TS_NEVER;
    if (typeStr === "any") return TS_ANY;
    if (typeStr === "unknown") return TS_UNKNOWN;

    // Array types
    if (tsType.flags & ts.TypeFlags.Object) {
      const objectType = tsType as ts.ObjectType;
      const symbol = objectType.symbol;
      if (symbol) {
        const name = symbol.getName();

        // Built-in array
        if (name === "Array" || name === "ReadonlyArray") {
          const typeArgs = this.checker.getTypeArguments(objectType as ts.TypeReference);
          if (typeArgs.length > 0) {
            return tsArray(this.mapType(typeArgs[0]));
          }
          return tsArray(TS_ANY);
        }

        // Tuple types
        if (objectType.flags & ts.TypeFlags.Object) {
          const flags = (objectType as any).objectFlags;
          if (flags & ts.ObjectFlags.Tuple) {
            return this.mapTupleType(tsType);
          }
        }

        // Promise
        if (name === "Promise") {
          return { kind: "promise", cType: "Value", cHeader: "ts_runtime.h" };
        }

        // Map/Record
        if (name === "Map" || name === "ReadonlyMap") {
          return { kind: "map", cType: "TSHashMap*", cHeader: "ts_runtime.h" };
        }
        if (name === "Record") {
          return { kind: "map", cType: "TSHashMap*", cHeader: "ts_runtime.h" };
        }

        // Date type - use double (timestamp)
        if (name === "Date") {
          return { kind: "class", cType: "double" };
        }

        // Buffer - use Value (tagged union wrapping Buffer struct)
        if (name === "Buffer") {
          return { kind: "class", cType: "Value", cHeader: "runtime.h" };
        }

        // HTTP Server / IncomingMessage from our d.ts → Value (runtime hashmap/object)
        if (name === "Server" || name === "IncomingMessage" || name === "IncomingHttpHeaders") {
          return { kind: "any", cType: "Value", cHeader: "ts_runtime.h" };
        }

        // Worker threads types - all map to Value (runtime hashmap/object)
        if (name === "Worker" || name === "MessageChannel" || name === "MessagePort" ||
            name === "BroadcastChannel") {
          return { kind: "any", cType: "Value", cHeader: "ts_runtime.h" };
        }

        // Class types - return ClassName*
        // Check if this is a class (not a built-in type)
        if (objectType.symbol?.declarations) {
          const decl = objectType.symbol.declarations[0];
          if (decl && ts.isClassDeclaration(decl)) {
            return { kind: "class", cType: `${name}*` };
          }
          // Interface types from ambient modules → Value
          if (decl && ts.isInterfaceDeclaration(decl)) {
            return { kind: "any", cType: "Value", cHeader: "ts_runtime.h" };
          }
        }
      }
    }

    // Union types
    if (tsType.flags & ts.TypeFlags.Union) {
      return this.mapUnionType(tsType);
    }

    // Intersection types
    if (tsType.flags & ts.TypeFlags.Intersection) {
      return this.mapIntersectionType(tsType);
    }

    // Literal types
    if (tsType.flags & (ts.TypeFlags.StringLiteral | ts.TypeFlags.NumberLiteral | ts.TypeFlags.BooleanLiteral)) {
      if (tsType.flags & ts.TypeFlags.StringLiteral) return tsString();
      if (tsType.flags & ts.TypeFlags.NumberLiteral) return TS_NUMBER;
      if (tsType.flags & ts.TypeFlags.BooleanLiteral) return TS_BOOLEAN;
    }

    // Function types
    if (tsType.flags & ts.TypeFlags.Object) {
      const sigs = tsType.getCallSignatures();
      if (sigs.length > 0) {
        return this.mapFunctionType(sigs[0]);
      }
    }

    // Fallback to Value (tagged union)
    return TS_ANY;
  }

  private mapTupleType(tsType: ts.Type): ResolvedType {
    const objectType = tsType as ts.ObjectType;
    const typeArgs = this.checker.getTypeArguments(objectType as ts.TypeReference);
    const fields = typeArgs.map((t, i) => ({
      name: `_${i}`,
      type: this.mapType(t),
    }));
    const structFields = fields.map(f => `  ${f.type.cType} ${f.name};`).join("\n");
    return {
      kind: "tuple",
      cType: `struct { ${fields.map(f => `${f.type.cType} ${f.name}`).join("; ")} }`,
    };
  }

  private mapUnionType(tsType: ts.Type): ResolvedType {
    const unionType = tsType as ts.UnionType;
    const types = unionType.types;

    // T | undefined / T | null for simple T → just T (optional values become Value if complex)
    const nonNullish = types.filter(t =>
      !(t.flags & (ts.TypeFlags.Undefined | ts.TypeFlags.Null | ts.TypeFlags.Void))
    );

    // If all non-nullish types are literal, it might be a string/number/boolean union (enum-like)
    const allStrings = nonNullish.length > 0 && nonNullish.every(t => t.flags & ts.TypeFlags.StringLiteral);
    const allNumbers = nonNullish.length > 0 && nonNullish.every(t => t.flags & ts.TypeFlags.NumberLiteral);
    const allBooleans = nonNullish.length > 0 && nonNullish.every(t => t.flags & (ts.TypeFlags.BooleanLiteral | ts.TypeFlags.Boolean));
    if (allStrings) return tsString();
    if (allNumbers) return TS_NUMBER;
    if (allBooleans) return TS_BOOLEAN;

    if (nonNullish.length === 1) {
      return this.mapType(nonNullish[0]);
    }

    // Complex object/interface unions (e.g. ReadableStreamDefaultReader | undefined with generics)
    // → Value; never emit invalid C identifiers like struct Foo<Bar>
    const isSimple = (t: ts.Type): boolean => {
      if (t.flags & (ts.TypeFlags.String | ts.TypeFlags.Number | ts.TypeFlags.Boolean |
          ts.TypeFlags.StringLiteral | ts.TypeFlags.NumberLiteral | ts.TypeFlags.BooleanLiteral |
          ts.TypeFlags.Undefined | ts.TypeFlags.Null | ts.TypeFlags.Void | ts.TypeFlags.Any |
          ts.TypeFlags.Unknown)) {
        return true;
      }
      const s = this.checker.typeToString(t);
      if (s === "string" || s === "number" || s === "boolean" || s === "any" || s === "unknown") {
        return true;
      }
      // Generics / interfaces with < > spaces → not simple
      if (/[<>\s\[\]|,]/.test(s)) return false;
      return false;
    };
    if (!types.every(isSimple)) {
      return TS_ANY;
    }

    // Tagged union: generate struct with tag + variants (sanitized names only)
    const sanitize = (s: string) => s.replace(/[^a-zA-Z0-9_]/g, "_").replace(/^(\d)/, "_$1");
    const variants = types.map((t, i) => ({
      tag: i,
      type: this.mapType(t),
      typeName: sanitize(this.checker.typeToString(t)),
    }));
    const uniqueName = `Union_${variants.map(v => v.typeName).join("_")}`;

    return {
      kind: "union",
      cType: `struct ${uniqueName}`,
      cHeader: "ts_runtime.h",
    };
  }

  private mapIntersectionType(tsType: ts.Type): ResolvedType {
    // Intersection = merged struct
    return TS_ANY; // Simplified — full impl would merge fields
  }

  mapFunctionType(sig: ts.Signature): ResolvedType {
    const params: ResolvedField[] = sig.parameters.map(p => {
      const paramType = this.checker.getTypeOfSymbolAtLocation(p, p.valueDeclaration!);
      return { name: p.getName(), type: this.mapType(paramType) };
    });
    const returnType = this.mapType(this.checker.getReturnTypeOfSignature(sig));
    const paramStr = params.map(p => p.type.cType).join(", ");
    return {
      kind: "function",
      cType: `${returnType.cType} (*)(${paramStr || "void"})`,
      cHeader: "ts_runtime.h",
    };
  }

  mapInterface(declaration: ts.InterfaceDeclaration): ResolvedInterface {
    const fields: ResolvedField[] = [];
    for (const member of declaration.members) {
      if (ts.isPropertySignature(member) && member.name) {
        const propName = member.name.getText();
        const resolvedType = member.type
          ? this.mapType(this.checker.getTypeAtLocation(member))
          : TS_ANY;
        fields.push({ name: propName, type: resolvedType });
      }
    }
    return { name: declaration.name.text, fields };
  }

  mapClass(declaration: ts.ClassDeclaration): ResolvedClass {
    const fields: ResolvedField[] = [];
    const vtable: VTableEntry[] = [];

    for (const member of declaration.members) {
      if (ts.isPropertyDeclaration(member) && member.name) {
        const propName = member.name.getText();
        const resolvedType = member.type
          ? this.mapType(this.checker.getTypeAtLocation(member))
          : TS_ANY;
        fields.push({ name: propName, type: resolvedType });
      } else if (ts.isMethodDeclaration(member) && member.name) {
        const methodName = member.name.getText();
        const sig = this.checker.getTypeAtLocation(member).getCallSignatures()[0];
        if (sig) {
          const params: ResolvedField[] = sig.parameters.map(p => ({
            name: p.getName(),
            type: this.mapType(
              this.checker.getTypeOfSymbolAtLocation(p, p.valueDeclaration!)
            ),
          }));
          vtable.push({
            methodName,
            cFunctionName: `${declaration.name?.text || "Class"}_${methodName}`,
            paramTypes: params.map(p => p.type),
            returnType: this.mapType(this.checker.getReturnTypeOfSignature(sig)),
          });
        }
      }
    }

    return {
      name: declaration.name?.text || "AnonymousClass",
      fields,
      vtable,
    };
  }

  mapEnum(declaration: ts.EnumDeclaration): ResolvedEnum {
    const values: { name: string; value?: number }[] = [];
    const isConst = declaration.modifiers?.some(
      m => m.kind === ts.SyntaxKind.ConstKeyword
    ) ?? false;

    declaration.members.forEach((member, index) => {
      const name = member.name.getText();
      if (member.initializer) {
        const initType = this.checker.getTypeAtLocation(member.initializer);
        if (initType.flags & ts.TypeFlags.NumberLiteral) {
          values.push({ name, value: (initType as ts.LiteralType).value as number });
          return;
        }
      }
      values.push({ name, value: index });
    });

    return { name: declaration.name.text, values, isConst };
  }

  getGenericSpecialization(originalName: string, typeArgs: ResolvedType[]): GenericSpecialization {
    const suffix = typeArgs.map(t => t.cType.replace(/[^a-zA-Z0-9]/g, "_")).join("_");
    const mangledName = `${originalName}_${suffix}`;
    const key = mangledName;

    if (this.specializationCache.has(key)) {
      return this.specializationCache.get(key)!;
    }

    const spec: GenericSpecialization = { originalName, typeArgs, mangledName };
    this.specializationCache.set(key, spec);
    return spec;
  }
}
