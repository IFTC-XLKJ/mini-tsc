#include "node_uuid.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
static uint64_t get_time_ms(void) {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  return t / 10000 - 11644473600000ULL;
}
#else
#include <time.h>
static uint64_t get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
#endif

static uint64_t uuid_v7_counter = 0;

Value node_uuid_v4(void) {
  uint8_t bytes[16];
  for (int i = 0; i < 16; i++) bytes[i] = (uint8_t)(rand() & 0xFF);
  bytes[6] = (bytes[6] & 0x0f) | 0x40;
  bytes[8] = (bytes[8] & 0x3f) | 0x80;
  char uuid[37];
  snprintf(uuid, sizeof(uuid),
    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],
    bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
  return ts_value_string(ts_string_new(uuid));
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

Value node_uuid_validate(TSString* s) {
  if (!s || !s->data) return ts_value_number(0);
  const char* str = s->data;
  int len = s->length;
  if (len != 36) return ts_value_number(0);
  for (int i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (str[i] != '-') return ts_value_number(0);
    } else {
      if (hex_digit(str[i]) < 0) return ts_value_number(0);
    }
  }
  return ts_value_number(1);
}

Value node_uuid_v7(void) {
  uint64_t ms = get_time_ms();
  uuid_v7_counter = (uuid_v7_counter + 1) & 0xFFF;
  uint8_t bytes[16];
  bytes[0] = (ms >> 40) & 0xFF;
  bytes[1] = (ms >> 32) & 0xFF;
  bytes[2] = (ms >> 24) & 0xFF;
  bytes[3] = (ms >> 16) & 0xFF;
  bytes[4] = (ms >> 8) & 0xFF;
  bytes[5] = ms & 0xFF;
  bytes[6] = 0x70 | ((uuid_v7_counter >> 8) & 0x0F);
  bytes[7] = uuid_v7_counter & 0xFF;
  for (int i = 8; i < 16; i++) bytes[i] = (uint8_t)(rand() & 0xFF);
  bytes[8] = (bytes[8] & 0x3f) | 0x80;
  char uuid[37];
  snprintf(uuid, sizeof(uuid),
    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],
    bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
  return ts_value_string(ts_string_new(uuid));
}
