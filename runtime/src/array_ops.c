#include "runtime.h"

#define INITIAL_CAPACITY 16

TSArray* ts_array_new(void) {
  TSArray* arr = (TSArray*)malloc(sizeof(TSArray));
  arr->refcount = 1;
  arr->length = 0;
  arr->capacity = INITIAL_CAPACITY;
  arr->items = (Value*)calloc(INITIAL_CAPACITY, sizeof(Value));
  return arr;
}

TSArray* ts_array_from_values(Value* values, int32_t count) {
  TSArray* arr = ts_array_new();
  arr->length = count;
  if (count > arr->capacity) {
    arr->capacity = count;
    arr->items = (Value*)realloc(arr->items, count * sizeof(Value));
  }
  memcpy(arr->items, values, count * sizeof(Value));
  return arr;
}

void ts_array_push(TSArray* arr, Value val) {
  if (arr->length >= arr->capacity) {
    arr->capacity *= 2;
    arr->items = (Value*)realloc(arr->items, arr->capacity * sizeof(Value));
  }
  arr->items[arr->length++] = val;
}

Value ts_array_get(TSArray* arr, int32_t index) {
  if (index < 0 || index >= arr->length) {
    return ts_value_undefined();
  }
  return arr->items[index];
}

void ts_array_free(TSArray* arr) {
  arr->refcount--;
  if (arr->refcount <= 0) {
    free(arr->items);
    free(arr);
  }
}

void ts_array_set(TSArray* arr, int32_t index, Value val) {
  if (index >= 0 && index < arr->length) {
    arr->items[index] = val;
  }
}

int32_t ts_array_index_of(TSArray* arr, Value val) {
  for (int32_t i = 0; i < arr->length; i++) {
    if (arr->items[i].tag == val.tag) {
      if (val.tag == TAG_NUMBER && arr->items[i].as.number == val.as.number) return i;
      if (val.tag == TAG_STRING && ts_string_equals(arr->items[i].as.string, val.as.string)) return i;
      if (val.tag == TAG_BOOLEAN && arr->items[i].as.boolean == val.as.boolean) return i;
      if (val.tag == TAG_NULL) return i;
    }
  }
  return -1;
}

TSArray* ts_array_filter(TSArray* arr, int (*predicate)(Value)) {
  TSArray* result = ts_array_new();
  for (int32_t i = 0; i < arr->length; i++) {
    if (predicate(arr->items[i])) {
      ts_array_push(result, arr->items[i]);
    }
  }
  return result;
}

TSArray* ts_array_map(TSArray* arr, Value (*transform)(Value)) {
  TSArray* result = ts_array_new();
  for (int32_t i = 0; i < arr->length; i++) {
    ts_array_push(result, transform(arr->items[i]));
  }
  return result;
}

TSString* ts_array_join(TSArray* arr, TSString* separator) {
  if (!arr || arr->length == 0) return ts_string_new("");
  TSString* result = ts_to_string(arr->items[0]);
  for (int32_t i = 1; i < arr->length; i++) {
    result = ts_string_concat(result, separator);
    result = ts_string_concat(result, ts_to_string(arr->items[i]));
  }
  return result;
}

int ts_array_some(TSArray* arr, int (*predicate)(Value)) {
  for (int32_t i = 0; i < arr->length; i++) {
    if (predicate(arr->items[i])) return 1;
  }
  return 0;
}

int ts_array_every(TSArray* arr, int (*predicate)(Value)) {
  for (int32_t i = 0; i < arr->length; i++) {
    if (!predicate(arr->items[i])) return 0;
  }
  return 1;
}

Value ts_array_find(TSArray* arr, int (*predicate)(Value)) {
  for (int32_t i = 0; i < arr->length; i++) {
    if (predicate(arr->items[i])) return arr->items[i];
  }
  return ts_value_undefined();
}

Value ts_array_reduce(TSArray* arr, Value (*reducer)(Value, Value), Value initialValue) {
  Value acc = initialValue;
  for (int32_t i = 0; i < arr->length; i++) {
    acc = reducer(acc, arr->items[i]);
  }
  return acc;
}

void ts_array_foreach(TSArray* arr, void (*callback)(Value)) {
  for (int32_t i = 0; i < arr->length; i++) {
    callback(arr->items[i]);
  }
}

void ts_array_splice(TSArray* arr, int32_t start, int32_t deleteCount, Value* items, int32_t itemCount) {
  if (start < 0) start = 0;
  if (start > arr->length) start = arr->length;
  if (deleteCount > arr->length - start) deleteCount = arr->length - start;
  int32_t newLen = arr->length - deleteCount + itemCount;
  if (newLen > arr->capacity) {
    arr->capacity = newLen;
    arr->items = (Value*)realloc(arr->items, arr->capacity * sizeof(Value));
  }
  // Shift existing elements
  memmove(arr->items + start + itemCount, arr->items + start + deleteCount,
          (arr->length - start - deleteCount) * sizeof(Value));
  // Insert new items
  if (items && itemCount > 0) {
    memcpy(arr->items + start, items, itemCount * sizeof(Value));
  }
  arr->length = newLen;
}

Value ts_array_pop(TSArray* arr) {
  if (arr->length == 0) return ts_value_undefined();
  return arr->items[--arr->length];
}
