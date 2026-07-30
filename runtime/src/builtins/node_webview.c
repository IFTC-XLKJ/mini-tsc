#include "node_webview.h"
#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <wincrypt.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")

typedef int socklen_t;

#define CINTERFACE
#define COBJMACROS
#include "WebView2.h"

#pragma comment(lib, "WebView2Loader.dll.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

/* WebSocket bridge for async addJavaScriptInterface */
typedef struct {
  int listen_fd;
  int client_fd;
  int port;
  int handshake_done;
  char* recv_buf;
  int recv_len;
  int recv_cap;
} WsBridge;

/* WebView2 instance data */
typedef struct {
  HWND hwnd;
  ICoreWebView2Controller* controller;
  ICoreWebView2* webview;
  char* url;
  int width;
  int height;
  char* title;
  char* icon;
  int show;
  int center;
  int ready;
  int frame;        /* 1 = show window frame (default), 0 = frameless */
  int transparent;  /* 1 = transparent background, 0 = opaque (default) */
  int devTools;     /* 1 = open devtools (default), 0 = hide devtools */
  /* Event listeners: map eventName → array of functions */
  TSHashMap* listeners;
  /* COM event tokens for cleanup */
  EventRegistrationToken token_nav_completed;
  EventRegistrationToken token_source_changed;
  EventRegistrationToken token_web_message;
  EventRegistrationToken token_title_changed;
  /* JavaScript interfaces: map name → JS shim script string */
  TSHashMap* interfaces;
  /* Interface method callbacks: map "name.method" → callback Function */
  TSHashMap* interfaceMethods;
  /* Async WebSocket bridge */
  WsBridge* bridge;
  int wsa_inited;
} WebViewInstance;

static const wchar_t* CLASS_NAME = L"MiniTscWebView";

/* Forward declarations */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* Convert char* to wchar_t* (caller must free) */
static wchar_t* to_wide(const char* str) {
  if (!str) return NULL;
  int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
  wchar_t* wstr = (wchar_t*)malloc(len * sizeof(wchar_t));
  if (wstr) MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, len);
  return wstr;
}

/* Register window class */
static void register_class(void) {
  static int registered = 0;
  if (registered) return;
  registered = 1;

  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandle(NULL);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = CLASS_NAME;
  wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
  RegisterClassExW(&wc);
}

/* Convert wchar_t* to char* (caller must free) */
static char* from_wide(const wchar_t* wstr) {
  if (!wstr) return NULL;
  int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
  char* str = (char*)malloc(len);
  if (str) WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL);
  return str;
}

/* ==================== WebSocket Bridge ==================== */

static void bridge_ensure_wsa(WebViewInstance* inst) {
  if (!inst->wsa_inited) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    inst->wsa_inited = 1;
  }
}

static int bridge_init(WebViewInstance* inst) {
  if (inst->bridge) return inst->bridge->port;
  bridge_ensure_wsa(inst);
  WsBridge* b = (WsBridge*)calloc(1, sizeof(WsBridge));
  if (!b) return -1;
  b->listen_fd = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (b->listen_fd < 0) { free(b); return -1; }
  int opt = 1;
  setsockopt(b->listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = 0; /* dynamic */
  if (bind(b->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    closesocket(b->listen_fd); free(b); return -1;
  }
  socklen_t addrlen = sizeof(addr);
  if (getsockname(b->listen_fd, (struct sockaddr*)&addr, &addrlen) < 0) {
    closesocket(b->listen_fd); free(b); return -1;
  }
  b->port = (int)ntohs(addr.sin_port);
  if (listen(b->listen_fd, 1) < 0) {
    closesocket(b->listen_fd); free(b); return -1;
  }
  u_long mode = 1;
  ioctlsocket(b->listen_fd, FIONBIO, &mode);
  b->client_fd = -1;
  b->handshake_done = 0;
  b->recv_cap = 4096;
  b->recv_buf = (char*)malloc(b->recv_cap);
  if (b->recv_buf) b->recv_buf[0] = '\0';
  b->recv_len = 0;
  inst->bridge = b;
  fprintf(stderr, "WebView: Bridge server listening on port %d\n", b->port);
  return b->port;
}

static void bridge_close_client(WsBridge* b) {
  if (!b) return;
  if (b->client_fd >= 0) {
    closesocket(b->client_fd);
    b->client_fd = -1;
  }
  b->handshake_done = 0;
  b->recv_len = 0;
  if (b->recv_buf) b->recv_buf[0] = '\0';
  fprintf(stderr, "WebView: Bridge client disconnected\n");
}

static void bridge_shutdown(WebViewInstance* inst) {
  if (!inst || !inst->bridge) return;
  WsBridge* b = inst->bridge;
  bridge_close_client(b);
  if (b->listen_fd >= 0) { closesocket(b->listen_fd); b->listen_fd = -1; }
  free(b->recv_buf);
  free(b);
  inst->bridge = NULL;
  fprintf(stderr, "WebView: Bridge server shut down\n");
}

static int bridge_send_frame(int fd, const char* data, int len) {
  if (fd < 0) return -1;
  uint8_t header[10];
  int hdrlen = 0;
  header[0] = 0x81; /* FIN + text */
  if (len < 126) {
    header[1] = (uint8_t)len;
    hdrlen = 2;
  } else if (len <= 0xFFFF) {
    header[1] = 126;
    header[2] = (uint8_t)(len >> 8);
    header[3] = (uint8_t)(len & 0xFF);
    hdrlen = 4;
  } else {
    return -1; /* too large */
  }
  if (send(fd, (const char*)header, hdrlen, 0) != hdrlen) return -1;
  if (len > 0 && send(fd, data, len, 0) != len) return -1;
  return 0;
}

static int bridge_recv_raw(int fd, char* buf, int cap) {
  int total = 0;
  while (total < cap) {
    int r = recv(fd, buf + total, cap - total, 0);
    if (r > 0) total += r;
    else if (r == 0) return -1; /* closed */
    else {
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) break;
      return -1;
    }
  }
  return total;
}

/* ---- Inline SHA-1 + Base64 for bridge handshake ---- */
#define SHA1_BLOCK 64
typedef struct { uint32_t state[5]; uint64_t count; uint8_t buffer[SHA1_BLOCK]; } BridgeSha1;

static void bridge_sha1_transform(uint32_t state[5], const uint8_t block[64]) {
  uint32_t a,b,c,d,e,w[80];
  for (int i = 0; i < 16; i++)
    w[i] = ((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|((uint32_t)block[i*4+2]<<8)|(uint32_t)block[i*4+3];
  for (int i = 16; i < 80; i++) { uint32_t t = w[i-3]^w[i-8]^w[i-14]^w[i-16]; w[i] = (t<<1)|(t>>31); }
  a=state[0]; b=state[1]; c=state[2]; d=state[3]; e=state[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f,t;
    if (i<20)      { f=(b&c)|((~b)&d);          t=0x5A827999; }
    else if (i<40) { f=b^c^d;                    t=0x6ED9EBA1; }
    else if (i<60) { f=(b&c)|(b&d)|(c&d);        t=0x8F1BBCDC; }
    else           { f=b^c^d;                    t=0xCA62C1D6; }
    uint32_t tmp = ((a<<5)|(a>>27))+f+e+t+w[i]; e=d; d=c; c=(b<<30)|(b>>2); b=a; a=tmp;
  }
  state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e;
}

static void bridge_sha1_init(BridgeSha1* h) {
  h->state[0]=0x67452301; h->state[1]=0xEFCDAB89; h->state[2]=0x98BADCFE; h->state[3]=0x10325476; h->state[4]=0xC3D2E1F0;
  h->count=0; memset(h->buffer,0,SHA1_BLOCK);
}

static void bridge_sha1_update(BridgeSha1* h, const uint8_t* data, size_t len) {
  size_t idx=(size_t)(h->count&63); h->count+=len;
  for (size_t i=0;i<len;i++) { h->buffer[idx++]=data[i]; if(idx==64){bridge_sha1_transform(h->state,h->buffer);idx=0;} }
}

static void bridge_sha1_final(BridgeSha1* h, uint8_t digest[20]) {
  uint64_t bits=h->count*8; uint32_t idx=(uint32_t)(h->count&63);
  h->buffer[idx++]=0x80;
  if(idx>56){while(idx<64)h->buffer[idx++]=0;bridge_sha1_transform(h->state,h->buffer);idx=0;}
  while(idx<56) h->buffer[idx++]=0;
  h->buffer[56]=(uint8_t)(bits>>24); h->buffer[57]=(uint8_t)(bits>>16);
  h->buffer[58]=(uint8_t)(bits>>8);  h->buffer[59]=(uint8_t)bits;
  bridge_sha1_transform(h->state,h->buffer);
  for(int i=0;i<5;i++){ digest[i*4]=(uint8_t)(h->state[i]>>24); digest[i*4+1]=(uint8_t)(h->state[i]>>16); digest[i*4+2]=(uint8_t)(h->state[i]>>8); digest[i*4+3]=(uint8_t)h->state[i]; }
}

static const char B64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void bridge_base64(const uint8_t* data, int len, char* out) {
  int j=0;
  for(int i=0;i<len;i+=3){
    uint32_t a=(uint32_t)data[i];
    uint32_t b=(i+1<len)?(uint32_t)data[i+1]:0;
    uint32_t c=(i+2<len)?(uint32_t)data[i+2]:0;
    uint32_t triple=(a<<16)|(b<<8)|c;
    out[j++]=B64[(triple>>18)&0x3F];
    out[j++]=B64[(triple>>12)&0x3F];
    out[j++]=(i+1<len)?B64[(triple>>6)&0x3F]:'=';
    out[j++]=(i+2<len)?B64[triple&0x3F]:'=';
  }
  out[j]='\0';
}

static int bridge_send_all(int fd, const char* data, int len) {
  int total = 0;
  while (total < len) {
    int sent = send(fd, data + total, len - total, 0);
    if (sent > 0) {
      total += sent;
    } else if (sent == 0) {
      return -1;
    } else {
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
        Sleep(1);
        continue;
      }
      return -1;
    }
  }
  return 0;
}

static int compute_accept_key(const char* key, char* out, size_t out_len) {
  HCRYPTPROV hProv = 0;
  HCRYPTHASH hHash = 0;
  if (!CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
    return -1;
  }
  if (!CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) {
    CryptReleaseContext(hProv, 0);
    return -1;
  }
  const char* GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  if (!CryptHashData(hHash, (BYTE*)key, (DWORD)strlen(key), 0)) goto hash_fail;
  if (!CryptHashData(hHash, (BYTE*)GUID, (DWORD)strlen(GUID), 0)) goto hash_fail;
  BYTE digest[20];
  DWORD dwHashLen = 20;
  if (!CryptGetHashParam(hHash, HP_HASHVAL, digest, &dwHashLen, 0)) goto hash_fail;

  DWORD base64Len = 0;
  if (!CryptBinaryToStringA(digest, 20, CRYPT_STRING_BASE64, NULL, &base64Len)) goto hash_fail;
  char* base64Str = (char*)malloc(base64Len);
  if (!base64Str) goto hash_fail;
  if (!CryptBinaryToStringA(digest, 20, CRYPT_STRING_BASE64, base64Str, &base64Len)) {
    free(base64Str);
    goto hash_fail;
  }
  int j = 0;
  for (size_t i = 0; base64Str[i] && j < (int)out_len - 1; i++) {
    if (base64Str[i] != '\r' && base64Str[i] != '\n') {
      out[j++] = base64Str[i];
    }
  }
  out[j] = '\0';
  free(base64Str);
  CryptDestroyHash(hHash);
  CryptReleaseContext(hProv, 0);
  return 0;
hash_fail:
  CryptDestroyHash(hHash);
  CryptReleaseContext(hProv, 0);
  return -1;
}

static void bridge_append_recv(WsBridge* b, const char* data, int len) {
  if (!b || !data || len <= 0) return;
  if (b->recv_len + len > b->recv_cap) {
    b->recv_cap = b->recv_len + len + 4096;
    b->recv_buf = (char*)realloc(b->recv_buf, b->recv_cap);
  }
  memcpy(b->recv_buf + b->recv_len, data, len);
  b->recv_len += len;
}

static int bridge_try_parse_frame(WsBridge* b, char** out_payload, int* out_len) {
  if (!b || b->recv_len < 2) return 0;

  unsigned char* buf = (unsigned char*)b->recv_buf;
  int opcode = buf[0] & 0x0F;
  int masked = buf[1] & 0x80;
  uint64_t payload_len = buf[1] & 0x7F;
  int header_len = 2;

  if (payload_len == 126) {
    header_len += 2;
    if (b->recv_len < header_len) return 0;
    payload_len = ((uint64_t)buf[2] << 8) | (uint64_t)buf[3];
  } else if (payload_len == 127) {
    header_len += 8;
    if (b->recv_len < header_len) return 0;
    payload_len = 0;
    for (int i = 0; i < 8; i++) {
      payload_len = (payload_len << 8) | (uint64_t)buf[2 + i];
    }
  }

  if (masked) header_len += 4;
  if (b->recv_len < header_len) return 0;

  if (payload_len > 65536) return -1;

  int total_len = header_len + (int)payload_len;
  if (b->recv_len < total_len) return 0;

  char* payload = (char*)malloc((size_t)payload_len + 1);
  if (!payload) return -1;

  if (payload_len > 0) {
    memcpy(payload, b->recv_buf + header_len, (size_t)payload_len);
    if (masked) {
      unsigned char* mask = buf + header_len - 4;
      for (uint64_t i = 0; i < payload_len; i++) {
        payload[i] ^= mask[i % 4];
      }
    }
  }
  payload[payload_len] = '\0';

  int remaining = b->recv_len - total_len;
  if (remaining > 0) {
    memmove(b->recv_buf, b->recv_buf + total_len, remaining);
  }
  b->recv_len = remaining;

  *out_payload = payload;
  *out_len = (int)payload_len;
  return opcode;
}

static void bridge_handle_handshake(WsBridge* b) {
  char temp[2048];
  int n = recv(b->client_fd, temp, sizeof(temp), 0);
  if (n > 0) {
    bridge_append_recv(b, temp, n);
  } else if (n == 0) {
    bridge_close_client(b);
    return;
  } else {
    int err = WSAGetLastError();
    if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
      bridge_close_client(b);
    }
    return;
  }

  if (b->recv_len < 4) return;
  if (b->recv_len > 8192) { bridge_close_client(b); return; }

  b->recv_buf[b->recv_len] = '\0';

  if (!strstr(b->recv_buf, "\r\n\r\n")) return;

  if (!strstr(b->recv_buf,"Upgrade: websocket") && !strstr(b->recv_buf,"upgrade: websocket")){
    bridge_close_client(b); return;
  }
  char* key_line = strstr(b->recv_buf,"Sec-WebSocket-Key:");
  if (!key_line) key_line = strstr(b->recv_buf,"sec-websocket-key:");
  if (!key_line) { bridge_close_client(b); return; }
  char* ks = key_line + 18;
  while(*ks==' ' || *ks=='\t') ks++;
  char key[128]={0}; int ki=0;
  while(*ks && *ks!='\r' && *ks!='\n' && ki<127) key[ki++]=*ks++;
  // trim trailing spaces from key
  while(ki>0 && (key[ki-1]==' ' || key[ki-1]=='\t')) { key[ki-1]='\0'; ki--; }

  char accept_key[64] = {0};
  if (compute_accept_key(key, accept_key, sizeof(accept_key)) != 0) {
    fprintf(stderr, "WebView: Failed to compute accept key for '%s'\n", key);
    bridge_close_client(b);
    return;
  }

  char resp[512];
  int rlen = snprintf(resp,sizeof(resp),
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Accept: %s\r\n"
    "\r\n", accept_key);
  if (bridge_send_all(b->client_fd, resp, rlen) != 0) {
    fprintf(stderr, "WebView: Failed to send handshake response\n");
    bridge_close_client(b);
    return;
  }
  b->handshake_done = 1;
  b->recv_len = 0;
  if (b->recv_buf) b->recv_buf[0] = '\0';
  fprintf(stderr, "WebView: Bridge handshake done\n");
}

static void bridge_dispatch_message(WebViewInstance* inst, const char* payload, int payload_len) {
  (void)payload_len;
  if (!inst || !inst->interfaceMethods || !payload) return;

  TSString* msgStr = ts_string_new(payload);
  Value parsed = ts_json_parse(msgStr);
  ts_string_free(msgStr);

  if (parsed.tag != TAG_OBJECT || !parsed.as.object) return;

  TSHashMap* map = (TSHashMap*)parsed.as.object;
  Value ifVal = ts_hashmap_get(map, ts_string_new("__if"));
  Value mVal = ts_hashmap_get(map, ts_string_new("__m"));
  Value aVal = ts_hashmap_get(map, ts_string_new("__a"));
  Value idVal = ts_hashmap_get(map, ts_string_new("__id"));

  if (ifVal.tag != TAG_STRING || !ifVal.as.string || mVal.tag != TAG_STRING || !mVal.as.string) return;

  size_t keyLen = strlen(ifVal.as.string->data) + 1 + strlen(mVal.as.string->data) + 1;
  char* compositeKey = (char*)malloc(keyLen);
  if (!compositeKey) return;
  snprintf(compositeKey, keyLen, "%s.%s", ifVal.as.string->data, mVal.as.string->data);
  Value cb = ts_hashmap_get(inst->interfaceMethods, ts_string_new(compositeKey));
  free(compositeKey);

  if ((cb.tag != TAG_FUNCTION || !cb.as.function) &&
      !(cb.tag == TAG_OBJECT && cb.as.object && *(int32_t*)cb.as.object == BOUND_FN_TAG)) {
    return;
  }

  TSArray* argsArr = (aVal.tag == TAG_ARRAY && aVal.as.array) ? aVal.as.array : NULL;
  int argc = argsArr ? argsArr->length : 0;
  static Value callArgs[16];
  for (int i = 0; i < argc && i < 16; i++) {
    callArgs[i] = ts_array_get(argsArr, i);
  }

  Value result = ts_value_call(cb, callArgs, argc);

  if (idVal.tag == TAG_STRING && idVal.as.string && idVal.as.string->data &&
      inst->bridge && inst->bridge->client_fd >= 0 && inst->bridge->handshake_done) {
    TSString* resultJson = ts_json_stringify(result);
    char resp[65536];
    int respLen = snprintf(resp, sizeof(resp), "{\"__id\":\"%s\",\"__res\":%s}",
                           idVal.as.string->data, resultJson ? resultJson->data : "null");
    if (resultJson) ts_string_free(resultJson);
    bridge_send_frame(inst->bridge->client_fd, resp, respLen);
  }

  fflush(stdout);
  fflush(stderr);
}

static void bridge_poll(WebViewInstance* inst) {
  if (!inst || !inst->bridge) return;
  WsBridge* b = inst->bridge;

  /* Always try to accept a new connection (listen_fd is non-blocking).
   * If a new client connects while another is still alive, replace it.
   * This handles page reloads / re-injections that create a new WebSocket. */
  if (b->listen_fd >= 0) {
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);
    int fd = (int)accept(b->listen_fd, (struct sockaddr*)&addr, &addrlen);
    if (fd >= 0) {
      if (b->client_fd >= 0) {
        bridge_close_client(b);
      }
      u_long mode = 1;
      ioctlsocket(fd, FIONBIO, &mode);
      b->client_fd = fd;
      b->handshake_done = 0;
      b->recv_len = 0;
      if (b->recv_buf) b->recv_buf[0] = '\0';
      fprintf(stderr, "WebView: Bridge client connected\n");
    }
  }
  if (b->client_fd < 0) return;

  if (!b->handshake_done) {
    bridge_handle_handshake(b);
    return;
  }

  char temp[4096];
  int n = recv(b->client_fd, temp, sizeof(temp), 0);
  if (n > 0) {
    bridge_append_recv(b, temp, n);
  } else if (n == 0) {
    bridge_close_client(b);
    return;
  } else {
    int err = WSAGetLastError();
    if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
      bridge_close_client(b);
      return;
    }
  }

  while (1) {
    char* payload = NULL;
    int payload_len = 0;
    int opcode = bridge_try_parse_frame(b, &payload, &payload_len);
    if (opcode == 0) break;
    if (opcode < 0) {
      bridge_close_client(b);
      return;
    }

    if (opcode == 0x08) {
      bridge_close_client(b);
      free(payload);
      return;
    }
    if (opcode == 0x09) {
      uint8_t pong[2] = {0x8A, 0x00};
      send(b->client_fd, (char*)pong, 2, 0);
      free(payload);
      continue;
    }
    if (opcode == 0x01 || opcode == 0x02) {
      bridge_dispatch_message(inst, payload, payload_len);
    }
    free(payload);
  }
}

static void webview_emit(WebViewInstance* inst, const char* event, Value* args, int argc) {
  if (!inst || !inst->listeners) return;
  Value arr = ts_hashmap_get(inst->listeners, ts_string_new(event));
  if (arr.tag == TAG_ARRAY && arr.as.array) {
    TSArray* a = arr.as.array;
    for (int i = 0; i < a->length; i++) {
      Value fn = ts_array_get(a, i);
      ts_value_call(fn, args, argc);
    }
  }
  fflush(stdout);
  fflush(stderr);
}

/* Window procedure */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  WebViewInstance* inst = (WebViewInstance*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

  /* Debug output for first few messages */
  static int msgDbgCount = 0;
  if (msgDbgCount < 10) {
    fprintf(stderr, "WndProc: msg=%u hwnd=%p inst=%p\n", msg, hwnd, inst);
    msgDbgCount++;
  }

  switch (msg) {
    case WM_CREATE: {
      CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
      SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
      return 0;
    }
    case WM_ERASEBKGND:
      /* Suppress background erase when WebView2 is active to avoid flashing over content */
      if (inst && inst->controller) return 1;
      break;
    case WM_SIZE:
      if (inst && inst->controller) {
        RECT bounds;
        GetClientRect(hwnd, &bounds);
        ICoreWebView2Controller_put_Bounds(inst->controller, bounds);
        if (inst) {
          Value w = ts_value_number((double)(bounds.right - bounds.left));
          Value h = ts_value_number((double)(bounds.bottom - bounds.top));
          Value args[2] = { w, h };
          webview_emit(inst, "resize", args, 2);
        }
      }
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_CLOSE:
      if (inst) {
        webview_emit(inst, "close", NULL, 0);
      }
      if (inst && inst->controller) {
        ICoreWebView2Controller_Close(inst->controller);
      }
      DestroyWindow(hwnd);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* Navigate to URL */
static void navigate_to_url(WebViewInstance* inst) {
  if (!inst->webview || !inst->url) {
    fprintf(stderr, "WebView: navigate_to_url skipped - webview=%p, url=%s\n",
            inst->webview, inst->url ? inst->url : "(null)");
    return;
  }
  wchar_t* wurl = to_wide(inst->url);
  if (wurl) {
    HRESULT hr = ICoreWebView2_Navigate(inst->webview, wurl);
    fprintf(stderr, "WebView: ICoreWebView2_Navigate result: 0x%08lx\n", hr);
    free(wurl);
  }
}

/* ==================== COM Event Handlers ==================== */

/* --- NavigationCompleted → "load" --- */
typedef struct {
  ICoreWebView2NavigationCompletedEventHandler handler;
  WebViewInstance* inst;
} NavCompletedHandler;

static HRESULT STDMETHODCALLTYPE NavCompleted_QueryInterface(
    ICoreWebView2NavigationCompletedEventHandler* This, REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2NavigationCompletedEventHandler)) {
    *ppv = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
  }
  return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE NavCompleted_AddRef(ICoreWebView2NavigationCompletedEventHandler* This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE NavCompleted_Release(ICoreWebView2NavigationCompletedEventHandler* This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE NavCompleted_Invoke(
    ICoreWebView2NavigationCompletedEventHandler* This,
    ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) {
  (void)sender; (void)args;
  NavCompletedHandler* h = (NavCompletedHandler*)This;
  /* Re-inject interface scripts for the newly loaded document.
   * The server-side bridge_poll auto-replaces old connections,
   * so we do NOT close the socket here (avoids killing a connection
   * that an iframe NavigationCompleted may otherwise destroy). */
  if (h->inst && h->inst->interfaces && h->inst->webview) {
    for (size_t i = 0; i < h->inst->interfaces->capacity; i++) {
      if (h->inst->interfaces->entries[i].occupied) {
        Value val = h->inst->interfaces->entries[i].value;
        if (val.tag == TAG_STRING && val.as.string && val.as.string->data) {
          wchar_t* wcode = to_wide(val.as.string->data);
          if (wcode) {
            ICoreWebView2_ExecuteScript(h->inst->webview, wcode, NULL);
            free(wcode);
          }
        }
      }
    }
  }
  webview_emit(h->inst, "load", NULL, 0);
  return S_OK;
}
static ICoreWebView2NavigationCompletedEventHandlerVtbl navCompletedVtbl = {
  NavCompleted_QueryInterface, NavCompleted_AddRef, NavCompleted_Release, NavCompleted_Invoke
};

/* --- SourceChanged → "navigate" --- */
typedef struct {
  ICoreWebView2SourceChangedEventHandler handler;
  WebViewInstance* inst;
} SourceChangedHandler;

static HRESULT STDMETHODCALLTYPE SourceChanged_QueryInterface(
    ICoreWebView2SourceChangedEventHandler* This, REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2SourceChangedEventHandler)) {
    *ppv = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
  }
  return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE SourceChanged_AddRef(ICoreWebView2SourceChangedEventHandler* This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE SourceChanged_Release(ICoreWebView2SourceChangedEventHandler* This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE SourceChanged_Invoke(
    ICoreWebView2SourceChangedEventHandler* This,
    ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs* args) {
  (void)args;
  SourceChangedHandler* h = (SourceChangedHandler*)This;
  char* url = NULL;
  if (sender) {
    LPWSTR urlW = NULL;
    ICoreWebView2_get_Source(sender, &urlW);
    if (urlW) {
      url = from_wide(urlW);
      CoTaskMemFree(urlW);
    }
  }
  Value arg = ts_value_string(ts_string_new(url ? url : ""));
  Value argsArr[1] = { arg };
  webview_emit(h->inst, "navigate", argsArr, 1);
  free(url);
  return S_OK;
}
static ICoreWebView2SourceChangedEventHandlerVtbl sourceChangedVtbl = {
  SourceChanged_QueryInterface, SourceChanged_AddRef, SourceChanged_Release, SourceChanged_Invoke
};

/* --- WebMessageReceived → "message" --- */
typedef struct {
  ICoreWebView2WebMessageReceivedEventHandler handler;
  WebViewInstance* inst;
} WebMessageReceivedHandler;

static HRESULT STDMETHODCALLTYPE WebMsg_QueryInterface(
    ICoreWebView2WebMessageReceivedEventHandler* This, REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2WebMessageReceivedEventHandler)) {
    *ppv = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
  }
  return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE WebMsg_AddRef(ICoreWebView2WebMessageReceivedEventHandler* This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE WebMsg_Release(ICoreWebView2WebMessageReceivedEventHandler* This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE WebMsg_Invoke(
    ICoreWebView2WebMessageReceivedEventHandler* This,
    ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) {
  (void)sender;
  WebMessageReceivedHandler* h = (WebMessageReceivedHandler*)This;
  char* msg = NULL;
  if (args) {
    LPWSTR msgW = NULL;
    ICoreWebView2WebMessageReceivedEventArgs_TryGetWebMessageAsString(args, &msgW);
    if (msgW) {
      msg = from_wide(msgW);
      CoTaskMemFree(msgW);
    }
  }
  int interfaceDispatched = 0;
  if (msg && h->inst && h->inst->interfaceMethods) {
    TSString* msgStr = ts_string_new(msg);
    Value parsed = ts_json_parse(msgStr);
    ts_string_free(msgStr);
    if (parsed.tag == TAG_OBJECT && parsed.as.object) {
      TSHashMap* map = (TSHashMap*)parsed.as.object;
      Value ifVal = ts_hashmap_get(map, ts_string_new("__if"));
      Value mVal = ts_hashmap_get(map, ts_string_new("__m"));
      Value aVal = ts_hashmap_get(map, ts_string_new("__a"));
      if (ifVal.tag == TAG_STRING && ifVal.as.string && mVal.tag == TAG_STRING && mVal.as.string) {
        size_t keyLen = strlen(ifVal.as.string->data) + 1 + strlen(mVal.as.string->data) + 1;
        char* compositeKey = (char*)malloc(keyLen);
        if (compositeKey) {
          snprintf(compositeKey, keyLen, "%s.%s", ifVal.as.string->data, mVal.as.string->data);
          Value cb = ts_hashmap_get(h->inst->interfaceMethods, ts_string_new(compositeKey));
          free(compositeKey);
          if ((cb.tag == TAG_FUNCTION && cb.as.function) || (cb.tag == TAG_OBJECT && cb.as.object && *(int32_t*)cb.as.object == BOUND_FN_TAG)) {
            TSArray* argsArr = (aVal.tag == TAG_ARRAY && aVal.as.array) ? aVal.as.array : NULL;
            int argc = argsArr ? argsArr->length : 0;
            static Value callArgs[16];
            for (int i = 0; i < argc && i < 16; i++) {
              callArgs[i] = ts_array_get(argsArr, i);
            }
            ts_value_call(cb, callArgs, argc);
            interfaceDispatched = 1;
            fflush(stdout);
            fflush(stderr);
          }
        }
      }
    }
  }
  if (!interfaceDispatched) {
    Value arg = ts_value_string(ts_string_new(msg ? msg : ""));
    Value argsArr[1] = { arg };
    webview_emit(h->inst, "message", argsArr, 1);
  }
  free(msg);
  return S_OK;
}
static ICoreWebView2WebMessageReceivedEventHandlerVtbl webMsgVtbl = {
  WebMsg_QueryInterface, WebMsg_AddRef, WebMsg_Release, WebMsg_Invoke
};

/* --- DocumentTitleChanged → "title" --- */
typedef struct {
  ICoreWebView2DocumentTitleChangedEventHandler handler;
  WebViewInstance* inst;
} TitleChangedHandler;

static HRESULT STDMETHODCALLTYPE TitleChanged_QueryInterface(
    ICoreWebView2DocumentTitleChangedEventHandler* This, REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2DocumentTitleChangedEventHandler)) {
    *ppv = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
  }
  return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE TitleChanged_AddRef(ICoreWebView2DocumentTitleChangedEventHandler* This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE TitleChanged_Release(ICoreWebView2DocumentTitleChangedEventHandler* This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE TitleChanged_Invoke(
    ICoreWebView2DocumentTitleChangedEventHandler* This,
    ICoreWebView2* sender, IUnknown* args) {
  (void)args;
  TitleChangedHandler* h = (TitleChangedHandler*)This;
  char* title = NULL;
  if (sender) {
    LPWSTR titleW = NULL;
    ICoreWebView2_get_DocumentTitle(sender, &titleW);
    if (titleW) {
      title = from_wide(titleW);
      CoTaskMemFree(titleW);
    }
  }
  Value arg = ts_value_string(ts_string_new(title ? title : ""));
  Value argsArr[1] = { arg };
  webview_emit(h->inst, "title", argsArr, 1);
  free(title);
  return S_OK;
}
static ICoreWebView2DocumentTitleChangedEventHandlerVtbl titleChangedVtbl = {
  TitleChanged_QueryInterface, TitleChanged_AddRef, TitleChanged_Release, TitleChanged_Invoke
};

/* Controller completion handler vtable - forward declarations */
static HRESULT STDMETHODCALLTYPE ControllerCreatedHandler_QueryInterface(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This,
    REFIID riid, void** ppvObject);
static ULONG STDMETHODCALLTYPE ControllerCreatedHandler_AddRef(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This);
static ULONG STDMETHODCALLTYPE ControllerCreatedHandler_Release(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This);
static HRESULT STDMETHODCALLTYPE ControllerCreatedHandler_Invoke(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This,
    HRESULT errorCode, ICoreWebView2Controller* result);

static ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl controllerVtbl = {
  ControllerCreatedHandler_QueryInterface,
  ControllerCreatedHandler_AddRef,
  ControllerCreatedHandler_Release,
  ControllerCreatedHandler_Invoke
};

typedef struct {
  ICoreWebView2CreateCoreWebView2ControllerCompletedHandler handler;
  WebViewInstance* inst;
} ControllerCompletedHandler;

/* Controller completion handler implementation */
static HRESULT STDMETHODCALLTYPE ControllerCreatedHandler_QueryInterface(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This,
    REFIID riid, void** ppvObject) {
  if (!ppvObject) return E_POINTER;
  *ppvObject = NULL;
  if (IsEqualIID(riid, &IID_IUnknown) ||
      IsEqualIID(riid, &IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
    *ppvObject = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
  }
  return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ControllerCreatedHandler_AddRef(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This) {
  (void)This;
  return 1;
}

static ULONG STDMETHODCALLTYPE ControllerCreatedHandler_Release(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This) {
  (void)This;
  return 1;
}

static HRESULT STDMETHODCALLTYPE ControllerCreatedHandler_Invoke(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* This,
    HRESULT errorCode, ICoreWebView2Controller* result) {
  ControllerCompletedHandler* handler = (ControllerCompletedHandler*)This;
  WebViewInstance* inst = handler->inst;

  if (FAILED(errorCode) || !result) {
    fprintf(stderr, "WebView2: Failed to create controller: 0x%08lx\n", errorCode);
    return E_FAIL;
  }

  fprintf(stderr, "WebView2: Controller created successfully\n");

  inst->controller = result;
  ICoreWebView2Controller_AddRef(result);

  /* Get webview interface */
  ICoreWebView2Controller_get_CoreWebView2(result, &inst->webview);
  inst->ready = 1;

  /* Set bounds */
  RECT bounds;
  GetClientRect(inst->hwnd, &bounds);
  fprintf(stderr, "WebView: Setting bounds: %ldx%ld\n", bounds.right - bounds.left, bounds.bottom - bounds.top);
  ICoreWebView2Controller_put_Bounds(result, bounds);

  /* Force repaint */
  InvalidateRect(inst->hwnd, NULL, TRUE);

  /* Navigate to URL */
  if (inst->url) {
    fprintf(stderr, "WebView: Navigating to URL: %s\n", inst->url);
  }
  navigate_to_url(inst);

  /* Force repaint after navigation */
  InvalidateRect(inst->hwnd, NULL, TRUE);
  UpdateWindow(inst->hwnd);

  /* Show window before opening devtools */
  if (inst->show) {
    fprintf(stderr, "WebView: Showing window...\n");
    ShowWindow(inst->hwnd, SW_SHOW);
    UpdateWindow(inst->hwnd);
    /* Ensure the controller is visible after the parent window is shown */
    ICoreWebView2Controller_put_IsVisible(result, TRUE);
    /* Force full redraw */
    RedrawWindow(inst->hwnd, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN);
    fprintf(stderr, "WebView: Window shown\n");
  }

  /* Open devtools if requested */
  if (inst->devTools && inst->webview) {
    ICoreWebView2Settings* settings = NULL;
    HRESULT hrSettings = ICoreWebView2_get_Settings(inst->webview, &settings);
    if (SUCCEEDED(hrSettings) && settings) {
      ICoreWebView2Settings_put_AreDevToolsEnabled(settings, TRUE);
      ICoreWebView2Settings_put_IsWebMessageEnabled(settings, TRUE);
      ICoreWebView2Settings_Release(settings);
      fprintf(stderr, "WebView: DevTools enabled\n");
    }
    /* Open devtools window */
    HRESULT hrDevTools = ICoreWebView2_OpenDevToolsWindow(inst->webview);
    if (SUCCEEDED(hrDevTools)) {
      fprintf(stderr, "WebView: DevTools window opened\n");
    } else {
      fprintf(stderr, "WebView: Failed to open DevTools: 0x%08lx\n", hrDevTools);
    }
  }

  /* Register COM event handlers */
  {
    NavCompletedHandler* nc = (NavCompletedHandler*)malloc(sizeof(NavCompletedHandler));
    if (nc) {
      nc->handler.lpVtbl = &navCompletedVtbl;
      nc->inst = inst;
      ICoreWebView2_add_NavigationCompleted(inst->webview, &nc->handler, &inst->token_nav_completed);
    }

    SourceChangedHandler* sc = (SourceChangedHandler*)malloc(sizeof(SourceChangedHandler));
    if (sc) {
      sc->handler.lpVtbl = &sourceChangedVtbl;
      sc->inst = inst;
      ICoreWebView2_add_SourceChanged(inst->webview, &sc->handler, &inst->token_source_changed);
    }

    WebMessageReceivedHandler* wm = (WebMessageReceivedHandler*)malloc(sizeof(WebMessageReceivedHandler));
    if (wm) {
      wm->handler.lpVtbl = &webMsgVtbl;
      wm->inst = inst;
      ICoreWebView2_add_WebMessageReceived(inst->webview, &wm->handler, &inst->token_web_message);
    }

    TitleChangedHandler* tc = (TitleChangedHandler*)malloc(sizeof(TitleChangedHandler));
    if (tc) {
      tc->handler.lpVtbl = &titleChangedVtbl;
      tc->inst = inst;
      ICoreWebView2_add_DocumentTitleChanged(inst->webview, &tc->handler, &inst->token_title_changed);
    }
  }

  /* Re-inject any existing interface scripts into current page */
  if (inst->interfaces && inst->webview) {
    for (size_t i = 0; i < inst->interfaces->capacity; i++) {
      if (inst->interfaces->entries[i].occupied) {
        Value val = inst->interfaces->entries[i].value;
        if (val.tag == TAG_STRING && val.as.string && val.as.string->data) {
          wchar_t* wcode = to_wide(val.as.string->data);
          if (wcode) {
            ICoreWebView2_ExecuteScript(inst->webview, wcode, NULL);
            free(wcode);
          }
        }
      }
    }
  }

  /* Emit ready event */
  webview_emit(inst, "ready", NULL, 0);

  return S_OK;
}

/* Environment completion handler vtable - forward declarations */
static HRESULT STDMETHODCALLTYPE EnvCompletedHandler_QueryInterface(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This,
    REFIID riid, void** ppvObject);
static ULONG STDMETHODCALLTYPE EnvCompletedHandler_AddRef(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This);
static ULONG STDMETHODCALLTYPE EnvCompletedHandler_Release(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This);
static HRESULT STDMETHODCALLTYPE EnvCompletedHandler_Invoke(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This,
    HRESULT errorCode, ICoreWebView2Environment* environment);

static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl envVtbl = {
  EnvCompletedHandler_QueryInterface,
  EnvCompletedHandler_AddRef,
  EnvCompletedHandler_Release,
  EnvCompletedHandler_Invoke
};

typedef struct {
  ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler handler;
  WebViewInstance* inst;
} EnvCompletedHandler;

/* Environment completion handler implementation */
static HRESULT STDMETHODCALLTYPE EnvCompletedHandler_QueryInterface(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This,
    REFIID riid, void** ppvObject) {
  if (!ppvObject) return E_POINTER;
  *ppvObject = NULL;
  if (IsEqualIID(riid, &IID_IUnknown) ||
      IsEqualIID(riid, &IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
    *ppvObject = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
  }
  return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE EnvCompletedHandler_AddRef(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This) {
  (void)This;
  return 1;
}

static ULONG STDMETHODCALLTYPE EnvCompletedHandler_Release(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This) {
  (void)This;
  return 1;
}

static HRESULT STDMETHODCALLTYPE EnvCompletedHandler_Invoke(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* This,
    HRESULT errorCode, ICoreWebView2Environment* environment) {
  EnvCompletedHandler* handler = (EnvCompletedHandler*)This;
  WebViewInstance* inst = handler->inst;

  if (FAILED(errorCode) || !environment) {
    fprintf(stderr, "WebView2: Failed to create environment: 0x%08lx\n", errorCode);
    free(handler);
    return E_FAIL;
  }

  fprintf(stderr, "WebView2: Environment created successfully\n");

  /* Allocate controller handler on heap so it survives until callback */
  ControllerCompletedHandler* controllerHandler = (ControllerCompletedHandler*)malloc(sizeof(ControllerCompletedHandler));
  if (!controllerHandler) {
    fprintf(stderr, "WebView: Failed to allocate controller handler\n");
    free(handler);
    return E_FAIL;
  }
  controllerHandler->handler.lpVtbl = &controllerVtbl;
  controllerHandler->inst = inst;

  /* Create controller */
  ICoreWebView2Environment_CreateCoreWebView2Controller(
    environment, inst->hwnd,
    &controllerHandler->handler);

  /* Free env handler (no longer needed after queuing controller creation) */
  free(handler);

  return S_OK;
}

Value node_webview_isAvailable(void) {
#ifdef _WIN32
  LPWSTR version = NULL;
  HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(NULL, &version);
  if (version) CoTaskMemFree(version);
  return ts_value_boolean(SUCCEEDED(hr));
#else
  return ts_value_boolean(0);
#endif
}

Value node_webview_WebView(Value options) {
  printf("WebView: node_webview_WebView called\n");
  fflush(stdout);
  fprintf(stderr, "WebView: node_webview_WebView called (stderr)\n");
  WebViewInstance* inst = (WebViewInstance*)calloc(1, sizeof(WebViewInstance));
  if (!inst) {
    fprintf(stderr, "WebView: Failed to allocate instance\n");
    return ts_value_undefined();
  }

  /* Defaults */
  inst->width = 800;
  inst->height = 600;
  inst->show = 1;
  inst->center = 1;
  inst->frame = 1;        /* default: show frame */
  inst->transparent = 0;  /* default: opaque */
  inst->devTools = 1;     /* default: open devtools */

  /* Parse options */
  if (options.tag == TAG_OBJECT && options.as.object) {
    TSHashMap* map = (TSHashMap*)options.as.object;
    Value v;

    v = ts_hashmap_get(map, ts_string_new("url"));
    if (v.tag == TAG_STRING && v.as.string && v.as.string->data) {
      inst->url = strdup(v.as.string->data);
    }

    v = ts_hashmap_get(map, ts_string_new("width"));
    if (v.tag == TAG_NUMBER) inst->width = (int)v.as.number;

    v = ts_hashmap_get(map, ts_string_new("height"));
    if (v.tag == TAG_NUMBER) inst->height = (int)v.as.number;

    v = ts_hashmap_get(map, ts_string_new("title"));
    if (v.tag == TAG_STRING && v.as.string && v.as.string->data) {
      inst->title = strdup(v.as.string->data);
    }

    v = ts_hashmap_get(map, ts_string_new("icon"));
    if (v.tag == TAG_STRING && v.as.string && v.as.string->data) {
      inst->icon = strdup(v.as.string->data);
    }

    v = ts_hashmap_get(map, ts_string_new("show"));
    inst->show = ts_to_boolean(v);

    v = ts_hashmap_get(map, ts_string_new("center"));
    inst->center = ts_to_boolean(v);

    v = ts_hashmap_get(map, ts_string_new("frame"));
    inst->frame = ts_to_boolean(v);

    v = ts_hashmap_get(map, ts_string_new("transparent"));
    inst->transparent = ts_to_boolean(v);

    v = ts_hashmap_get(map, ts_string_new("devTools"));
    inst->devTools = ts_to_boolean(v);
  }

  /* Register window class */
  fprintf(stderr, "WebView: Registering window class...\n");
  register_class();

  /* Create window */
  fprintf(stderr, "WebView: Creating window %dx%d, frame=%d, transparent=%d\n",
          inst->width, inst->height, inst->frame, inst->transparent);
  int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
  if (inst->center) {
    x = (GetSystemMetrics(SM_CXSCREEN) - inst->width) / 2;
    y = (GetSystemMetrics(SM_CYSCREEN) - inst->height) / 2;
  }

  /* Set window style based on frame option */
  DWORD dwStyle = (inst->frame ? WS_OVERLAPPEDWINDOW : WS_POPUP) | WS_CLIPCHILDREN;
  DWORD dwExStyle = 0;

  /* Note: transparent option is reserved for future use */

  wchar_t* wtitle = to_wide(inst->title ? inst->title : "WebView");

  inst->hwnd = CreateWindowExW(
    dwExStyle, CLASS_NAME, wtitle ? wtitle : L"WebView",
    dwStyle,
    x, y, inst->width, inst->height,
    NULL, NULL, GetModuleHandle(NULL), inst);

  if (wtitle) free(wtitle);

  if (!inst->hwnd) {
    fprintf(stderr, "WebView: Failed to create window\n");
    free(inst->url);
    free(inst->title);
    free(inst->icon);
    free(inst);
    return ts_value_undefined();
  }

  fprintf(stderr, "WebView: Window created successfully, hwnd=%p\n", inst->hwnd);

  /* Set window icon if provided */
  if (inst->icon) {
    wchar_t* wicon = to_wide(inst->icon);
    if (wicon) {
      HICON hIcon = (HICON)LoadImageW(NULL, wicon, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
      if (!hIcon) {
        /* Try relative to executable directory */
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        /* Find last backslash and replace with icon filename */
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) {
          *(lastSlash + 1) = L'\0';
          wcscat_s(exePath, MAX_PATH, wicon);
          hIcon = (HICON)LoadImageW(NULL, exePath, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
          if (hIcon) {
            fprintf(stderr, "WebView: Loaded icon from exe dir: %ls\n", exePath);
          }
        }
      }
      if (hIcon) {
        SendMessage(inst->hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessage(inst->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        fprintf(stderr, "WebView: Set window icon: %s\n", inst->icon);
      } else {
        fprintf(stderr, "WebView: Failed to load icon: %s\n", inst->icon);
      }
      free(wicon);
    }
  }

  /* Note: transparent option is reserved for future use with WebView2 background transparency */

  /* Initialize WebView2 */
  fprintf(stderr, "WebView: Initializing WebView2...\n");

  /* Initialize COM as Single-Threaded Apartment for UI */
  HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    fprintf(stderr, "WebView2: Failed to initialize COM: 0x%08lx\n", hr);
    DestroyWindow(inst->hwnd);
    free(inst->url);
    free(inst->title);
    free(inst->icon);
    free(inst);
    return ts_value_undefined();
  }

  /* Allocate environment handler on heap so it survives until callback */
  EnvCompletedHandler* envHandler = (EnvCompletedHandler*)malloc(sizeof(EnvCompletedHandler));
  if (!envHandler) {
    fprintf(stderr, "WebView: Failed to allocate env handler\n");
    DestroyWindow(inst->hwnd);
    free(inst->url);
    free(inst->title);
    free(inst->icon);
    free(inst);
    return ts_value_undefined();
  }
  envHandler->handler.lpVtbl = &envVtbl;
  envHandler->inst = inst;

  hr = CreateCoreWebView2EnvironmentWithOptions(
    NULL, NULL, NULL,
    &envHandler->handler);

  if (FAILED(hr)) {
    fprintf(stderr, "WebView2: Failed to create environment: 0x%08lx\n", hr);
    DestroyWindow(inst->hwnd);
    free(inst->url);
    free(inst->title);
    free(inst->icon);
    free(inst);
    return ts_value_undefined();
  }

  fprintf(stderr, "WebView2: Environment creation started\n");
  return ts_value_object((void*)inst);
}

Value node_webview_loadURL(Value self, Value url) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst) return ts_value_undefined();
  free(inst->url);
  inst->url = NULL;
  if (url.tag == TAG_STRING && url.as.string && url.as.string->data) {
    inst->url = strdup(url.as.string->data);
  }
  navigate_to_url(inst);
  return ts_value_undefined();
}

Value node_webview_navigate(Value self, Value url) {
  return node_webview_loadURL(self, url);
}

Value node_webview_loadHTML(Value self, Value html) {
  (void)self; (void)html;
  /* TODO: Implement NavigateToString */
  return ts_value_undefined();
}

Value node_webview_evaluate(Value self, Value script) {
  (void)self; (void)script;
  /* TODO: Implement ExecuteScript */
  return ts_value_undefined();
}

Value node_webview_executeJavaScript(Value self, Value script) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->webview) return ts_value_undefined();
  if (script.tag != TAG_STRING || !script.as.string || !script.as.string->data) {
    return ts_value_undefined();
  }
  wchar_t* wscript = to_wide(script.as.string->data);
  if (wscript) {
    ICoreWebView2_ExecuteScript(inst->webview, wscript, NULL);
    free(wscript);
  }
  return ts_value_undefined();
}

Value node_webview_setTitle(Value self, Value title) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  free(inst->title);
  inst->title = NULL;
  if (title.tag == TAG_STRING && title.as.string && title.as.string->data) {
    inst->title = strdup(title.as.string->data);
    wchar_t* wtitle = to_wide(inst->title);
    if (wtitle) {
      SetWindowTextW(inst->hwnd, wtitle);
      free(wtitle);
    }
  }
  return ts_value_undefined();
}

Value node_webview_setSize(Value self, Value width, Value height) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  int w = (width.tag == TAG_NUMBER) ? (int)width.as.number : inst->width;
  int h = (height.tag == TAG_NUMBER) ? (int)height.as.number : inst->height;
  inst->width = w;
  inst->height = h;
  SetWindowPos(inst->hwnd, NULL, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
  return ts_value_undefined();
}

Value node_webview_setIcon(Value self, Value iconPath) {
  (void)self; (void)iconPath;
  /* TODO: Implement icon setting */
  return ts_value_undefined();
}

Value node_webview_setPosition(Value self, Value x, Value y) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  int px = (x.tag == TAG_NUMBER) ? (int)x.as.number : 0;
  int py = (y.tag == TAG_NUMBER) ? (int)y.as.number : 0;
  SetWindowPos(inst->hwnd, NULL, px, py, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
  return ts_value_undefined();
}

Value node_webview_center(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  int x = (GetSystemMetrics(SM_CXSCREEN) - inst->width) / 2;
  int y = (GetSystemMetrics(SM_CYSCREEN) - inst->height) / 2;
  SetWindowPos(inst->hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
  return ts_value_undefined();
}

Value node_webview_show(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  ShowWindow(inst->hwnd, SW_SHOW);
  UpdateWindow(inst->hwnd);
  return ts_value_undefined();
}

Value node_webview_hide(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  ShowWindow(inst->hwnd, SW_HIDE);
  return ts_value_undefined();
}

Value node_webview_focus(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  SetForegroundWindow(inst->hwnd);
  SetFocus(inst->hwnd);
  return ts_value_undefined();
}

Value node_webview_minimize(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  ShowWindow(inst->hwnd, SW_MINIMIZE);
  return ts_value_undefined();
}

Value node_webview_maximize(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  ShowWindow(inst->hwnd, SW_MAXIMIZE);
  return ts_value_undefined();
}

Value node_webview_unmaximize(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();
  ShowWindow(inst->hwnd, SW_RESTORE);
  return ts_value_undefined();
}

Value node_webview_close(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) return ts_value_undefined();

  if (inst->webview) {
    ICoreWebView2_remove_NavigationCompleted(inst->webview, inst->token_nav_completed);
    ICoreWebView2_remove_SourceChanged(inst->webview, inst->token_source_changed);
    ICoreWebView2_remove_WebMessageReceived(inst->webview, inst->token_web_message);
    ICoreWebView2_remove_DocumentTitleChanged(inst->webview, inst->token_title_changed);
  }

  if (inst->controller) {
    ICoreWebView2Controller_Close(inst->controller);
  }
  DestroyWindow(inst->hwnd);

  /* Shutdown WebSocket bridge */
  bridge_shutdown(inst);

  /* Cleanup C strings (but leave inst itself — runtime/GC owns the object pointer) */
  free(inst->url);
  free(inst->title);
  free(inst->icon);
  return ts_value_undefined();
}

Value node_webview_run(Value self) {
  fprintf(stderr, "WebView: node_webview_run called, self.tag=%d, self.as.object=%p\n", self.tag, self.as.object);
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->hwnd) {
    fprintf(stderr, "WebView: node_webview_run returning early, inst=%p, hwnd=%p\n", inst, inst ? inst->hwnd : NULL);
    return ts_value_undefined();
  }

  fprintf(stderr, "WebView: Starting message loop, hwnd=%p\n", inst->hwnd);

  MSG msg;
  int running = 1;
  while (running) {
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        running = 0;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    if (!running) break;

    /* Poll WebSocket bridge for async interface messages */
    if (inst && inst->bridge) {
      bridge_poll(inst);
    }

    Sleep(5);
  }

  fprintf(stderr, "WebView: Message loop ended\n");
  return ts_value_undefined();
}

Value node_webview_on(Value self, Value event, Value callback) {
  if (self.tag != TAG_OBJECT || !self.as.object) return self;
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst) return self;

  if (!inst->listeners) {
    inst->listeners = ts_hashmap_new();
  }

  TSString* evName = ts_to_string(event);
  if (!evName) return self;

  Value arr = ts_hashmap_get(inst->listeners, evName);
  if (arr.tag != TAG_ARRAY || !arr.as.array) {
    arr = ts_value_array(ts_array_new());
    ts_hashmap_set(inst->listeners, evName, arr);
  }
  ts_array_push(arr.as.array, callback);
  return self;
}

Value node_webview_once(Value self, Value event, Value callback) {
  /* For simplicity, same as on (full implementation would track once flag) */
  return node_webview_on(self, event, callback);
}

Value node_webview_off(Value self, Value event, Value callback) {
  if (self.tag != TAG_OBJECT || !self.as.object) return self;
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->listeners) return self;

  TSString* evName = ts_to_string(event);
  if (!evName) return self;

  Value arr = ts_hashmap_get(inst->listeners, evName);
  if (arr.tag != TAG_ARRAY || !arr.as.array) return self;

  TSArray* old = arr.as.array;
  TSArray* neu = ts_array_new();
  int removed = 0;
  for (int i = 0; i < old->length; i++) {
    Value fn = ts_array_get(old, i);
    if (!removed && fn.tag == callback.tag && fn.as.function == callback.as.function) {
      removed = 1;
      continue;
    }
    ts_array_push(neu, fn);
  }
  ts_hashmap_set(inst->listeners, evName, ts_value_array(neu));
  return self;
}

Value node_webview_get_ready(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst) return ts_value_boolean(0);
  return ts_value_boolean(inst->ready);
}

Value node_webview_get_url(Value self) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst || !inst->url) return ts_value_string(ts_string_new(""));
  return ts_value_string(ts_string_new(inst->url));
}

/* helper: dynamic string builder */
typedef struct {
  char* buf;
  size_t cap;
  size_t len;
} StrBuilder;

static void sb_init(StrBuilder* sb) {
  sb->cap = 256;
  sb->buf = (char*)malloc(sb->cap);
  sb->len = 0;
  if (sb->buf) sb->buf[0] = '\0';
}

static void sb_append(StrBuilder* sb, const char* text) {
  if (!sb->buf) return;
  size_t tlen = strlen(text);
  while (sb->len + tlen + 1 > sb->cap) {
    sb->cap = sb->cap * 2 + tlen + 1;
    sb->buf = (char*)realloc(sb->buf, sb->cap);
  }
  memcpy(sb->buf + sb->len, text, tlen);
  sb->len += tlen;
  sb->buf[sb->len] = '\0';
}

static char* sb_take(StrBuilder* sb) {
  char* result = sb->buf;
  sb->buf = NULL;
  sb->cap = 0;
  sb->len = 0;
  return result;
}

static char* build_interface_script(const char* name, TSHashMap* methods, int port) {
  StrBuilder sb;
  sb_init(&sb);
  if (!sb.buf) return NULL;

  sb_append(&sb, "(function() {\n");
  sb_append(&sb, "  var NAME = \""); sb_append(&sb, name); sb_append(&sb, "\";\n");
  sb_append(&sb, "  var PORT = "); {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    sb_append(&sb, port_str);
  }
  sb_append(&sb, ";\n");
  sb_append(&sb, "  var G = window.__mjbBr;\n");
  sb_append(&sb, "  if (!G) { G = {pending:{}, queue:[], port:PORT}; window.__mjbBr = G; }\n");
  sb_append(&sb, "  function gid() { return Math.random().toString(36).substring(2,11)+Date.now().toString(36); }\n");
  sb_append(&sb, "  function flush() { while(G.queue.length && G.ws && G.ws.readyState===1){ G.ws.send(G.queue.shift()); } }\n");
  sb_append(&sb, "  function onmsg(ev){var d=JSON.parse(ev.data);if(d.__id&&G.pending[d.__id]){if(d.__err){G.pending[d.__id].rej(new Error(d.__err));}else{G.pending[d.__id].res(d.__res);}delete G.pending[d.__id];}}\n");
  sb_append(&sb, "  function conn(){\n");
  sb_append(&sb, "    if (G.ws && (G.ws.readyState===0||G.ws.readyState===1)) return;\n");
  sb_append(&sb, "    if (G.ws){ try{ G.ws.close(); }catch(e){} }\n");
  sb_append(&sb, "    G.ws = new WebSocket('ws://127.0.0.1:'+G.port);\n");
  sb_append(&sb, "    G.ws.onopen = function(){ flush(); };\n");
  sb_append(&sb, "    G.ws.onmessage = onmsg;\n");
  sb_append(&sb, "    G.ws.onclose = function(){ setTimeout(conn, 500); };\n");
  sb_append(&sb, "    G.ws.onerror = function(){ if(G.ws){ try{G.ws.close();}catch(e){} } };\n");
  sb_append(&sb, "  }\n");
  sb_append(&sb, "  conn();\n");
  sb_append(&sb, "  G.gid = gid;\n");
  sb_append(&sb, "  window[NAME] = window[NAME] || {};\n");

  for (int32_t i = 0; i < methods->capacity; i++) {
    if (!methods->entries[i].occupied) continue;
    TSString* key = methods->entries[i].key;
    sb_append(&sb, "  window[NAME][\"");
    sb_append(&sb, key->data);
    sb_append(&sb, "\"] = function() {\n");
    sb_append(&sb, "    var args = Array.prototype.slice.call(arguments);\n");
    sb_append(&sb, "    return new Promise(function(res, rej) {\n");
    sb_append(&sb, "      var id = G.gid();\n");
    sb_append(&sb, "      var msg = JSON.stringify({__if:NAME,__m:\"");
    sb_append(&sb, key->data);
    sb_append(&sb, "\",__a:args,__id:id});\n");
    sb_append(&sb, "      G.pending[id] = {res:res, rej:rej};\n");
    sb_append(&sb, "      if (!G.ws || G.ws.readyState !== 1) conn();\n");
    sb_append(&sb, "      if (G.ws && G.ws.readyState === 1) G.ws.send(msg);\n");
    sb_append(&sb, "      else G.queue.push(msg);\n");
    sb_append(&sb, "      setTimeout(function(){ if(G.pending[id]){ delete G.pending[id]; rej(new Error('Timeout')); } }, 30000);\n");
    sb_append(&sb, "    });\n");
    sb_append(&sb, "  };\n");
  }

  sb_append(&sb, "})();\n");
  return sb_take(&sb);
}

Value node_webview_addJavaScriptInterface(Value self, Value name, Value methods) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst) return ts_value_undefined();
  if (name.tag != TAG_STRING || !name.as.string || !name.as.string->data) return ts_value_undefined();
  if (methods.tag != TAG_OBJECT || !methods.as.object) return ts_value_undefined();

  TSHashMap* methodsMap = (TSHashMap*)methods.as.object;
  const char* ifName = name.as.string->data;

  /* Initialize WebSocket bridge */
  int port = bridge_init(inst);
  if (port < 0) {
    fprintf(stderr, "WebView: Failed to initialize bridge\n");
    return ts_value_undefined();
  }

  /* Store callbacks */
  if (!inst->interfaceMethods) {
    inst->interfaceMethods = ts_hashmap_new();
  }
  for (int32_t i = 0; i < methodsMap->capacity; i++) {
    if (!methodsMap->entries[i].occupied) continue;
    TSString* key = methodsMap->entries[i].key;
    Value fn = methodsMap->entries[i].value;
    if (fn.tag != TAG_FUNCTION && !(fn.tag == TAG_OBJECT && fn.as.object && *(int32_t*)fn.as.object == BOUND_FN_TAG)) continue;
    size_t keyLen = strlen(ifName) + 1 + strlen(key->data) + 1;
    char* compositeKey = (char*)malloc(keyLen);
    snprintf(compositeKey, keyLen, "%s.%s", ifName, key->data);
    ts_hashmap_set(inst->interfaceMethods, ts_string_new(compositeKey), fn);
    free(compositeKey);
  }

  /* Build JS shim and store for re-injection on every navigation */
  char* jsCode = build_interface_script(ifName, methodsMap, port);
  if (!jsCode) return ts_value_undefined();

  if (!inst->interfaces) {
    inst->interfaces = ts_hashmap_new();
  }
  TSString* keyStr = ts_string_new(ifName);
  /* Remove old script string if present */
  Value oldVal = ts_hashmap_get(inst->interfaces, keyStr);
  if (oldVal.tag == TAG_STRING && oldVal.as.string) {
    ts_string_free(oldVal.as.string);
  }
  Value scriptVal;
  scriptVal.tag = TAG_STRING;
  scriptVal.as.string = ts_string_new(jsCode);
  ts_hashmap_set(inst->interfaces, keyStr, scriptVal);
  ts_string_free(keyStr);

  /* If webview already ready, inject immediately into current page */
  if (inst->webview) {
    wchar_t* wcode = to_wide(jsCode);
    if (wcode) {
      ICoreWebView2_ExecuteScript(inst->webview, wcode, NULL);
      free(wcode);
    }
  }

  free(jsCode);
  return ts_value_undefined();
}

Value node_webview_removeJavaScriptInterface(Value self, Value name) {
  WebViewInstance* inst = (WebViewInstance*)self.as.object;
  if (!inst) return ts_value_undefined();
  if (name.tag != TAG_STRING || !name.as.string || !name.as.string->data) return ts_value_undefined();

  /* Remove stored script string */
  if (inst->interfaces) {
    Value idVal = ts_hashmap_get(inst->interfaces, name.as.string);
    if (idVal.tag == TAG_STRING && idVal.as.string) {
      ts_string_free(idVal.as.string);
    }
    ts_hashmap_set(inst->interfaces, name.as.string, ts_value_undefined());
  }

  /* Remove callbacks */
  if (inst->interfaceMethods) {
    const char* prefix = name.as.string->data;
    size_t prefixLen = strlen(prefix);
    TSHashMap* newMap = ts_hashmap_new();
    for (int32_t i = 0; i < inst->interfaceMethods->capacity; i++) {
      if (!inst->interfaceMethods->entries[i].occupied) continue;
      TSString* key = inst->interfaceMethods->entries[i].key;
      if (strlen(key->data) <= prefixLen || strncmp(key->data, prefix, prefixLen) != 0 || key->data[prefixLen] != '.') {
        ts_hashmap_set(newMap, key, inst->interfaceMethods->entries[i].value);
      }
    }
    /* We intentionally leak the old hashmap and just swap pointer; GC will handle orphans */
    inst->interfaceMethods = newMap;
  }

  return ts_value_undefined();
}
