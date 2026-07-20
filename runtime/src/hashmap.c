#include "runtime.h"

#define HASHMAP_INITIAL_CAPACITY 64
#define HASHMAP_LOAD_FACTOR 0.75

static uint32_t hash_string(const char* str) {
  uint32_t hash = 5381;
  while (*str) {
    hash = ((hash << 5) + hash) + (unsigned char)*str++;
  }
  return hash;
}

TSHashMap* ts_hashmap_new(void) {
  TSHashMap* map = (TSHashMap*)malloc(sizeof(TSHashMap));
  map->refcount = 1;
  map->size = 0;
  map->capacity = HASHMAP_INITIAL_CAPACITY;
  map->entries = (HashEntry*)calloc(HASHMAP_INITIAL_CAPACITY, sizeof(HashEntry));
  return map;
}

static void ts_hashmap_resize(TSHashMap* map) {
  int32_t oldCapacity = map->capacity;
  HashEntry* oldEntries = map->entries;

  map->capacity *= 2;
  map->entries = (HashEntry*)calloc(map->capacity, sizeof(HashEntry));
  map->size = 0;

  for (int32_t i = 0; i < oldCapacity; i++) {
    if (oldEntries[i].occupied) {
      ts_hashmap_set(map, oldEntries[i].key, oldEntries[i].value);
    }
  }
  free(oldEntries);
}

void ts_hashmap_set(TSHashMap* map, TSString* key, Value val) {
  if (map->size >= (int32_t)(map->capacity * HASHMAP_LOAD_FACTOR)) {
    ts_hashmap_resize(map);
  }

  uint32_t idx = hash_string(key->data) % map->capacity;
  while (map->entries[idx].occupied && !ts_string_equals(map->entries[idx].key, key)) {
    idx = (idx + 1) % map->capacity;
  }

  int was_new = !map->entries[idx].occupied;
  map->entries[idx].key = key;
  map->entries[idx].value = val;
  map->entries[idx].occupied = 1;
  if (was_new) map->size++;
}

Value ts_hashmap_get(TSHashMap* map, TSString* key) {
  if (!map) return ts_value_undefined();
  uint32_t idx = hash_string(key->data) % map->capacity;
  while (map->entries[idx].occupied) {
    if (ts_string_equals(map->entries[idx].key, key)) {
      return map->entries[idx].value;
    }
    idx = (idx + 1) % map->capacity;
  }
  return ts_value_undefined();
}

int ts_hashmap_has(TSHashMap* map, TSString* key) {
  uint32_t idx = hash_string(key->data) % map->capacity;
  while (map->entries[idx].occupied) {
    if (ts_string_equals(map->entries[idx].key, key)) {
      return 1;
    }
    idx = (idx + 1) % map->capacity;
  }
  return 0;
}

void ts_hashmap_free(TSHashMap* map) {
  map->refcount--;
  if (map->refcount <= 0) {
    free(map->entries);
    free(map);
  }
}

void ts_hashmap_for_each(TSHashMap* map, void (*callback)(TSString* key, Value value, void* ctx), void* ctx) {
  if (!map) return;
  for (int32_t i = 0; i < map->capacity; i++) {
    if (map->entries[i].occupied) {
      callback(map->entries[i].key, map->entries[i].value, ctx);
    }
  }
}

int32_t ts_hashmap_count(TSHashMap* map) {
  if (!map) return 0;
  int32_t count = 0;
  for (int32_t i = 0; i < map->capacity; i++) {
    if (map->entries[i].occupied) count++;
  }
  return count;
}

TSString* ts_hashmap_to_string(TSHashMap* map) {
  if (!map || map->size == 0) return ts_string_new("{}");

  TSString* result = ts_string_new("{");
  TSString* tmp;
  int first = 1;

  for (int32_t i = 0; i < map->capacity; i++) {
    if (map->entries[i].occupied) {
      if (!first) {
        TSString* comma = ts_string_new(", ");
        tmp = ts_string_concat(result, comma);
        ts_string_free(result);
        result = tmp;
        ts_string_free(comma);
      }
      first = 0;

      // Add key
      TSString* quote1 = ts_string_new("\"");
      tmp = ts_string_concat(result, quote1);
      ts_string_free(result);
      result = tmp;
      ts_string_free(quote1);

      tmp = ts_string_concat(result, map->entries[i].key);
      ts_string_free(result);
      result = tmp;

      tmp = ts_string_concat(result, quote1);
      ts_string_free(result);
      result = tmp;

      // Add ": "
      TSString* colon = ts_string_new(": ");
      tmp = ts_string_concat(result, colon);
      ts_string_free(result);
      result = tmp;
      ts_string_free(colon);

      // Add value (quote strings so objects look JSON-ish)
      Value v = map->entries[i].value;
      if (v.tag == TAG_STRING) {
        TSString* q = ts_string_new("\"");
        tmp = ts_string_concat(result, q);
        ts_string_free(result);
        result = tmp;
        TSString* valStr = v.as.string ? v.as.string : ts_string_new("");
        tmp = ts_string_concat(result, valStr);
        ts_string_free(result);
        result = tmp;
        tmp = ts_string_concat(result, q);
        ts_string_free(result);
        result = tmp;
        ts_string_free(q);
      } else {
        TSString* valStr = ts_to_string(v);
        tmp = ts_string_concat(result, valStr);
        ts_string_free(result);
        result = tmp;
        ts_string_free(valStr);
      }
    }
  }

  TSString* closing = ts_string_new("}");
  tmp = ts_string_concat(result, closing);
  ts_string_free(result);
  ts_string_free(closing);
  return tmp;
}
