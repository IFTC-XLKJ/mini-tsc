/**
 * Resolved type IR — intermediate representation between TS types and C code.
 */

export type TypeKind =
  | "number"
  | "string"
  | "boolean"
  | "void"
  | "null"
  | "undefined"
  | "never"
  | "any"
  | "unknown"
  | "object"
  | "interface"
  | "class"
  | "enum"
  | "union"
  | "intersection"
  | "tuple"
  | "array"
  | "map"
  | "function"
  | "generic"
  | "optional"
  | "literal"
  | "symbol"
  | "promise"
  | "custom";

export interface ResolvedType {
  kind: TypeKind;
  cType: string;
  cHeader?: string;
}

export interface ResolvedField {
  name: string;
  type: ResolvedType;
}

export interface ResolvedInterface {
  name: string;
  fields: ResolvedField[];
}

export interface VTableEntry {
  methodName: string;
  cFunctionName: string;
  paramTypes: ResolvedType[];
  returnType: ResolvedType;
}

export interface ResolvedClass {
  name: string;
  fields: ResolvedField[];
  vtable: VTableEntry[];
  extends?: string;
  implements?: string[];
}

export interface ResolvedEnum {
  name: string;
  values: { name: string; value?: number }[];
  isConst: boolean;
}

export interface ResolvedUnion {
  name: string;
  variants: { tag: number; type: ResolvedType; typeName: string }[];
}

export interface ResolvedFunction {
  params: ResolvedField[];
  returnType: ResolvedType;
  isAsync: boolean;
  capturedVars?: ResolvedField[];
}

export interface GenericSpecialization {
  originalName: string;
  typeArgs: ResolvedType[];
  mangledName: string;
}

/** Primitive type constants */
export const TS_NUMBER: ResolvedType = { kind: "number", cType: "double" };
export const TS_BOOLEAN: ResolvedType = { kind: "boolean", cType: "int" };
export const TS_VOID: ResolvedType = { kind: "void", cType: "void" };
export const TS_NULL: ResolvedType = { kind: "null", cType: "void*" };
export const TS_UNDEFINED: ResolvedType = { kind: "undefined", cType: "void*" };
export const TS_NEVER: ResolvedType = { kind: "never", cType: "void" };
export const TS_ANY: ResolvedType = { kind: "any", cType: "Value", cHeader: "ts_runtime.h" };
export const TS_UNKNOWN: ResolvedType = { kind: "unknown", cType: "Value", cHeader: "ts_runtime.h" };

export function tsString(): ResolvedType {
  return { kind: "string", cType: "TSString*", cHeader: "ts_runtime.h" };
}

export function tsArray(elementType: ResolvedType): ResolvedType {
  return { kind: "array", cType: "TSArray*", cHeader: "ts_runtime.h" };
}

export function tsOptional(inner: ResolvedType): ResolvedType {
  return { kind: "optional", cType: `${inner.cType}*` };
}

export function tsFunction(params: ResolvedField[], returnType: ResolvedType): ResolvedType {
  const paramStr = params.map(p => p.type.cType).join(", ");
  return {
    kind: "function",
    cType: `${returnType.cType} (*)(${paramStr})`,
    cHeader: "ts_runtime.h",
  };
}
