#include "test_my_app_main.h"

extern void __init_test_my_app_main(void);

void __init_all_modules(void) {
  __init_test_my_app_main();
}