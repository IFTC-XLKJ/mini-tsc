#include "node_ffi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

/* Type tags for discriminating Value objects */
#define FFI_LIB_TAG  0x4646494C  /* "FFIL" */
#define FFI_FUNC_TAG 0x4646464E  /* "FFNF" */

typedef struct {
  int32_t type_tag;
#ifdef _WIN32
  HMODULE handle;
#else
  void* handle;
#endif
} FfiLib;

typedef struct {
  int32_t type_tag;
  void* fnPtr;
  FfiLib* owner;
} FfiFunc;

static FfiLib* as_lib(Value v) {
  if (v.tag != TAG_OBJECT || !v.as.object) return NULL;
  FfiLib* lib = (FfiLib*)v.as.object;
  if (lib->type_tag != FFI_LIB_TAG) return NULL;
  return lib;
}

static FfiFunc* as_func(Value v) {
  if (v.tag != TAG_OBJECT || !v.as.object) return NULL;
  FfiFunc* fn = (FfiFunc*)v.as.object;
  if (fn->type_tag != FFI_FUNC_TAG) return NULL;
  return fn;
}

static Value ffi_throw(const char* msg) {
  TS_THROW(ts_value_string(ts_string_new(msg ? msg : "ffi error")));
  return ts_value_undefined();
}

/* dlopen(path: string) → LibraryHandle */
Value node_ffi_dlopen(Value path) {
  if (path.tag != TAG_STRING || !path.as.string) {
    return ffi_throw("dlopen: path must be a string");
  }
  const char* cpath = path.as.string->data;

#ifdef _WIN32
  HMODULE h = LoadLibraryA(cpath);
  if (!h) {
    char buf[256];
    snprintf(buf, sizeof(buf), "dlopen: failed to load '%s' (error %lu)", cpath, GetLastError());
    return ffi_throw(buf);
  }
#else
  void* h = dlopen(cpath, RTLD_NOW | RTLD_LOCAL);
  if (!h) {
    const char* err = dlerror();
    char buf[512];
    snprintf(buf, sizeof(buf), "dlopen: failed to load '%s': %s", cpath, err ? err : "unknown error");
    return ffi_throw(buf);
  }
#endif

  FfiLib* lib = (FfiLib*)malloc(sizeof(FfiLib));
  lib->type_tag = FFI_LIB_TAG;
  lib->handle = h;
  return ts_value_object(lib);
}

/* dlsym(handle: LibraryHandle, symbol: string) → FunctionPointer */
Value node_ffi_dlsym(Value handle, Value symbol) {
  FfiLib* lib = as_lib(handle);
  if (!lib) return ffi_throw("dlsym: invalid library handle");

  if (symbol.tag != TAG_STRING || !symbol.as.string) {
    return ffi_throw("dlsym: symbol must be a string");
  }
  const char* csym = symbol.as.string->data;

#ifdef _WIN32
  void* fn = (void*)GetProcAddress(lib->handle, csym);
#else
  void* fn = dlsym(lib->handle, csym);
#endif

  if (!fn) {
    char buf[256];
    snprintf(buf, sizeof(buf), "dlsym: symbol '%s' not found", csym);
    return ffi_throw(buf);
  }

  FfiFunc* func = (FfiFunc*)malloc(sizeof(FfiFunc));
  func->type_tag = FFI_FUNC_TAG;
  func->fnPtr = fn;
  func->owner = lib;
  return ts_value_object(func);
}

/* call(funcPtr: FunctionPointer, returnType: string, args: any[]) → any */
Value node_ffi_call(Value funcPtr, Value returnType, Value args) {
  FfiFunc* fn = as_func(funcPtr);
  if (!fn) return ffi_throw("call: invalid function pointer");

  if (returnType.tag != TAG_STRING || !returnType.as.string) {
    return ffi_throw("call: returnType must be a string");
  }
  const char* retType = returnType.as.string->data;

  /* Collect up to 8 args from the array */
  Value argVals[8];
  int argc = 0;
  if (args.tag == TAG_ARRAY && args.as.array) {
    argc = args.as.array->length;
    if (argc > 8) argc = 8;
    for (int i = 0; i < argc; i++) {
      argVals[i] = ts_array_get(args.as.array, i);
    }
  }

  /* Marshal arguments to C types based on their Value tags */
  int iArgs[8];
  double dArgs[8];
  char* sArgs[8];
  void* pArgs[8];

  for (int i = 0; i < argc; i++) {
    Value v = argVals[i];
    switch (v.tag) {
      case TAG_NUMBER:
        iArgs[i] = (int)v.as.number;
        dArgs[i] = v.as.number;
        break;
      case TAG_BOOLEAN:
        iArgs[i] = v.as.boolean;
        dArgs[i] = v.as.boolean ? 1.0 : 0.0;
        break;
      case TAG_STRING:
        sArgs[i] = v.as.string ? v.as.string->data : "";
        pArgs[i] = v.as.string ? v.as.string->data : NULL;
        break;
      case TAG_NULL:
      case TAG_OBJECT:
        iArgs[i] = 0;
        dArgs[i] = 0.0;
        pArgs[i] = (v.tag == TAG_OBJECT && v.as.object) ? v.as.object : NULL;
        break;
      default:
        iArgs[i] = 0;
        dArgs[i] = 0.0;
        pArgs[i] = NULL;
        break;
    }
  }

  /* Call based on return type */
  if (strcmp(retType, "int") == 0) {
    typedef int (*fn_i0)(void);
    typedef int (*fn_i1)(int);
    typedef int (*fn_i2)(int, int);
    typedef int (*fn_i3)(int, int, int);
    typedef int (*fn_i4)(int, int, int, int);
    typedef int (*fn_i5)(int, int, int, int, int);
    typedef int (*fn_i6)(int, int, int, int, int, int);
    typedef int (*fn_i7)(int, int, int, int, int, int, int);
    typedef int (*fn_i8)(int, int, int, int, int, int, int, int);
    int result;
    switch (argc) {
      case 0: result = ((fn_i0)fn->fnPtr)(); break;
      case 1: result = ((fn_i1)fn->fnPtr)(iArgs[0]); break;
      case 2: result = ((fn_i2)fn->fnPtr)(iArgs[0], iArgs[1]); break;
      case 3: result = ((fn_i3)fn->fnPtr)(iArgs[0], iArgs[1], iArgs[2]); break;
      case 4: result = ((fn_i4)fn->fnPtr)(iArgs[0], iArgs[1], iArgs[2], iArgs[3]); break;
      case 5: result = ((fn_i5)fn->fnPtr)(iArgs[0], iArgs[1], iArgs[2], iArgs[3], iArgs[4]); break;
      case 6: result = ((fn_i6)fn->fnPtr)(iArgs[0], iArgs[1], iArgs[2], iArgs[3], iArgs[4], iArgs[5]); break;
      case 7: result = ((fn_i7)fn->fnPtr)(iArgs[0], iArgs[1], iArgs[2], iArgs[3], iArgs[4], iArgs[5], iArgs[6]); break;
      default: result = ((fn_i8)fn->fnPtr)(iArgs[0], iArgs[1], iArgs[2], iArgs[3], iArgs[4], iArgs[5], iArgs[6], iArgs[7]); break;
    }
    return ts_value_number((double)result);
  }

  if (strcmp(retType, "double") == 0) {
    typedef double (*fn_d0)(void);
    typedef double (*fn_d1)(double);
    typedef double (*fn_d2)(double, double);
    typedef double (*fn_d3)(double, double, double);
    typedef double (*fn_d4)(double, double, double, double);
    typedef double (*fn_d5)(double, double, double, double, double);
    typedef double (*fn_d6)(double, double, double, double, double, double);
    typedef double (*fn_d7)(double, double, double, double, double, double, double);
    typedef double (*fn_d8)(double, double, double, double, double, double, double, double);
    double result;
    switch (argc) {
      case 0: result = ((fn_d0)fn->fnPtr)(); break;
      case 1: result = ((fn_d1)fn->fnPtr)(dArgs[0]); break;
      case 2: result = ((fn_d2)fn->fnPtr)(dArgs[0], dArgs[1]); break;
      case 3: result = ((fn_d3)fn->fnPtr)(dArgs[0], dArgs[1], dArgs[2]); break;
      case 4: result = ((fn_d4)fn->fnPtr)(dArgs[0], dArgs[1], dArgs[2], dArgs[3]); break;
      case 5: result = ((fn_d5)fn->fnPtr)(dArgs[0], dArgs[1], dArgs[2], dArgs[3], dArgs[4]); break;
      case 6: result = ((fn_d6)fn->fnPtr)(dArgs[0], dArgs[1], dArgs[2], dArgs[3], dArgs[4], dArgs[5]); break;
      case 7: result = ((fn_d7)fn->fnPtr)(dArgs[0], dArgs[1], dArgs[2], dArgs[3], dArgs[4], dArgs[5], dArgs[6]); break;
      default: result = ((fn_d8)fn->fnPtr)(dArgs[0], dArgs[1], dArgs[2], dArgs[3], dArgs[4], dArgs[5], dArgs[6], dArgs[7]); break;
    }
    return ts_value_number(result);
  }

  if (strcmp(retType, "string") == 0) {
    typedef char* (*fn_s0)(void);
    typedef char* (*fn_s1)(char*);
    typedef char* (*fn_s2)(char*, char*);
    typedef char* (*fn_s3)(char*, char*, char*);
    typedef char* (*fn_s4)(char*, char*, char*, char*);
    char* result;
    switch (argc) {
      case 0: result = ((fn_s0)fn->fnPtr)(); break;
      case 1: result = ((fn_s1)fn->fnPtr)(sArgs[0]); break;
      case 2: result = ((fn_s2)fn->fnPtr)(sArgs[0], sArgs[1]); break;
      case 3: result = ((fn_s3)fn->fnPtr)(sArgs[0], sArgs[1], sArgs[2]); break;
      default: result = ((fn_s4)fn->fnPtr)(sArgs[0], sArgs[1], sArgs[2], sArgs[3]); break;
    }
    return result ? ts_value_string(ts_string_new(result)) : ts_value_null();
  }

  if (strcmp(retType, "pointer") == 0 || strcmp(retType, "void*") == 0) {
    typedef void* (*fn_p0)(void);
    typedef void* (*fn_p1)(void*);
    typedef void* (*fn_p2)(void*, void*);
    typedef void* (*fn_p3)(void*, void*, void*);
    void* result;
    switch (argc) {
      case 0: result = ((fn_p0)fn->fnPtr)(); break;
      case 1: result = ((fn_p1)fn->fnPtr)(pArgs[0]); break;
      case 2: result = ((fn_p2)fn->fnPtr)(pArgs[0], pArgs[1]); break;
      default: result = ((fn_p3)fn->fnPtr)(pArgs[0], pArgs[1], pArgs[2]); break;
    }
    return result ? ts_value_object(result) : ts_value_null();
  }

  if (strcmp(retType, "void") == 0) {
    typedef void (*fn_v0)(void);
    typedef void (*fn_v1)(int);
    typedef void (*fn_v2)(int, int);
    typedef void (*fn_v3)(int, int, int);
    typedef void (*fn_v4)(int, int, int, int);
    switch (argc) {
      case 0: ((fn_v0)fn->fnPtr)(); break;
      case 1: ((fn_v1)fn->fnPtr)(iArgs[0]); break;
      case 2: ((fn_v2)fn->fnPtr)(iArgs[0], iArgs[1]); break;
      case 3: ((fn_v3)fn->fnPtr)(iArgs[0], iArgs[1], iArgs[2]); break;
      default: ((fn_v4)fn->fnPtr)(iArgs[0], iArgs[1], iArgs[2], iArgs[3]); break;
    }
    return ts_value_undefined();
  }

  char buf[256];
  snprintf(buf, sizeof(buf), "call: unsupported return type '%s'", retType);
  return ffi_throw(buf);
}

/* dlclose(handle: LibraryHandle) → void */
Value node_ffi_dlclose(Value handle) {
  FfiLib* lib = as_lib(handle);
  if (!lib) return ffi_throw("dlclose: invalid library handle");

#ifdef _WIN32
  FreeLibrary(lib->handle);
#else
  dlclose(lib->handle);
#endif

  free(lib);
  return ts_value_undefined();
}
