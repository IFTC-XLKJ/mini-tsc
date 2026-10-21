# Vendored QuickJS — upstream deviations

This directory holds the upstream QuickJS engine (upstream version recorded in
`VERSION`, currently `2026-06-04`), vendored to back the mini-tsc `quickjs`
builtin module. The tree is **deliberately not byte-identical to upstream**: a
small, reviewed set of local deviations makes it build and run on this
project's Windows / MSVC-target-clang toolchain.

Re-syncing the tree from a fresh upstream tarball must **re-apply every
deviation below**. Each one is also commented in place at its source site, so a
`grep` for the marker comment will surface it.

Why deviations instead of global build flags: the project prefers a local,
reviewable source change over changes to the driver's global compile/link flags
(e.g. `-fmerge-all-constants`, `/OPT:ICF`), which have wide, hard-to-audit
blast radius. Two changes were once suspected of causing the identical crash
that deviation (a) fixes — both were disproven by a revert-test (see
`docs/quickjs.md` and project memory).

---

## (a) `JSClosureVar.closure_type` is `unsigned int`, not the enum type

**File:** `quickjs.c`, struct `JSClosureVar` (around line 631).

**Upstream:**

```c
JSClosureTypeEnum closure_type : 3;
```

**Here:**

```c
unsigned int closure_type : 3;
```

**Why:** `JSClosureTypeEnum` has members at and above `JS_CLOSURE_GLOBAL_DECL`
(= 4), but on the MSVC ABI (which `clang.exe` targets on this host) an
**enum-typed bit-field is signed**, so a 3-bit field only spans `0..3`. Values
`>= 4` round-trip negative, and the closure-var `switch (cv->closure_type)`
(around lines 17608 / 33435) falls through to `abort()` — surfacing at runtime
as a `0xC0000409` (`STATUS_STACK_BUFFER_OVERRUN`) fastfail, with **buffered
stdout discarded**. This made any `eval` that resolved or declared an
identifier crash. Forcing an unsigned field lets all eight enum values fit.

**Load-bearing:** proven by revert-test — reverting to the upstream form
reproduces the crash (`RUN_EXIT=-1073740791`, empty stdout); restoring the
unsigned form makes `test/quickjs.ts` print all expected output and exit 0.
Committed in `b6a0d21`.

> This is a toolchain-ABI divergence, **not** an upstream bug: upstream builds
> with GCC/Clang where the enum is unsigned by default.

---

## (b) `compat/` POSIX-over-Win32 shim headers

**Files (added, not in upstream):** `compat/sys/time.h`, `compat/pthread.h`.

**Why:** `quickjs.c` unconditionally includes `<sys/time.h>` (top of file) and —
because `CONFIG_ATOMICS` is `#define`d unconditionally — `<pthread.h>` /
`<stdatomic.h>` (the SharedArrayBuffer / `Atomics.wait` support; the
`node_quickjs.c` wrapper never uses it, but the symbols must still link). clang
/MSVC has `<stdatomic.h>` but **not** `<pthread.h>` or `<sys/time.h>`, and this
host has no MinGW/MSYS/WSL sysroot. The `compat/` dir provides minimal
`#ifdef _WIN32` shims (each falls through to `#include_next` on non-Windows):

- `compat/sys/time.h` — `struct timeval` (guarded so it does not clash with the
  one `<windows.h>`/winsock already defines), `CLOCK_REALTIME`, `gettimeofday`
  (via `GetSystemTimeAsFileTime`), `clock_gettime`.
- `compat/pthread.h` — `pthread_mutex_t` / `pthread_cond_t` over Win32
  `CRITICAL_SECTION` / `CONDITION_VARIABLE`, lazily initialised via
  `InterlockedCompareExchange` (because `PTHREAD_MUTEX_INITIALIZER` cannot
  statically initialise a `CRITICAL_SECTION`), plus
  `pthread_cond_timedwait` over `SleepConditionVariableCS` and `ETIMEDOUT`.

**Safety:** all other `<pthread.h>` / `<sys/time.h>` includes in the mini-tsc
runtime live in non-Windows `#else` branches, so only QuickJS resolves these
compat headers. The compat dir is added to the include path **Windows-only**.

---

## How the deviations are wired into the build

All of this lives in `src/driver/compiler.ts` (search `quickjs`):

- **Linking (core only):** when the `quickjs` module is used, exactly
  `["quickjs.c", "cutils.c", "dtoa.c", "libregexp.c", "libunicode.c"]` from this
  dir are pushed into `runtimeSrcFiles`. `quickjs-libc.c` is **deliberately
  excluded** — the os/std modules pull in heavy POSIX deps and the
  `node_quickjs.c` wrapper does not use them.
- **Include path:** `-I<root>/runtime/src/quickjs`, plus the `compat/` dir
  Windows-only.
- **Version define:** `-DCONFIG_VERSION=\"<contents of VERSION>\"` read from the
  `VERSION` file at compile time (backslash-escaped because the driver builds a
  shell string, not an args array). Never hard-coded — swapping the vendored
  tree only needs the tree plus its `VERSION` file replaced.
- **Link flags:** Unix adds `-lm`; on Windows the quoted
  `clang_rt.builtins-<arch>.lib` (resolved via `clang -print-resource-dir`) is
  added to satisfy QuickJS BigInt's `__udivti3` / `__divti3`.

## Re-vendoring checklist

1. Replace this directory with the desired upstream QuickJS release.
2. Restore the `VERSION` file to that release's value (the driver reads it).
3. Re-apply deviation (a): change `JSClosureTypeEnum closure_type : 3;` in
   `JSClosureVar` to `unsigned int closure_type : 3;` (keep the explanatory
   comment).
4. Re-add deviation (b): the `compat/` directory (`sys/time.h`, `pthread.h`)
   with its `#ifdef _WIN32` shims.
5. Confirm generated headers ship in the tarball (`libunicode-table.h`,
   `quickjs-atom.h`, `quickjs-opcode.h`, `libregexp-opcode.h`) so no
   `unicode_gen` prebuild is needed.
6. Rebuild and run `test/quickjs.ts` end-to-end via the real driver
   (`node dist/cli/index.js compile test/quickjs.ts -o out_qjs -d out_qjs`) —
   never a hand-written clang line.