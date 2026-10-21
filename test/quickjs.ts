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

  quickjs.dispose(ctx);
  console.log("quickjs tests passed!");
}

main();