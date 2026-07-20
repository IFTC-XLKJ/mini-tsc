#define _CRT_SECURE_NO_WARNINGS
#include "node_fs.h"
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

int node_fs_existsSync(Value path) {
  TSString* pathStr = ts_to_string(path);
  stat_struct st;
  return stat_fn(pathStr->data, &st) == 0;
}

int node_fs_mkdirSync(Value path, Value options) {
  TSString* pathStr = ts_to_string(path);
  return mkdir_p(pathStr->data, 0755);
}

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

int node_fs_unlinkSync(Value path) {
  TSString* pathStr = ts_to_string(path);
#ifdef _WIN32
  return _unlink(pathStr->data);
#else
  return unlink(pathStr->data);
#endif
}

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

int node_fs_rmdirSync(Value path) {
  TSString* pathStr = ts_to_string(path);
#ifdef _WIN32
  return _rmdir(pathStr->data);
#else
  return rmdir(pathStr->data);
#endif
}

int node_fs_renameSync(Value oldPath, Value newPath) {
  TSString* oldStr = ts_to_string(oldPath);
  TSString* newStr = ts_to_string(newPath);
  return rename(oldStr->data, newStr->data);
}

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

int node_fs_symlinkSync(Value target, Value path) {
  TSString* targetStr = ts_to_string(target);
  TSString* pathStr = ts_to_string(path);
#ifdef _WIN32
  return CreateSymbolicLinkA(pathStr->data, targetStr->data, 0) ? 0 : -1;
#else
  return symlink(targetStr->data, pathStr->data);
#endif
}

int node_fs_chmodSync(Value path, Value mode) {
  TSString* pathStr = ts_to_string(path);
  int modeInt = (int)ts_to_number(mode);
#ifdef _WIN32
  return _chmod(pathStr->data, modeInt);
#else
  return chmod(pathStr->data, modeInt);
#endif
}

/* ==================== Asynchronous Functions ==================== */
/* These wrap the synchronous versions for now.
   In a real implementation, these would use a thread pool. */

Value node_fs_readFile(Value path, Value options) {
  return node_fs_readFileSync(path, options);
}

Value node_fs_writeFile(Value path, Value data, Value options) {
  int result = node_fs_writeFileSync(path, data, options);
  return ts_value_boolean(result == 0);
}

Value node_fs_access(Value path, Value mode) {
  TSString* pathStr = ts_to_string(path);
  stat_struct st;
  int exists = stat_fn(pathStr->data, &st) == 0;
  return ts_value_boolean(exists);
}

Value node_fs_mkdir(Value path, Value options) {
  int result = node_fs_mkdirSync(path, options);
  return ts_value_boolean(result == 0);
}

Value node_fs_readdir(Value path) {
  return node_fs_readdirSync(path);
}

Value node_fs_unlink(Value path) {
  int result = node_fs_unlinkSync(path);
  return ts_value_boolean(result == 0);
}

Value node_fs_stat(Value path) {
  return node_fs_statSync(path);
}

Value node_fs_rmdir(Value path) {
  int result = node_fs_rmdirSync(path);
  return ts_value_boolean(result == 0);
}

Value node_fs_rename(Value oldPath, Value newPath) {
  int result = node_fs_renameSync(oldPath, newPath);
  return ts_value_boolean(result == 0);
}

Value node_fs_readlink(Value path) {
  return node_fs_readlinkSync(path);
}

Value node_fs_symlink(Value target, Value path) {
  int result = node_fs_symlinkSync(target, path);
  return ts_value_boolean(result == 0);
}

Value node_fs_chmod(Value path, Value mode) {
  int result = node_fs_chmodSync(path, mode);
  return ts_value_boolean(result == 0);
}
