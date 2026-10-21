#include "test_quickjs.h"

extern void __init_test_quickjs(void);

void __init_all_modules(void) {
  __init_test_quickjs();
}