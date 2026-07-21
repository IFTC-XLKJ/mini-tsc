/*
 * Automatic GC for mini-tsc runtime.
 *
 * - Tracked heap via object headers (ts_gc_alloc / ts_gc_alloc_kind)
 * - Hybrid reclaim: refcount → 0 free immediately; mark-sweep for garbage
 * - Triggers: allocation threshold, idle (event loop), explicit ts_gc_collect
 * - Conservative stack scan from current SP to stack bottom set at main
 */

#include "runtime.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <pthread.h>
#endif

/* ---------- Object header (before user pointer) ---------- */

typedef struct GcHeader {
  struct GcHeader* next;
  uint8_t marked;
  uint8_t kind;      /* GcKind */
  uint16_t flags;
  size_t size;       /* payload bytes */
} GcHeader;

#define GC_HDR_SIZE ((sizeof(GcHeader) + sizeof(void*) - 1) & ~(sizeof(void*) - 1))
#define PTR_TO_HDR(p) ((GcHeader*)((char*)(p) - GC_HDR_SIZE))
#define HDR_TO_PTR(h) ((void*)((char*)(h) + GC_HDR_SIZE))

/* ---------- Heap state ---------- */

static GcHeader* g_heap = NULL;
static size_t g_bytes_live = 0;
static size_t g_bytes_since_collect = 0;
static size_t g_object_count = 0;
static size_t g_threshold = 4 * 1024 * 1024; /* 4 MiB default */
static size_t g_collect_count = 0;
static int g_initialized = 0;
static int g_collecting = 0;
static void* g_stack_bottom = NULL;

/* Explicit roots: array of Value* or void** slots holding GC pointers */
#define GC_MAX_ROOTS 256
static void** g_roots[GC_MAX_ROOTS];
static int g_root_count = 0;

#ifdef _WIN32
static CRITICAL_SECTION g_gc_lock;
static int g_lock_inited = 0;
#  define GC_LOCK()   do { if (g_lock_inited) EnterCriticalSection(&g_gc_lock); } while (0)
#  define GC_UNLOCK() do { if (g_lock_inited) LeaveCriticalSection(&g_gc_lock); } while (0)
#else
static pthread_mutex_t g_gc_lock = PTHREAD_MUTEX_INITIALIZER;
#  define GC_LOCK()   pthread_mutex_lock(&g_gc_lock)
#  define GC_UNLOCK() pthread_mutex_unlock(&g_gc_lock)
#endif

/* ---------- Internals ---------- */

static void gc_finalize(GcHeader* h);

static int ptr_in_object(GcHeader* h, void* p) {
  char* base = (char*)HDR_TO_PTR(h);
  char* end = base + h->size;
  return (char*)p >= base && (char*)p < end;
}

static GcHeader* find_object(void* p) {
  if (!p) return NULL;
  for (GcHeader* h = g_heap; h; h = h->next) {
    if (HDR_TO_PTR(h) == p) return h;
    /* Also accept interior pointers into payload */
    if (ptr_in_object(h, p)) return h;
  }
  return NULL;
}

static void mark_object(GcHeader* h);

/* Conservatively scan a memory range for heap pointers */
static void scan_range(void* start, void* end) {
  if (!start || !end) return;
  uintptr_t a = (uintptr_t)start;
  uintptr_t b = (uintptr_t)end;
  if (a > b) {
    uintptr_t t = a; a = b; b = t;
  }
  /* Align to pointer boundary */
  a = (a + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
  for (uintptr_t p = a; p + sizeof(void*) <= b; p += sizeof(void*)) {
    void* candidate = *(void**)p;
    if (!candidate) continue;
    GcHeader* h = find_object(candidate);
    if (h && !h->marked) {
      mark_object(h);
    }
  }
}

/* Scan writable data sections of the main executable (global roots). */
static void scan_static_roots(void) {
#ifdef _WIN32
  HMODULE mod = GetModuleHandleW(NULL);
  if (!mod) return;
  IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)mod;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
  IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((char*)mod + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return;
  IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++) {
    DWORD ch = sec[i].Characteristics;
    if ((ch & IMAGE_SCN_MEM_WRITE) && (ch & IMAGE_SCN_MEM_READ) &&
        !(ch & IMAGE_SCN_MEM_EXECUTE)) {
      char* start = (char*)mod + sec[i].VirtualAddress;
      size_t sz = sec[i].Misc.VirtualSize;
      if (sz > 0) scan_range(start, start + sz);
    }
  }
#else
  /* ELF: linker-provided data segment bounds (glibc / musl common symbols) */
  extern char __data_start[] __attribute__((weak));
  extern char _end[] __attribute__((weak));
  if (&__data_start && &_end && (void*)__data_start < (void*)_end) {
    scan_range((void*)__data_start, (void*)_end);
  }
#endif
}

/* Mark one object and scan its payload for more refs */
static void mark_object(GcHeader* h) {
  if (!h || h->marked) return;
  h->marked = 1;

  void* payload = HDR_TO_PTR(h);
  size_t size = h->size;

  /* Kind-aware mark for known structures (more precise than pure conservative) */
  switch ((GcKind)h->kind) {
    case GC_KIND_STRING: {
      /* TSString: data is non-GC malloc — nothing to mark inside */
      (void)payload;
      break;
    }
    case GC_KIND_ARRAY: {
      TSArray* arr = (TSArray*)payload;
      if (arr->items && arr->length > 0) {
        scan_range(arr->items, (char*)arr->items + (size_t)arr->length * sizeof(Value));
      }
      break;
    }
    case GC_KIND_HASHMAP: {
      TSHashMap* map = (TSHashMap*)payload;
      if (map->entries && map->capacity > 0) {
        for (int32_t i = 0; i < map->capacity; i++) {
          if (!map->entries[i].occupied) continue;
          if (map->entries[i].key) {
            GcHeader* kh = find_object(map->entries[i].key);
            if (kh) mark_object(kh);
          }
          scan_range(&map->entries[i].value, (char*)&map->entries[i].value + sizeof(Value));
        }
      }
      break;
    }
    case GC_KIND_CLOSURE: {
      Closure* c = (Closure*)payload;
      if (c->captured_vars && c->captured_count > 0) {
        scan_range(c->captured_vars,
                   (char*)c->captured_vars + (size_t)c->captured_count * sizeof(Value));
      }
      break;
    }
    case GC_KIND_PROMISE: {
      TSPromise* p = (TSPromise*)payload;
      scan_range(&p->result, (char*)&p->result + sizeof(Value));
      scan_range(&p->onFulfilled, (char*)&p->onFulfilled + sizeof(Value));
      scan_range(&p->onRejected, (char*)&p->onRejected + sizeof(Value));
      scan_range(&p->onFinally, (char*)&p->onFinally + sizeof(Value));
      if (p->then_promise) {
        GcHeader* th = find_object(p->then_promise);
        if (th) mark_object(th);
      }
      break;
    }
    case GC_KIND_RAW:
    default:
      /* Conservative: scan whole payload for pointers */
      if (size >= sizeof(void*)) {
        scan_range(payload, (char*)payload + size);
      }
      break;
  }
}

static void gc_finalize(GcHeader* h) {
  void* payload = HDR_TO_PTR(h);
  switch ((GcKind)h->kind) {
    case GC_KIND_STRING: {
      TSString* s = (TSString*)payload;
      if (s->data) {
        free(s->data);
        s->data = NULL;
      }
      break;
    }
    case GC_KIND_ARRAY: {
      TSArray* arr = (TSArray*)payload;
      if (arr->items) {
        free(arr->items);
        arr->items = NULL;
      }
      break;
    }
    case GC_KIND_HASHMAP: {
      TSHashMap* map = (TSHashMap*)payload;
      if (map->entries) {
        free(map->entries);
        map->entries = NULL;
      }
      break;
    }
    case GC_KIND_CLOSURE: {
      Closure* c = (Closure*)payload;
      if (c->captured_vars) {
        free(c->captured_vars);
        c->captured_vars = NULL;
      }
      break;
    }
    case GC_KIND_PROMISE:
      /* Values held inside are either GC objects (swept separately) or scalars */
      break;
    default:
      break;
  }
}

/* Free a single heap object (must be on list); caller holds lock */
static void free_header(GcHeader* h, GcHeader* prev) {
  if (prev) prev->next = h->next;
  else g_heap = h->next;

  if (g_bytes_live >= h->size + GC_HDR_SIZE)
    g_bytes_live -= h->size + GC_HDR_SIZE;
  else
    g_bytes_live = 0;
  if (g_object_count > 0) g_object_count--;

  gc_finalize(h);
  free(h);
}

/* ---------- Public API ---------- */

void ts_gc_init(void) {
  if (g_initialized) return;
  g_initialized = 1;
  g_heap = NULL;
  g_bytes_live = 0;
  g_bytes_since_collect = 0;
  g_object_count = 0;
  g_threshold = 4 * 1024 * 1024;
  g_collect_count = 0;
  g_root_count = 0;
  g_stack_bottom = NULL;
#ifdef _WIN32
  if (!g_lock_inited) {
    InitializeCriticalSection(&g_gc_lock);
    g_lock_inited = 1;
  }
#endif
}

void ts_gc_set_stack_bottom(void* bottom) {
  g_stack_bottom = bottom;
}

void ts_gc_set_threshold(size_t bytes) {
  g_threshold = bytes < (256 * 1024) ? (256 * 1024) : bytes;
}

size_t ts_gc_allocated_bytes(void) {
  return g_bytes_live;
}

size_t ts_gc_object_count(void) {
  return g_object_count;
}

size_t ts_gc_collect_count(void) {
  return g_collect_count;
}

void ts_gc_add_root(void** slot) {
  if (!slot) return;
  GC_LOCK();
  if (g_root_count < GC_MAX_ROOTS) {
    g_roots[g_root_count++] = slot;
  }
  GC_UNLOCK();
}

void ts_gc_remove_root(void** slot) {
  if (!slot) return;
  GC_LOCK();
  for (int i = 0; i < g_root_count; i++) {
    if (g_roots[i] == slot) {
      g_roots[i] = g_roots[--g_root_count];
      break;
    }
  }
  GC_UNLOCK();
}

void* ts_gc_alloc(size_t size) {
  return ts_gc_alloc_kind(size, GC_KIND_RAW);
}

void* ts_gc_alloc_kind(size_t size, GcKind kind) {
  if (!g_initialized) ts_gc_init();

  /* Auto-collect on allocation threshold (before allocating more) */
  if (g_bytes_since_collect >= g_threshold && !g_collecting) {
    ts_gc_collect();
  }

  size_t total = GC_HDR_SIZE + size;
  GcHeader* h = (GcHeader*)malloc(total);
  if (!h) {
    /* Try collect once then retry */
    if (!g_collecting) {
      ts_gc_collect();
      h = (GcHeader*)malloc(total);
    }
    if (!h) return NULL;
  }
  memset(h, 0, total);
  h->next = NULL;
  h->marked = 0;
  h->kind = (uint8_t)kind;
  h->flags = 0;
  h->size = size;

  GC_LOCK();
  h->next = g_heap;
  g_heap = h;
  g_bytes_live += total;
  g_bytes_since_collect += total;
  g_object_count++;
  GC_UNLOCK();

  return HDR_TO_PTR(h);
}

/* Immediate free when refcount hits 0 (or explicit free). Safe if not on heap. */
void ts_gc_free_object(void* ptr) {
  if (!ptr) return;
  GC_LOCK();
  GcHeader* prev = NULL;
  for (GcHeader* h = g_heap; h; prev = h, h = h->next) {
    if (HDR_TO_PTR(h) == ptr) {
      free_header(h, prev);
      GC_UNLOCK();
      return;
    }
  }
  GC_UNLOCK();
  /* Not a GC object — plain free for compatibility */
  free(ptr);
}

int ts_gc_is_managed(void* ptr) {
  if (!ptr) return 0;
  GC_LOCK();
  int found = find_object(ptr) != NULL;
  GC_UNLOCK();
  return found;
}

void ts_gc_collect(void) {
  if (!g_initialized) ts_gc_init();
  if (g_collecting) return;
  g_collecting = 1;

  GC_LOCK();

  /* Clear marks */
  for (GcHeader* h = g_heap; h; h = h->next) {
    h->marked = 0;
  }

  /* 1) Explicit roots */
  for (int i = 0; i < g_root_count; i++) {
    if (!g_roots[i] || !*g_roots[i]) continue;
    GcHeader* h = find_object(*g_roots[i]);
    if (h) mark_object(h);
  }

  /* 2) Global / static data (module-level TS variables live here) */
  scan_static_roots();

  /* 3) Conservative stack scan */
  volatile int stack_top_anchor = 0;
  void* stack_top = (void*)&stack_top_anchor;
  if (g_stack_bottom) {
    scan_range(stack_top, g_stack_bottom);
  }

  /* 3) Treat refcount > 0 on known kinds as roots (still reachable from C locals
   *    that our stack scan might miss if optimized into registers — belt & suspenders).
   *    Objects start with refcount=1; free decrements. So this keeps anything not
   *    yet explicitly released. Combined with stack scan this is safe; pure
   *    refcount-only would never free unreleased objects, which is intentional
   *    until the program drops the last ref OR they become unreachable with rc==0.
   *
   *    For automatic optimization of *leaked* temps that never call free, we rely
   *    on stack scan: if no stack/heap/root points at them and somehow rc==0, sweep.
   *    If rc stays 1 forever (typical leak), we still need stack absence + force.
   *
   *    Policy: mark all objects that still have refcount >= 1 for STRING/ARRAY/
   *    HASHMAP/PROMISE. That means collect only reclaims objects already released
   *    (rc==0) but still on the heap (shouldn't happen) OR we allow "orphan with
   *    rc>0" only when not stack-reachable — that would require NOT marking by
   *    refcount.
   *
   *    Chosen policy for auto optimization of forgotten frees:
   *    - Do NOT treat refcount as root.
   *    - Stack + heap + explicit roots only.
   *    - Immediate free on ts_*_free when rc hits 0 still works.
   *    - Unreachable objects (even with rc>0) get swept — this is the auto fix
   *      for programs that never free.
   *
   *    Risk: if a live pointer lives only in a register, we may free too early.
   *    Mitigation: stack bottom set in main; most Values sit on stack frames.
   */

  /* Sweep unmarked */
  GcHeader* prev = NULL;
  GcHeader* h = g_heap;
  size_t freed = 0;
  while (h) {
    GcHeader* next = h->next;
    if (!h->marked) {
      free_header(h, prev);
      freed++;
      /* prev stays the same */
    } else {
      h->marked = 0;
      prev = h;
    }
    h = next;
  }

  g_bytes_since_collect = 0;
  g_collect_count++;

  /* Adaptive threshold: at least 1 MiB, else 2× live */
  size_t next_th = g_bytes_live * 2;
  if (next_th < 1024 * 1024) next_th = 1024 * 1024;
  g_threshold = next_th;

  (void)freed;
  GC_UNLOCK();
  g_collecting = 0;
}

void ts_gc_maybe_collect(void) {
  if (g_bytes_since_collect >= g_threshold) {
    ts_gc_collect();
  }
}

void ts_gc_maybe_collect_idle(void) {
  /* Idle path: only if we've allocated a meaningful fraction of the threshold */
  if (g_bytes_since_collect >= (g_threshold / 4) && g_bytes_since_collect > 64 * 1024) {
    ts_gc_collect();
  }
}
