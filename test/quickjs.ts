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