import * as quickjs from "quickjs";

function main(): void {
  const ctx = quickjs.createContext();

  // Simple arithmetic: number crosses the boundary as double.
  console.log(quickjs.eval(ctx, "1 + 2 * 3"));

  // String result.
  console.log(quickjs.eval(ctx, "'hello ' + 'world'"));

  // Array result: JS array -> TS array.
  const arr = quickjs.eval(ctx, "[1, 2, 3, 4]");
  console.log(arr.length);
  console.log(arr[0]);
  console.log(arr[3]);

  // Object result: JS object -> TS object.
  const obj = quickjs.eval(ctx, "({ name: 'Alice', age: 30 })");
  console.log(obj.name);
  console.log(obj.age);

  // Inject a global from TS, then read it back in JS.
  quickjs.setGlobal(ctx, "answer", 42);
  console.log(quickjs.eval(ctx, "answer * 2"));

  // Define a function in JS, call it from JS, read result.
  quickjs.eval(ctx, "function double(x) { return x * 2; }");
  console.log(quickjs.eval(ctx, "double(21)"));

  // getGlobal reads a binding defined in JS.
  quickjs.eval(ctx, "var greeting = 'hi from js';");
  console.log(quickjs.getGlobal(ctx, "greeting"));

  // eval returning a JS function -> callable handle; call it with args.
  const scale = quickjs.eval(ctx, "(x) => x * 10");
  console.log(quickjs.call(ctx, scale, [4]));

  // Several args, string result.
  const join2 = quickjs.eval(ctx, "(a, b) => a + '-' + b");
  console.log(quickjs.call(ctx, join2, ["x", "y"]));

  // No args (empty array).
  const seven = quickjs.eval(ctx, "() => 7");
  console.log(quickjs.call(ctx, seven, []));

  // getGlobal on a JS-defined function -> callable handle.
  const dbl = quickjs.getGlobal(ctx, "double");
  console.log(quickjs.call(ctx, dbl, [50]));

  // A TS function handed to JS; JS invokes it and returns the result.
  quickjs.setGlobal(ctx, "tag", (s: any) => "ts:" + s);
  console.log(quickjs.eval(ctx, "tag('a')"));
  console.log(quickjs.eval(ctx, "[1, 2, 3].map(function (n) { return tag('n' + n); }).join(',')"));

  // Errors surface as thrown TS errors.
  try {
    quickjs.eval(ctx, "throw new Error('boom');");
  } catch (e: any) {
    console.log("caught:", e.message);
  }

  // Memory accounting is observable, and a limit is enforceable.
  const usage = quickjs.getMemoryUsage(ctx);
  console.log("usage mallocSize:", usage.mallocSize);

  // Set a comfortable limit, then confirm a heavy allocation trips it.
  quickjs.setMemoryLimit(ctx, 256 * 1024 * 1024);
  console.log(quickjs.eval(ctx, "'under limit'"));

  quickjs.setMemoryLimit(ctx, 1024 * 1024);
  let tripped = false;
  try {
    quickjs.eval(ctx, "var a = []; for (var i = 0; i < 1000000; i++) a.push('xxxxxxxxxx');");
  } catch (e: any) {
    tripped = true;
  }
  console.log("limit tripped:", tripped);

  // 0 disables the limit again.
  quickjs.setMemoryLimit(ctx, 0);
  console.log(quickjs.eval(ctx, "'unlimited again'"));

  // --- Engine info & control ---
  console.log("version:", quickjs.version());
  quickjs.setRuntimeInfo(ctx, "mini-tsc-quickjs-test");
  quickjs.setGCThreshold(ctx, 4 * 1024 * 1024);
  quickjs.setMaxStackSize(ctx, 4 * 1024 * 1024);
  quickjs.gc(ctx);
  console.log("after gc:", quickjs.eval(ctx, "'ok'"));

  // --- Global reflection ---
  quickjs.setGlobal(ctx, "answer", 42);
  console.log("hasGlobal answer:", quickjs.hasGlobal(ctx, "answer"));
  console.log("typeofGlobal answer:", quickjs.typeofGlobal(ctx, "answer"));
  console.log("hasGlobal nope:", quickjs.hasGlobal(ctx, "nope"));
  const gk = quickjs.globalKeys(ctx);
  console.log("globalKeys is array:", quickjs.isArray(gk));
  console.log("deleteGlobal answer:", quickjs.deleteGlobal(ctx, "answer"));
  console.log("hasGlobal after delete:", quickjs.hasGlobal(ctx, "answer"));

  // --- JSON helpers ---
  const parsed = quickjs.parseJSON(ctx, '{"a":1,"b":[2,3]}');
  console.log("parseJSON a:", parsed.a);
  console.log("parseJSON b0:", parsed.b[0]);
  console.log("stringify:", quickjs.stringify(ctx, parsed));
  console.log("evalJSON:", quickjs.evalJSON(ctx, "({ x: 1, y: { z: 2 } })"));
  quickjs.setGlobalJSON(ctx, "cfg", parsed);
  console.log("getGlobalJSON:", quickjs.getGlobalJSON(ctx, "cfg"));
  console.log("roundtrip:", quickjs.eval(ctx, "cfg.a + cfg.b[1]"));

  // --- Function handles: apply / newInstance / compile ---
  quickjs.eval(ctx, "function greet(prefix) { return prefix + ':' + this.name; }");
  const greet = quickjs.getGlobal(ctx, "greet");
  const who = quickjs.eval(ctx, "({ name: 'Bob' })");
  const whoRef = quickjs.ref(ctx, who);
  console.log("apply:", quickjs.apply(ctx, greet, whoRef, ["hi"]));

  quickjs.eval(ctx, "function Point(x, y) { this.x = x; this.y = y; }");
  const Point = quickjs.getGlobal(ctx, "Point");
  const pRef = quickjs.newInstance(ctx, Point, [3, 4]);
  console.log("newInstance x:", quickjs.getProperty(ctx, pRef, "x"));
  console.log("newInstance y:", quickjs.getProperty(ctx, pRef, "y"));

  const compiled = quickjs.compile(ctx, "1 + 2 + 3");
  console.log("compile result:", compiled);

  // --- Value handles: reflect over live JS objects/arrays ---
  const objRef = quickjs.evalRef(ctx, "({ name: 'Alice', age: 30 })");
  console.log("keys:", quickjs.getKeys(ctx, objRef));
  console.log("values:", quickjs.getValues(ctx, objRef));
  console.log("entries len:", quickjs.arrayLength(ctx, quickjs.getEntries(ctx, objRef)));
  console.log("get name:", quickjs.getProperty(ctx, objRef, "name"));
  console.log("has age:", quickjs.hasProperty(ctx, objRef, "age"));
  console.log("setProperty:", quickjs.setProperty(ctx, objRef, "age", 31));
  console.log("get age after set:", quickjs.getProperty(ctx, objRef, "age"));
  console.log("delete city:", quickjs.deleteProperty(ctx, objRef, "age"));
  console.log("has age after del:", quickjs.hasProperty(ctx, objRef, "age"));

  const arrRef = quickjs.evalRef(ctx, "[1, 2, 3]");
  console.log("arrayLength:", quickjs.arrayLength(ctx, arrRef));
  console.log("arrayPush:", quickjs.arrayPush(ctx, arrRef, 4));
  console.log("get idx3:", quickjs.getProperty(ctx, arrRef, 3));
  console.log("deref:", quickjs.deref(ctx, arrRef));

  const made = quickjs.createObject(ctx, { hello: "world" });
  console.log("createObject:", quickjs.getProperty(ctx, made, "hello"));
  const madeArr = quickjs.createArray(ctx, ["a", "b"]);
  console.log("createArray length:", quickjs.arrayLength(ctx, madeArr));

  quickjs.releaseHandle(ctx, objRef);
  quickjs.releaseHandle(ctx, arrRef);

  // --- Introspection & coercion ---
  console.log("typeof number:", quickjs.typeofValue(1));
  console.log("typeof string:", quickjs.typeofValue("s"));
  console.log("typeof fn handle:", quickjs.typeofValue(greet));
  console.log("isArray:", quickjs.isArray(quickjs.eval(ctx, "[1]")));
  console.log("isObject:", quickjs.isObject(objRef));
  console.log("isString:", quickjs.isString("x"));
  console.log("isNumber:", quickjs.isNumber(5));
  console.log("isBoolean:", quickjs.isBoolean(true));
  console.log("isCallable:", quickjs.isCallable(greet));
  console.log("toNumber:", quickjs.toNumber(ctx, "42"));
  console.log("toString:", quickjs.toStringValue(ctx, 3.5));
  console.log("toBoolean:", quickjs.toBoolean(ctx, 0));
  console.log("strictEqual:", quickjs.strictEqual(ctx, 1, 1));
  console.log("strictEqual false:", quickjs.strictEqual(ctx, "1", 1));

  // --- Time limit aborts a runaway script ---
  quickjs.setTimeLimit(ctx, 200);
  let timedOut = false;
  try {
    quickjs.eval(ctx, "while (true) {}");
  } catch (e: any) {
    timedOut = true;
  }
  console.log("time limit tripped:", timedOut);
  quickjs.setTimeLimit(ctx, 0);
  console.log("limit cleared:", quickjs.getTimeLimit(ctx));

  // --- Promise microtask queue ---
  quickjs.eval(ctx, "var order = []; Promise.resolve().then(function () { order.push('micro'); });");
  console.log("pending jobs:", quickjs.runPendingJobs(ctx));
  console.log("order:", quickjs.eval(ctx, "order.join(',')"));

  // --- ES module evaluation ---
  quickjs.evalModule(ctx, "globalThis.modValue = 6 * 7;");
  console.log("evalModule:", quickjs.getGlobal(ctx, "modValue"));

  quickjs.dispose(ctx);
  console.log("quickjs tests passed!");
}

main();