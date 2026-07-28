import { dlopen, dlsym, call, dlclose } from "ffi";

function main(): void {
  console.log("=== FFI Module Test ===");

  // Load platform-specific C library
  console.log("Loading library...");
  const lib = dlopen("msvcrt.dll");
  console.log("Library loaded successfully");

  // Test 1: Call abs(-42) → should return 42
  const absFn = dlsym(lib, "abs");
  const absResult = call(absFn, "int", [-42]);
  console.log("abs(-42) =", absResult);

  // Test 2: Call abs(100) → should return 100
  const absResult2 = call(absFn, "int", [100]);
  console.log("abs(100) =", absResult2);

  // Test 3: Call sqrt(2.0) → should return ~1.414
  const sqrtFn = dlsym(lib, "sqrt");
  const sqrtResult = call(sqrtFn, "double", [2.0]);
  console.log("sqrt(2) =", sqrtResult);

  // Close library
  dlclose(lib);
  console.log("Library closed");
  console.log("=== FFI Test Complete ===");
}

main();
