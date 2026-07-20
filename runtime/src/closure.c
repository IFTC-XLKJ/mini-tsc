#include "runtime.h"

Closure* ts_closure_new(void* fn, Value* captures, int32_t count) {
  Closure* c = (Closure*)malloc(sizeof(Closure));
  c->function_ptr = fn;
  c->captured_count = count;
  if (count > 0 && captures) {
    c->captured_vars = (Value*)malloc(count * sizeof(Value));
    memcpy(c->captured_vars, captures, count * sizeof(Value));
  } else {
    c->captured_vars = NULL;
  }
  return c;
}

Value ts_closure_call(Closure* closure, Value* args, int32_t arg_count) {
  /* In generated code, the function_ptr is a typed function pointer.
   * This generic call would need to know the actual signature.
   * Generated code will cast and call directly. */
  return ts_value_undefined();
}

void ts_closure_free(Closure* closure) {
  if (closure->captured_vars) {
    free(closure->captured_vars);
  }
  free(closure);
}
