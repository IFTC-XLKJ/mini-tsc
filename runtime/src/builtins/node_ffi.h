#ifndef NODE_FFI_H
#define NODE_FFI_H
#include "runtime.h"

Value node_ffi_dlopen(Value path);
Value node_ffi_dlsym(Value handle, Value symbol);
Value node_ffi_call(Value funcPtr, Value returnType, Value args);
Value node_ffi_dlclose(Value handle);

#endif
