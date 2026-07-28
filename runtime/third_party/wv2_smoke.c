#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include "WebView2.h"
int main(void) {
  LPWSTR v = NULL;
  GetAvailableCoreWebView2BrowserVersionString(NULL, &v);
  return v ? 0 : 1;
}
