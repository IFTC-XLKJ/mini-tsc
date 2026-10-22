#include "test_7zip_plain.h"

extern void __init_test_7zip_plain(void);

void __init_all_modules(void) {
  __init_test_7zip_plain();
}