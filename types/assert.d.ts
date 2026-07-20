/** Minimal Node.js `assert` ambient types for mini-tsc. */
declare module "assert" {
  function ok(value: any, message?: string | Error): asserts value;
  function equal(actual: any, expected: any, message?: string | Error): void;
  function notEqual(actual: any, expected: any, message?: string | Error): void;
  function strictEqual(actual: any, expected: any, message?: string | Error): void;
  function notStrictEqual(actual: any, expected: any, message?: string | Error): void;
  function deepEqual(actual: any, expected: any, message?: string | Error): void;
  function deepStrictEqual(actual: any, expected: any, message?: string | Error): void;
  function notDeepEqual(actual: any, expected: any, message?: string | Error): void;
  function notDeepStrictEqual(actual: any, expected: any, message?: string | Error): void;
  function fail(message?: string | Error): never;
  function ifError(value: any): asserts value is null | undefined;
  function throws(fn: () => any, error?: any): void;
  function doesNotThrow(fn: () => any, message?: string | Error): void;
  function match(value: string, regexp: any, message?: string | Error): void;
  function doesNotMatch(value: string, regexp: any, message?: string | Error): void;

  const assert: typeof ok & {
    ok: typeof ok;
    equal: typeof equal;
    notEqual: typeof notEqual;
    strictEqual: typeof strictEqual;
    notStrictEqual: typeof notStrictEqual;
    deepEqual: typeof deepEqual;
    deepStrictEqual: typeof deepStrictEqual;
    notDeepEqual: typeof notDeepEqual;
    notDeepStrictEqual: typeof notDeepStrictEqual;
    fail: typeof fail;
    ifError: typeof ifError;
    throws: typeof throws;
    doesNotThrow: typeof doesNotThrow;
    match: typeof match;
    doesNotMatch: typeof doesNotMatch;
  };

  export {
    ok, equal, notEqual, strictEqual, notStrictEqual,
    deepEqual, deepStrictEqual, notDeepEqual, notDeepStrictEqual,
    fail, ifError, throws, doesNotThrow, match, doesNotMatch,
  };
  export default assert;
}
