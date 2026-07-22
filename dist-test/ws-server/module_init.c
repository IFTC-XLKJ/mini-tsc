#include "test_server.h"

extern void __init_test_server(void);

void __init_all_modules(void) {
  __init_test_server();
}