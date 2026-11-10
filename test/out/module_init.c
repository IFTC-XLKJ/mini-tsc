#include "test_7zip.h"

extern void __init_test_7zip(void);

void __init_all_modules(void) {
  __init_test_7zip();
}