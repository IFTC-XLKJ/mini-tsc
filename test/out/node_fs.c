#define _CRT_SECURE_NO_WARNINGS
#include "node_fs.h"
#include "ts_features.h"
#include <stdio.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#define mkdir_p(path, mode) _mkdir(path)
#define stat_fn _stat
#define stat_struct struct _stat
#else
#include <unistd.h>
#include <dirent.h>
#define mkdir_p(path, mode) mkdir(path, mode)
#define stat_fn stat
#define stat_struct struct stat
#endif

static int fs_options_has_encoding(Value options) {
  if (options.tag == TAG_STRING && options.as.string) return 1;
  if (options.tag == TAG_OBJECT && options.as.object) {
    /* HashMap options: { encoding: "utf8" } */
    Value enc = ts_hashmap_get((TSHashMap*)options.as.object, ts_string_new("encoding"));
    if (enc.tag == TAG_STRING && enc.as.string && enc.as.string->length > 0) return 1;
  }
  return 0;
}

#if defined(TS_NEED_node_fs_readFileSync)
Value node_fs_readFileSync(Value path, Value options) {
  TSString* pathStr = ts_to_string(path);
  FILE* f = fopen(pathStr->data, "rb");
  if (!f) {
    TSString* err = ts_string_new("File not found: ");
    TSString* msg = ts_string_concat(err, pathStr);
    TS_THROW(ts_value_string(msg));
    return ts_value_undefined();
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  if (size < 0) size = 0;
  fseek(f, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)size + 1);
  size_t nread = fread(data, 1, (size_t)size, f);
  data[nread] = '\0';
  fclose(f);

  /* No encoding → Buffer (Node.js default); encoding present → string */
  if (!fs_options_has_encoding(options)) {
    Buffer* buf = (Buffer*)malloc(sizeof(Buffer));
    buf->type_tag = BUFFER_TAG;
    buf->length = (int32_t)nread;
    buf->capacity = (int32_t)nread > 0 ? (int32_t)nread : 16;
    buf->data = data;
    return ts_value_object(buf);
  }

  TSString* result = ts_string_new_len((const char*)data, (int32_t)nread);
  free(data);
  return ts_value_string(result);
}
#endif /* TS_NEED_node_fs_readFileSync */

#if defined(TS_NEED_node_fs_writeFileSync)
int node_fs_writeFileSync(Value path, Value data, Value options) {
  (void)options;
  TSString* pathStr = ts_to_string(path);
  FILE* f = fopen(pathStr->data, "wb");
  if (!f) return -1;

  if (data.tag == TAG_OBJECT && data.as.object &&
      *((int32_t*)data.as.object) == BUFFER_TAG) {
    Buffer* b = (Buffer*)data.as.object;
    if (b->data && b->length > 0) {
      fwrite(b->data, 1, (size_t)b->length, f);
    }
  } else {
    TSString* dataStr = ts_to_string(data);
    if (dataStr && dataStr->data && dataStr->length > 0) {
      fwrite(dataStr->data, 1, (size_t)dataStr->length, f);
    }
  }
  fclose(f);
  return 0;
}
#endif /* TS_NEED_node_fs_writeFileSync */

#if defined(TS_NEED_node_fs_existsSync)
int node_fs_existsSync(Value path) {
  TSString* pathStr = ts_to_string(path);
  stat_struct st;
  return stat_fn(pathStr->data, &st) == 0;
}
#endif /* TS_NEED_node_fs_existsSync */

#if defined(TS_NEED_node_fs_mkdirSync)
int node_fs_mkdirSync(Value path, Value options) {
  TSString* pathStr = ts_to_string(path);
  return mkdir_p(pathStr->data, 0755);
}
#endif /* TS_NEED_node_fs_mkdirSync */

#if defined(TS_NEED_node_fs_readdirSync)
Value node_fs_readdirSync(Value path) {
  TSString* pathStr = ts_to_string(path);
  TSArray* arr = ts_array_new();

#ifdef _WIN32
  /* Windows: simplified */
  WIN32_FIND_DATAA findData;
  char searchPath[1024];
  snprintf(searchPath, sizeof(searchPath), "%s\\*", pathStr->data);
  HANDLE hFind = FindFirstFileA(searchPath, &findData);
  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
        ts_array_push(arr, ts_value_string(ts_string_new(findData.cFileName)));
      }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
  }
#else
  DIR* d = opendir(pathStr->data);
  if (d) {
    struct dirent* entry;
    while ((entry = readdir(d)) != NULL) {
      if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
        ts_array_push(arr, ts_value_string(ts_string_new(entry->d_name)));
      }
    }
    closedir(d);
  }
#endif

  return ts_value_array(arr);
}
#endif /* TS_NEED_node_fs_readdirSync */

#if defined(TS_NEED_node_fs_unlinkSync)
int node_fs_unlinkSync(Value path) {
  TSString* pathStr = ts_to_string(path);
#ifdef _WIN32
  return _unlink(pathStr->data);
#else
  return unlink(pathStr->data);
#endif
}
#endif /* TS_NEED_node_fs_unlinkSync */

#if defined(TS_NEED_node_fs_statSync)
Value node_fs_statSync(Value path) {
  TSString* pathStr = ts_to_string(path);
  stat_struct st;
  if (stat_fn(pathStr->data, &st) != 0) {
    TS_THROW(ts_value_string(ts_string_new("File not found")));
    return ts_value_undefined();
  }
  TSHashMap* obj = ts_hashmap_new();
  ts_hashmap_set(obj, ts_string_new("size"), ts_value_number((double)st.st_size));
  ts_hashmap_set(obj, ts_string_new("mtime"), ts_value_number((double)st.st_mtime * 1000.0));
#ifdef _WIN32
  ts_hashmap_set(obj, ts_string_new("isFile"), ts_value_boolean((st.st_mode & _S_IFREG) != 0));
  ts_hashmap_set(obj, ts_string_new("isDirectory"), ts_value_boolean((st.st_mode & _S_IFDIR) != 0));
#else
  ts_hashmap_set(obj, ts_string_new("isFile"), ts_value_boolean(S_ISREG(st.st_mode)));
  ts_hashmap_set(obj, ts_string_new("isDirectory"), ts_value_boolean(S_ISDIR(st.st_mode)));
#endif
  return ts_value_object(obj);
}
#endif /* TS_NEED_node_fs_statSync */

#if defined(TS_NEED_node_fs_rmdirSync)
int node_fs_rmdirSync(Value path) {
  TSString* pathStr = ts_to_string(path);
#ifdef _WIN32
  return _rmdir(pathStr->data);
#else
  return rmdir(pathStr->data);
#endif
}
#endif /* TS_NEED_node_fs_rmdirSync */

#if defined(TS_NEED_node_fs_renameSync)
int node_fs_renameSync(Value oldPath, Value newPath) {
  TSString* oldStr = ts_to_string(oldPath);
  TSString* newStr = ts_to_string(newPath);
  return rename(oldStr->data, newStr->data);
}
#endif /* TS_NEED_node_fs_renameSync */

#if defined(TS_NEED_node_fs_readlinkSync)
Value node_fs_readlinkSync(Value path) {
  TSString* pathStr = ts_to_string(path);
#ifdef _WIN32
  /* Windows: return path as-is (simplified) */
  return ts_value_string(ts_string_new(pathStr->data));
#else
  char buf[4096];
  ssize_t len = readlink(pathStr->data, buf, sizeof(buf) - 1);
  if (len == -1) {
    TS_THROW(ts_value_string(ts_string_new("Not a symbolic link")));
    return ts_value_undefined();
  }
  buf[len] = '\0';
  return ts_value_string(ts_string_new(buf));
#endif
}
#endif /* TS_NEED_node_fs_readlinkSync */

#if defined(TS_NEED_node_fs_symlinkSync)
int node_fs_symlinkSync(Value target, Value path) {
  TSString* targetStr = ts_to_string(target);
  TSString* pathStr = ts_to_string(path);
#ifdef _WIN32
  return CreateSymbolicLinkA(pathStr->data, targetStr->data, 0) ? 0 : -1;
#else
  return symlink(targetStr->data, pathStr->data);
#endif
}
#endif /* TS_NEED_node_fs_symlinkSync */

#if defined(TS_NEED_node_fs_chmodSync)
int node_fs_chmodSync(Value path, Value mode) {
  TSString* pathStr = ts_to_string(path);
  int modeInt = (int)ts_to_number(mode);
#ifdef _WIN32
  return _chmod(pathStr->data, modeInt);
#else
  return chmod(pathStr->data, modeInt);
#endif
}
#endif /* TS_NEED_node_fs_chmodSync */

/* ==================== Asynchronous Functions (thread pool) ==================== */
/* Worker threads only touch raw C strings / malloc buffers.
   Main thread builds Value objects and resolves Promises. */

static char* fs_dup_cstr(const char* s) {
  if (!s) s = "";
  size_t n = strlen(s);
  char* p = (char*)malloc(n + 1);
  if (!p) return NULL;
  memcpy(p, s, n + 1);
  return p;
}

static char* fs_path_dup(Value path) {
  TSString* ps = ts_to_string(path);
  return fs_dup_cstr(ps && ps->data ? ps->data : "");
}

static void fs_submit(TsJobFn work, TsJobFn complete, void* userdata) {
  TsJob* job = (TsJob*)malloc(sizeof(TsJob));
  if (!job) {
    if (complete) complete(userdata);
    return;
  }
  job->work = work;
  job->complete = complete;
  job->userdata = userdata;
  job->next = NULL;
  ts_thread_pool_submit(job);
}

/* ---- readFile ---- */
typedef struct {
  char* path;
  int as_string;       /* encoding present */
  Value promise;
  int ok;
  uint8_t* data;
  size_t len;
  char* err;
} FsReadJob;

static void fs_read_work(void* ud) {
  FsReadJob* j = (FsReadJob*)ud;
  FILE* f = fopen(j->path, "rb");
  if (!f) {
    j->ok = 0;
    j->err = fs_dup_cstr("File not found");
    return;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  if (size < 0) size = 0;
  fseek(f, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)size + 1);
  if (!data) {
    fclose(f);
    j->ok = 0;
    j->err = fs_dup_cstr("Out of memory");
    return;
  }
  size_t nread = fread(data, 1, (size_t)size, f);
  data[nread] = '\0';
  fclose(f);
  j->ok = 1;
  j->data = data;
  j->len = nread;
}

static void fs_read_complete(void* ud) {
  FsReadJob* j = (FsReadJob*)ud;
  if (j->ok) {
    Value result;
    if (j->as_string) {
      result = ts_value_string(ts_string_new_len((const char*)j->data, (int32_t)j->len));
      free(j->data);
    } else {
      Buffer* buf = (Buffer*)malloc(sizeof(Buffer));
      buf->type_tag = BUFFER_TAG;
      buf->length = (int32_t)j->len;
      buf->capacity = (int32_t)j->len > 0 ? (int32_t)j->len : 16;
      buf->data = j->data;
      result = ts_value_object(buf);
    }
    ts_promise_resolve(j->promise, result);
  } else {
    ts_promise_reject(j->promise, ts_value_string(ts_string_new(j->err ? j->err : "readFile failed")));
  }
  free(j->path);
  free(j->err);
  free(j);
}

#if defined(TS_NEED_node_fs_readFile)
Value node_fs_readFile(Value path, Value options) {
  Value p = ts_promise_new();
  FsReadJob* j = (FsReadJob*)calloc(1, sizeof(FsReadJob));
  j->path = fs_path_dup(path);
  j->as_string = fs_options_has_encoding(options);
  j->promise = p;
  fs_submit(fs_read_work, fs_read_complete, j);
  return p;
}
#endif /* TS_NEED_node_fs_readFile */

/* ---- writeFile ---- */
typedef struct {
  char* path;
  uint8_t* data;
  size_t len;
  Value promise;
  int ok;
} FsWriteJob;

static void fs_write_work(void* ud) {
  FsWriteJob* j = (FsWriteJob*)ud;
  FILE* f = fopen(j->path, "wb");
  if (!f) { j->ok = 0; return; }
  if (j->data && j->len > 0) fwrite(j->data, 1, j->len, f);
  fclose(f);
  j->ok = 1;
}

static void fs_write_complete(void* ud) {
  FsWriteJob* j = (FsWriteJob*)ud;
  ts_promise_resolve(j->promise, ts_value_boolean(j->ok));
  free(j->path);
  free(j->data);
  free(j);
}

#if defined(TS_NEED_node_fs_writeFile)
Value node_fs_writeFile(Value path, Value data, Value options) {
  (void)options;
  Value p = ts_promise_new();
  FsWriteJob* j = (FsWriteJob*)calloc(1, sizeof(FsWriteJob));
  j->path = fs_path_dup(path);
  j->promise = p;
  if (data.tag == TAG_OBJECT && data.as.object &&
      *((int32_t*)data.as.object) == BUFFER_TAG) {
    Buffer* b = (Buffer*)data.as.object;
    if (b->data && b->length > 0) {
      j->len = (size_t)b->length;
      j->data = (uint8_t*)malloc(j->len);
      if (j->data) memcpy(j->data, b->data, j->len);
    }
  } else {
    TSString* s = ts_to_string(data);
    if (s && s->data && s->length > 0) {
      j->len = (size_t)s->length;
      j->data = (uint8_t*)malloc(j->len);
      if (j->data) memcpy(j->data, s->data, j->len);
    }
  }
  fs_submit(fs_write_work, fs_write_complete, j);
  return p;
}
#endif /* TS_NEED_node_fs_writeFile */

/* ---- access ---- */
typedef struct {
  char* path;
  Value promise;
  int exists;
} FsAccessJob;

static void fs_access_work(void* ud) {
  FsAccessJob* j = (FsAccessJob*)ud;
  stat_struct st;
  j->exists = (stat_fn(j->path, &st) == 0);
}

static void fs_access_complete(void* ud) {
  FsAccessJob* j = (FsAccessJob*)ud;
  ts_promise_resolve(j->promise, ts_value_boolean(j->exists));
  free(j->path);
  free(j);
}

#if defined(TS_NEED_node_fs_access)
Value node_fs_access(Value path, Value mode) {
  (void)mode;
  Value p = ts_promise_new();
  FsAccessJob* j = (FsAccessJob*)calloc(1, sizeof(FsAccessJob));
  j->path = fs_path_dup(path);
  j->promise = p;
  fs_submit(fs_access_work, fs_access_complete, j);
  return p;
}
#endif /* TS_NEED_node_fs_access */

/* ---- mkdir / unlink / rmdir / chmod (int result ops) ---- */
typedef struct {
  char* path;
  char* path2;   /* rename/symlink second path */
  int mode;
  int op;        /* 0=mkdir 1=unlink 2=rmdir 3=rename 4=symlink 5=chmod */
  Value promise;
  int result;
} FsIntJob;

static void fs_int_work(void* ud) {
  FsIntJob* j = (FsIntJob*)ud;
  switch (j->op) {
    case 0: j->result = mkdir_p(j->path, 0755); break;
#ifdef _WIN32
    case 1: j->result = _unlink(j->path); break;
    case 2: j->result = _rmdir(j->path); break;
#else
    case 1: j->result = unlink(j->path); break;
    case 2: j->result = rmdir(j->path); break;
#endif
    case 3: j->result = rename(j->path, j->path2); break;
#ifdef _WIN32
    case 4: j->result = CreateSymbolicLinkA(j->path2, j->path, 0) ? 0 : -1; break;
    case 5: j->result = _chmod(j->path, j->mode); break;
#else
    case 4: j->result = symlink(j->path, j->path2); break;
    case 5: j->result = chmod(j->path, j->mode); break;
#endif
    default: j->result = -1; break;
  }
}

static void fs_int_complete(void* ud) {
  FsIntJob* j = (FsIntJob*)ud;
  ts_promise_resolve(j->promise, ts_value_boolean(j->result == 0));
  free(j->path);
  free(j->path2);
  free(j);
}

static Value fs_int_async(Value path, Value path2, int mode, int op) {
  Value p = ts_promise_new();
  FsIntJob* j = (FsIntJob*)calloc(1, sizeof(FsIntJob));
  j->path = fs_path_dup(path);
  j->path2 = NULL;
  if (op == 3 || op == 4) {
    j->path2 = fs_path_dup(path2);
  }
  j->mode = mode;
  j->op = op;
  j->promise = p;
  fs_submit(fs_int_work, fs_int_complete, j);
  return p;
}

#if defined(TS_NEED_node_fs_mkdir)
Value node_fs_mkdir(Value path, Value options) {
  (void)options;
  return fs_int_async(path, ts_value_null(), 0, 0);
}
#endif /* TS_NEED_node_fs_mkdir */

#if defined(TS_NEED_node_fs_unlink)
Value node_fs_unlink(Value path) {
  return fs_int_async(path, ts_value_null(), 0, 1);
}
#endif /* TS_NEED_node_fs_unlink */

#if defined(TS_NEED_node_fs_rmdir)
Value node_fs_rmdir(Value path) {
  return fs_int_async(path, ts_value_null(), 0, 2);
}
#endif /* TS_NEED_node_fs_rmdir */

#if defined(TS_NEED_node_fs_rename)
Value node_fs_rename(Value oldPath, Value newPath) {
  return fs_int_async(oldPath, newPath, 0, 3);
}
#endif /* TS_NEED_node_fs_rename */

#if defined(TS_NEED_node_fs_symlink)
Value node_fs_symlink(Value target, Value path) {
  /* op 4: path=target stored in j->path, link path in j->path2 */
  return fs_int_async(target, path, 0, 4);
}
#endif /* TS_NEED_node_fs_symlink */

#if defined(TS_NEED_node_fs_chmod)
Value node_fs_chmod(Value path, Value mode) {
  return fs_int_async(path, ts_value_null(), (int)ts_to_number(mode), 5);
}
#endif /* TS_NEED_node_fs_chmod */

/* ---- readdir ---- */
typedef struct {
  char* path;
  Value promise;
  char** names;
  int count;
  int ok;
} FsReaddirJob;

static void fs_readdir_work(void* ud) {
  FsReaddirJob* j = (FsReaddirJob*)ud;
  int cap = 32;
  j->names = (char**)malloc(sizeof(char*) * (size_t)cap);
  j->count = 0;
  j->ok = 1;
#ifdef _WIN32
  WIN32_FIND_DATAA findData;
  char searchPath[1024];
  snprintf(searchPath, sizeof(searchPath), "%s\\*", j->path);
  HANDLE hFind = FindFirstFileA(searchPath, &findData);
  if (hFind == INVALID_HANDLE_VALUE) { j->ok = 1; return; } /* empty / missing → empty list */
  do {
    if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
      if (j->count >= cap) {
        cap *= 2;
        j->names = (char**)realloc(j->names, sizeof(char*) * (size_t)cap);
      }
      j->names[j->count++] = fs_dup_cstr(findData.cFileName);
    }
  } while (FindNextFileA(hFind, &findData));
  FindClose(hFind);
#else
  DIR* d = opendir(j->path);
  if (!d) { j->ok = 1; return; }
  struct dirent* entry;
  while ((entry = readdir(d)) != NULL) {
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
      if (j->count >= cap) {
        cap *= 2;
        j->names = (char**)realloc(j->names, sizeof(char*) * (size_t)cap);
      }
      j->names[j->count++] = fs_dup_cstr(entry->d_name);
    }
  }
  closedir(d);
#endif
}

static void fs_readdir_complete(void* ud) {
  FsReaddirJob* j = (FsReaddirJob*)ud;
  TSArray* arr = ts_array_new();
  for (int i = 0; i < j->count; i++) {
    ts_array_push(arr, ts_value_string(ts_string_new(j->names[i])));
    free(j->names[i]);
  }
  free(j->names);
  ts_promise_resolve(j->promise, ts_value_array(arr));
  free(j->path);
  free(j);
}

#if defined(TS_NEED_node_fs_readdir)
Value node_fs_readdir(Value path) {
  Value p = ts_promise_new();
  FsReaddirJob* j = (FsReaddirJob*)calloc(1, sizeof(FsReaddirJob));
  j->path = fs_path_dup(path);
  j->promise = p;
  fs_submit(fs_readdir_work, fs_readdir_complete, j);
  return p;
}
#endif /* TS_NEED_node_fs_readdir */

/* ---- stat ---- */
typedef struct {
  char* path;
  Value promise;
  int ok;
  long long size;
  double mtime_ms;
  int is_file;
  int is_dir;
} FsStatJob;

static void fs_stat_work(void* ud) {
  FsStatJob* j = (FsStatJob*)ud;
  stat_struct st;
  if (stat_fn(j->path, &st) != 0) { j->ok = 0; return; }
  j->ok = 1;
  j->size = (long long)st.st_size;
  j->mtime_ms = (double)st.st_mtime * 1000.0;
#ifdef _WIN32
  j->is_file = (st.st_mode & _S_IFREG) != 0;
  j->is_dir = (st.st_mode & _S_IFDIR) != 0;
#else
  j->is_file = S_ISREG(st.st_mode);
  j->is_dir = S_ISDIR(st.st_mode);
#endif
}

static void fs_stat_complete(void* ud) {
  FsStatJob* j = (FsStatJob*)ud;
  if (!j->ok) {
    ts_promise_reject(j->promise, ts_value_string(ts_string_new("File not found")));
  } else {
    TSHashMap* obj = ts_hashmap_new();
    ts_hashmap_set(obj, ts_string_new("size"), ts_value_number((double)j->size));
    ts_hashmap_set(obj, ts_string_new("mtime"), ts_value_number(j->mtime_ms));
    ts_hashmap_set(obj, ts_string_new("isFile"), ts_value_boolean(j->is_file));
    ts_hashmap_set(obj, ts_string_new("isDirectory"), ts_value_boolean(j->is_dir));
    ts_promise_resolve(j->promise, ts_value_object(obj));
  }
  free(j->path);
  free(j);
}

#if defined(TS_NEED_node_fs_stat)
Value node_fs_stat(Value path) {
  Value p = ts_promise_new();
  FsStatJob* j = (FsStatJob*)calloc(1, sizeof(FsStatJob));
  j->path = fs_path_dup(path);
  j->promise = p;
  fs_submit(fs_stat_work, fs_stat_complete, j);
  return p;
}
#endif /* TS_NEED_node_fs_stat */

/* ---- readlink ---- */
typedef struct {
  char* path;
  Value promise;
  int ok;
  char* target;
} FsReadlinkJob;

static void fs_readlink_work(void* ud) {
  FsReadlinkJob* j = (FsReadlinkJob*)ud;
#ifdef _WIN32
  j->ok = 1;
  j->target = fs_dup_cstr(j->path);
#else
  char buf[4096];
  ssize_t len = readlink(j->path, buf, sizeof(buf) - 1);
  if (len == -1) { j->ok = 0; return; }
  buf[len] = '\0';
  j->ok = 1;
  j->target = fs_dup_cstr(buf);
#endif
}

static void fs_readlink_complete(void* ud) {
  FsReadlinkJob* j = (FsReadlinkJob*)ud;
  if (!j->ok) {
    ts_promise_reject(j->promise, ts_value_string(ts_string_new("Not a symbolic link")));
  } else {
    ts_promise_resolve(j->promise, ts_value_string(ts_string_new(j->target)));
  }
  free(j->path);
  free(j->target);
  free(j);
}

#if defined(TS_NEED_node_fs_readlink)
Value node_fs_readlink(Value path) {
  Value p = ts_promise_new();
  FsReadlinkJob* j = (FsReadlinkJob*)calloc(1, sizeof(FsReadlinkJob));
  j->path = fs_path_dup(path);
  j->promise = p;
  fs_submit(fs_readlink_work, fs_readlink_complete, j);
  return p;
}
#endif /* TS_NEED_node_fs_readlink */
