import { dlopen, dlsym, call, dlclose } from "ffi";

function main(): void {
  console.log("=== FFI Shared Library Test ===");

  // Load the compiled shared library
  console.log("Loading math.dll...");
  const lib = dlopen("math.dll");
  console.log("Library loaded");

  // Look up the exported function
  const addFn = dlsym(lib, "add");
  console.log("Found exported function");

  // Call the function
  console.log("Calling add function...");
  const result = call(addFn, "double", [1, 2]);
  console.log("Add function result:", result); // Should be 3

  // Close the library
  dlclose(lib);
  console.log("Library closed");
  console.log("=== FFI Shared Library Test Complete ===");
}

main();
