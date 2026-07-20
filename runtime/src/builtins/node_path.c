#include "node_path.h"

#ifdef _WIN32
#define PATH_SEP '\\'
#define PATH_SEP_STR "\\"
#else
#define PATH_SEP '/'
#define PATH_SEP_STR "/"
#endif

static void join_path(char* buf, size_t bufSize, const char* a, const char* b) {
  size_t lenA = strlen(a);
  if (lenA > 0 && a[lenA - 1] != PATH_SEP && a[lenA - 1] != '/') {
    snprintf(buf, bufSize, "%s%s%s", a, PATH_SEP_STR, b);
  } else {
    snprintf(buf, bufSize, "%s%s", a, b);
  }
}

Value node_path_join(Value* args, int argc) {
  if (argc == 0) return ts_value_string(ts_string_new(""));

  char buf[4096];
  TSString* first = ts_to_string(args[0]);
  snprintf(buf, sizeof(buf), "%s", first->data);

  for (int i = 1; i < argc; i++) {
    TSString* part = ts_to_string(args[i]);
    char tmp[4096];
    join_path(tmp, sizeof(tmp), buf, part->data);
    snprintf(buf, sizeof(buf), "%s", tmp);
  }

  return ts_value_string(ts_string_new(buf));
}

Value node_path_resolve(Value* args, int argc) {
  if (argc == 0) return ts_value_string(ts_string_new(""));

  char buf[4096];
  TSString* first = ts_to_string(args[0]);
  snprintf(buf, sizeof(buf), "%s", first->data);

  for (int i = 1; i < argc; i++) {
    TSString* part = ts_to_string(args[i]);
    if (part->data[0] == PATH_SEP || (part->data[0] == '.' && part->length > 1 && part->data[1] == PATH_SEP)) {
      snprintf(buf, sizeof(buf), "%s", part->data);
    } else {
      char tmp[4096];
      join_path(tmp, sizeof(tmp), buf, part->data);
      snprintf(buf, sizeof(buf), "%s", tmp);
    }
  }

  return ts_value_string(ts_string_new(buf));
}

Value node_path_basename(Value path, Value ext) {
  TSString* pathStr = ts_to_string(path);
  const char* base = strrchr(pathStr->data, PATH_SEP);
  if (!base) base = strrchr(pathStr->data, '/');
  if (base) base++; else base = pathStr->data;

  if (ext.tag != TAG_NULL) {
    TSString* extStr = ts_to_string(ext);
    size_t baseLen = strlen(base);
    size_t extLen = extStr->length;
    if (baseLen >= extLen && strcmp(base + baseLen - extLen, extStr->data) == 0) {
      char buf[1024];
      snprintf(buf, sizeof(buf), "%.*s", (int)(baseLen - extLen), base);
      return ts_value_string(ts_string_new(buf));
    }
  }

  return ts_value_string(ts_string_new(base));
}

Value node_path_dirname(Value path) {
  TSString* pathStr = ts_to_string(path);
  const char* lastSep = strrchr(pathStr->data, PATH_SEP);
  if (!lastSep) lastSep = strrchr(pathStr->data, '/');
  if (!lastSep) return ts_value_string(ts_string_new("."));

  if (lastSep == pathStr->data) {
    return ts_value_string(ts_string_new(PATH_SEP_STR));
  }

  char buf[1024];
  snprintf(buf, sizeof(buf), "%.*s", (int)(lastSep - pathStr->data), pathStr->data);
  return ts_value_string(ts_string_new(buf));
}

Value node_path_extname(Value path) {
  TSString* pathStr = ts_to_string(path);
  const char* base = strrchr(pathStr->data, PATH_SEP);
  if (!base) base = strrchr(pathStr->data, '/');
  if (base) base++; else base = pathStr->data;

  const char* dot = strrchr(base, '.');
  if (!dot || dot == base) return ts_value_string(ts_string_new(""));
  return ts_value_string(ts_string_new(dot));
}

Value node_path_normalize(Value path) {
  TSString* pathStr = ts_to_string(path);
  /* Simplified: just return as-is */
  return ts_value_string(ts_string_new(pathStr->data));
}

Value node_path_isAbsolute(Value path) {
  TSString* pathStr = ts_to_string(path);
  if (!pathStr || !pathStr->data || pathStr->length == 0) {
    return ts_value_boolean(0);
  }
  const char* p = pathStr->data;
#ifdef _WIN32
  /* C:\ or \\server or / */
  if (p[0] == '/' || p[0] == '\\') return ts_value_boolean(1);
  if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) &&
      p[1] == ':' && (p[2] == '\\' || p[2] == '/' || p[2] == '\0')) {
    return ts_value_boolean(1);
  }
  return ts_value_boolean(0);
#else
  return ts_value_boolean(p[0] == '/');
#endif
}

Value node_path_parse(Value path) {
  TSString* pathStr = ts_to_string(path);
  const char* p = pathStr && pathStr->data ? pathStr->data : "";
  TSHashMap* map = ts_hashmap_new();

  /* root */
  char root[8] = "";
#ifdef _WIN32
  if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') {
    root[0] = p[0]; root[1] = ':';
    if (p[2] == '\\' || p[2] == '/') { root[2] = p[2]; root[3] = '\0'; }
    else root[2] = '\0';
  } else if (p[0] == '/' || p[0] == '\\') {
    root[0] = p[0]; root[1] = '\0';
  }
#else
  if (p[0] == '/') { root[0] = '/'; root[1] = '\0'; }
#endif
  ts_hashmap_set(map, ts_string_new("root"), ts_value_string(ts_string_new(root)));

  /* dir / base via dirname/basename */
  Value dirVal = node_path_dirname(path);
  Value baseVal = node_path_basename(path, ts_value_null());
  Value extVal = node_path_extname(path);
  ts_hashmap_set(map, ts_string_new("dir"), dirVal);
  ts_hashmap_set(map, ts_string_new("base"), baseVal);
  ts_hashmap_set(map, ts_string_new("ext"), extVal);

  /* name = base without ext */
  TSString* baseStr = ts_to_string(baseVal);
  TSString* extStr = ts_to_string(extVal);
  char nameBuf[1024];
  if (baseStr && extStr && extStr->length > 0 &&
      baseStr->length >= extStr->length &&
      strcmp(baseStr->data + baseStr->length - extStr->length, extStr->data) == 0) {
    snprintf(nameBuf, sizeof(nameBuf), "%.*s",
             (int)(baseStr->length - extStr->length), baseStr->data);
  } else {
    snprintf(nameBuf, sizeof(nameBuf), "%s", baseStr && baseStr->data ? baseStr->data : "");
  }
  ts_hashmap_set(map, ts_string_new("name"), ts_value_string(ts_string_new(nameBuf)));

  return ts_value_object(map);
}

Value node_path_format(Value pathObject) {
  if (pathObject.tag != TAG_OBJECT || !pathObject.as.object) {
    return ts_value_string(ts_string_new(""));
  }
  TSHashMap* map = (TSHashMap*)pathObject.as.object;
  Value dirV = ts_hashmap_get(map, ts_string_new("dir"));
  Value rootV = ts_hashmap_get(map, ts_string_new("root"));
  Value baseV = ts_hashmap_get(map, ts_string_new("base"));
  Value nameV = ts_hashmap_get(map, ts_string_new("name"));
  Value extV = ts_hashmap_get(map, ts_string_new("ext"));

  char base[1024];
  if (baseV.tag == TAG_STRING && baseV.as.string && baseV.as.string->data) {
    snprintf(base, sizeof(base), "%s", baseV.as.string->data);
  } else {
    TSString* name = ts_to_string(nameV);
    TSString* ext = ts_to_string(extV);
    snprintf(base, sizeof(base), "%s%s",
             name && name->data ? name->data : "",
             ext && ext->data ? ext->data : "");
  }

  char dir[2048];
  if (dirV.tag == TAG_STRING && dirV.as.string && dirV.as.string->data &&
      dirV.as.string->length > 0) {
    snprintf(dir, sizeof(dir), "%s", dirV.as.string->data);
  } else if (rootV.tag == TAG_STRING && rootV.as.string && rootV.as.string->data) {
    snprintf(dir, sizeof(dir), "%s", rootV.as.string->data);
  } else {
    dir[0] = '\0';
  }

  if (dir[0] == '\0') {
    return ts_value_string(ts_string_new(base));
  }

  /* Avoid double sep when dir is root "/" */
  size_t dlen = strlen(dir);
  char out[4096];
  if (dlen > 0 && (dir[dlen - 1] == '/' || dir[dlen - 1] == '\\')) {
    snprintf(out, sizeof(out), "%s%s", dir, base);
  } else {
    snprintf(out, sizeof(out), "%s%s%s", dir, PATH_SEP_STR, base);
  }
  return ts_value_string(ts_string_new(out));
}

Value node_path_relative(Value from, Value to) {
  /* Simplified: if equal return ""; else return `to` (full relative resolution is complex) */
  TSString* fromStr = ts_to_string(from);
  TSString* toStr = ts_to_string(to);
  if (fromStr && toStr && fromStr->data && toStr->data &&
      strcmp(fromStr->data, toStr->data) == 0) {
    return ts_value_string(ts_string_new(""));
  }
  if (toStr && toStr->data) {
    return ts_value_string(ts_string_new(toStr->data));
  }
  return ts_value_string(ts_string_new(""));
}
