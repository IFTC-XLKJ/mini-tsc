import * as assert from "assert";

function main(): void {
  assert.ok(true);
  assert.ok(1, "one is truthy");
  assert.equal(1, "1");
  assert.strictEqual(2, 2);
  assert.notEqual(1, 2);
  assert.notStrictEqual(1, "1");

  assert.deepEqual({ a: 1 }, { a: 1 });
  assert.deepStrictEqual([1, 2], [1, 2]);
  assert.notDeepEqual({ a: 1 }, { a: 2 });

  assert.ifError(null);

  assert.throws(() => {
    throw "boom";
  });

  assert.doesNotThrow(() => {
    const x = 1 + 1;
    void x;
  });

  assert.match("hello world", "world");
  assert.doesNotMatch("hello", "xyz");

  console.log("all assertions passed");
}
main();
