#include "test_7zip_password.h"

extern void __init_test_7zip_password(void);

void __init_all_modules(void) {
  __init_test_7zip_password();
}