declare module "ffi" {
  type LibraryHandle = any;
  type FunctionPointer = any;

  function dlopen(path: string): LibraryHandle;
  function dlsym(handle: LibraryHandle, symbol: string): FunctionPointer;
  function call(funcPtr: FunctionPointer, returnType: string, args: any[]): any;
  function dlclose(handle: LibraryHandle): void;

  export { dlopen, dlsym, call, dlclose };
}
