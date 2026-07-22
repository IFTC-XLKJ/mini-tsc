#include "test_websocket.h"

extern void __init_test_websocket(void);

void __init_all_modules(void) {
  __init_test_websocket();
}