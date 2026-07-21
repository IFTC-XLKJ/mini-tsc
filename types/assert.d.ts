/** Node.js `assert` ambient types for mini-tsc. */
declare module "assert" {
  type Message = string | Error | undefined;

  function ok(value: any, message?: Message): asserts value;
  function equal(actual: any, expected: any, message?: Message): void;
  function notEqual(actual: any, expected: any, message?: Message): void;
  function strictEqual(actual: any, expected: any, message?: Message): void;
  function notStrictEqual(actual: any, expected: any, message?: Message): void;
  function deepEqual(actual: any, expected: any, message?: Message): void;
  function deepStrictEqual(actual: any, expected: any, message?: Message): void;
  function notDeepEqual(actual: any, expected: any, message?: Message): void;
  function notDeepStrictEqual(actual: any, expected: any, message?: Message): void;
  function fail(message?: Message): never;
  function ifError(value: any): asserts value is null | undefined;
  function throws(fn: () => any, error?: any, message?: Message): void;
  function doesNotThrow(fn: () => any, message?: Message): void;
  function match(value: string, regexp: RegExp | any, message?: Message): void;
  function doesNotMatch(value: string, regexp: RegExp | any, message?: Message): void;

  /** Callable assert (assert(value)) plus named methods. */
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
    ok,
    equal,
    notEqual,
    strictEqual,
    notStrictEqual,
    deepEqual,
    deepStrictEqual,
    notDeepEqual,
    notDeepStrictEqual,
    fail,
    ifError,
    throws,
    doesNotThrow,
    match,
    doesNotMatch,
  };
  export default assert;
}
