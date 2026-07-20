#include "runtime.h"

TSString* ts_string_new(const char* cstr) {
  if (!cstr) cstr = "";
  return ts_string_new_len(cstr, (int32_t)strlen(cstr));
}

TSString* ts_string_new_len(const char* data, int32_t len) {
  if (len < 0) len = 0;
  TSString* s = (TSString*)malloc(sizeof(TSString));
  s->refcount = 1;
  s->length = len;
  s->data = (char*)malloc((size_t)len + 1);
  if (data && len > 0) {
    memcpy(s->data, data, (size_t)len);
  }
  s->data[len] = '\0';
  return s;
}

TSString* ts_string_concat(TSString* a, TSString* b) {
  int32_t aLen = a && a->data ? a->length : 0;
  int32_t bLen = b && b->data ? b->length : 0;
  int32_t totalLen = aLen + bLen;
  char* buf = (char*)malloc((size_t)totalLen + 1);
  if (a && a->data && aLen > 0) memcpy(buf, a->data, (size_t)aLen);
  if (b && b->data && bLen > 0) memcpy(buf + aLen, b->data, (size_t)bLen);
  buf[totalLen] = '\0';
  TSString* result = ts_string_new_len(buf, totalLen);
  free(buf);
  return result;
}

int ts_string_equals(TSString* a, TSString* b) {
  if (a->length != b->length) return 0;
  return memcmp(a->data, b->data, a->length) == 0;
}

TSString* ts_number_to_string(double n) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%g", n);
  return ts_string_new(buf);
}

char ts_string_char_at(TSString* s, int32_t index) {
  if (index < 0 || index >= s->length) return '\0';
  return s->data[index];
}

void ts_string_free(TSString* s) {
  s->refcount--;
  if (s->refcount <= 0) {
    free(s->data);
    free(s);
  }
}

TSString* ts_string_repeat(TSString* s, int32_t count) {
  if (!s || count <= 0) return ts_string_new("");
  int32_t totalLen = s->length * count;
  char* buf = (char*)malloc((size_t)totalLen + 1);
  for (int32_t i = 0; i < count; i++) {
    memcpy(buf + i * s->length, s->data, (size_t)s->length);
  }
  buf[totalLen] = '\0';
  TSString* result = ts_string_new_len(buf, totalLen);
  free(buf);
  return result;
}

/* String methods (always in string_ops — dead-code GC drops unused ones) */
int ts_string_index_of(TSString* haystack, TSString* needle) {
  if (!haystack || !haystack->data || !needle || !needle->data) return -1;
  char* pos = strstr(haystack->data, needle->data);
  if (!pos) return -1;
  return (int)(pos - haystack->data);
}

TSString* ts_string_substring(TSString* s, int32_t start, int32_t end) {
  if (!s || !s->data) return ts_string_new("");
  if (start < 0) start = 0;
  if (end > s->length) end = s->length;
  if (start >= end) return ts_string_new("");
  return ts_string_new_len(s->data + start, end - start);
}

TSString* ts_string_to_lower(TSString* s) {
  if (!s || !s->data) return ts_string_new("");
  char* buf = (char*)malloc((size_t)s->length + 1);
  for (int32_t i = 0; i < s->length; i++) {
    buf[i] = (s->data[i] >= 'A' && s->data[i] <= 'Z')
      ? (char)(s->data[i] + 32)
      : s->data[i];
  }
  buf[s->length] = '\0';
  TSString* result = ts_string_new_len(buf, s->length);
  free(buf);
  return result;
}

TSString* ts_string_to_upper(TSString* s) {
  if (!s || !s->data) return ts_string_new("");
  char* buf = (char*)malloc((size_t)s->length + 1);
  for (int32_t i = 0; i < s->length; i++) {
    buf[i] = (s->data[i] >= 'a' && s->data[i] <= 'z')
      ? (char)(s->data[i] - 32)
      : s->data[i];
  }
  buf[s->length] = '\0';
  TSString* result = ts_string_new_len(buf, s->length);
  free(buf);
  return result;
}

TSString* ts_string_trim(TSString* s) {
  if (!s || !s->data) return ts_string_new("");
  int32_t start = 0;
  int32_t end = s->length;
  while (start < end && (s->data[start] == ' ' || s->data[start] == '\t' ||
         s->data[start] == '\n' || s->data[start] == '\r')) {
    start++;
  }
  while (end > start && (s->data[end - 1] == ' ' || s->data[end - 1] == '\t' ||
         s->data[end - 1] == '\n' || s->data[end - 1] == '\r')) {
    end--;
  }
  return ts_string_substring(s, start, end);
}

int ts_string_starts_with(TSString* s, TSString* prefix) {
  if (!s || !s->data || !prefix || !prefix->data) return 0;
  if (prefix->length > s->length) return 0;
  return memcmp(s->data, prefix->data, (size_t)prefix->length) == 0;
}

int ts_string_ends_with(TSString* s, TSString* suffix) {
  if (!s || !s->data || !suffix || !suffix->data) return 0;
  if (suffix->length > s->length) return 0;
  return memcmp(s->data + s->length - suffix->length, suffix->data, (size_t)suffix->length) == 0;
}

int ts_string_includes(TSString* haystack, TSString* needle) {
  if (!haystack || !haystack->data || !needle || !needle->data) return 0;
  return strstr(haystack->data, needle->data) != NULL;
}

TSString* ts_string_replace(TSString* s, TSString* search, TSString* replacement) {
  if (!s || !s->data) return ts_string_new("");
  if (!search || !search->data || search->length == 0) return ts_string_new_len(s->data, s->length);
  if (!replacement) replacement = ts_string_new("");
  char* pos = strstr(s->data, search->data);
  if (!pos) return ts_string_new_len(s->data, s->length);
  int32_t prefixLen = (int32_t)(pos - s->data);
  int32_t totalLen = prefixLen + replacement->length + (s->length - prefixLen - search->length);
  char* buf = (char*)malloc((size_t)totalLen + 1);
  memcpy(buf, s->data, (size_t)prefixLen);
  memcpy(buf + prefixLen, replacement->data, (size_t)replacement->length);
  memcpy(buf + prefixLen + replacement->length, pos + search->length,
         (size_t)(s->length - prefixLen - search->length));
  buf[totalLen] = '\0';
  TSString* result = ts_string_new_len(buf, totalLen);
  free(buf);
  return result;
}

/* split needs array runtime — only compile when requested */
#include "ts_features.h"
#if defined(TS_NEED_ARRAY) || defined(TS_NEED_STRING_EXTRA)
TSArray* ts_string_split(TSString* s, TSString* separator) {
  TSArray* arr = ts_array_new();
  if (!s || !s->data || s->length == 0) {
    ts_array_push(arr, ts_value_string(ts_string_new("")));
    return arr;
  }
  if (!separator || !separator->data || separator->length == 0) {
    // Split into individual characters
    for (int32_t i = 0; i < s->length; i++) {
      char ch[2] = { s->data[i], '\0' };
      ts_array_push(arr, ts_value_string(ts_string_new_len(ch, 1)));
    }
    return arr;
  }
  int32_t start = 0;
  while (start <= s->length) {
    int32_t found = -1;
    for (int32_t i = start; i <= s->length - separator->length; i++) {
      if (memcmp(s->data + i, separator->data, separator->length) == 0) {
        found = i;
        break;
      }
    }
    if (found == -1) {
      // Last segment
      ts_array_push(arr, ts_value_string(ts_string_new_len(s->data + start, s->length - start)));
      break;
    }
    ts_array_push(arr, ts_value_string(ts_string_new_len(s->data + start, found - start)));
    start = found + separator->length;
  }
  return arr;
}
#endif
