/* 7zAesDec.c -- 7zAES (AES-256-CBC + SHA-256 KDF) decoder for the
 * vendored 7-Zip decode-only subset.
 *
 * The upstream low-level decoder (7zDec.c) supports only Copy/LZMA/LZMA2/PPMD
 * and has no password plumbing: the real password path lives in the C++ Archive
 * layer, which is not vendored. This file supplies the missing 7zAES coder so
 * encrypted archives can be decoded.
 *
 * 7zAES algorithm (interoperable with 7-Zip / p7zip / py7zr):
 *   - coder method id 0x06F10701
 *   - the packed stream is AES-256-CBC encrypted, size a multiple of 16 bytes
 *   - coder properties: 2 + saltSize + ivSize bytes:
 *       b0: numCyclesPower = b0 & 0x3F, bit6 = ivPresent, bit7 = saltPresent
 *       b1: ivSize   = (b1 & 0x0F) + 1  (when iv present)
 *           saltSize = (b1 >> 4) + 1    (when salt present)
 *     followed by salt bytes then iv bytes
 *   - key = KDF(password, numCyclesPower, salt, SHA-256):
 *       rounds = 1 << numCyclesPower
 *       digest = SHA256( salt || password_utf16le || round_le_u64 ) over `rounds`
 *       key = digest[0..31]
 *     special case numCyclesPower == 0x3F: key = (salt || password || zeros32)[0..31]
 *   - iv is zero-padded to 16 bytes
 */

#include "7zAesDec.h"

#include "Aes.h"
#include "Sha256.h"
#include "7zTypes.h"

#include <string.h>
#include <stdlib.h>

#define AES_DEC_BLOCK 16
#define AES_DEC_KEY_SIZE 32

/* One-time init flags for the AES tables and SHA-256 dispatch. */
static int g_aes_tables_ready = 0;
static int g_sha256_ready = 0;

static void aes_dec_ensure_init(void) {
  if (!g_aes_tables_ready) {
    AesGenTables();
    g_aes_tables_ready = 1;
  }
  if (!g_sha256_ready) {
    Sha256Prepare();
    g_sha256_ready = 1;
  }
}

/* Derive the 32-byte AES key from a UTF-16LE password, salt and cycle power. */
static void aes_dec_derive_key(const Byte* password16, size_t password16_len,
                               unsigned num_cycles_power, const Byte* salt,
                               size_t salt_size, Byte key[32]) {
  Byte counter[8];

  if (num_cycles_power == 0x3F) {
    /* Degenerate case: key = (salt || password || zeros(32))[0..31], NO hashing. */
    Byte buf[32];
    size_t o = 0;
    size_t i;
    memset(buf, 0, sizeof(buf));
    for (i = 0; i < salt_size && o < sizeof(buf); i++) buf[o++] = salt[i];
    for (i = 0; i < password16_len && o < sizeof(buf); i++) buf[o++] = password16[i];
    memcpy(key, buf, 32);
    return;
  } else {
    UInt64 rounds = (UInt64)1 << num_cycles_power;
    UInt64 i;
    CSha256 sha;
    Sha256_Init(&sha);
    for (i = 0; i < rounds; i++) {
      unsigned j;
      Sha256_Update(&sha, salt, salt_size);
      Sha256_Update(&sha, password16, password16_len);
      for (j = 0; j < 8; j++) counter[j] = (Byte)(i >> (8 * j));
      Sha256_Update(&sha, counter, 8);
    }
    Sha256_Final(&sha, key);
  }
}

/* UTF-8 → UTF-16LE (no BOM), malloc'd. Caller frees. Returns NULL on OOM.
 * Mirrors the encoding the 7-Zip / py7zr key derivation expects. */
static Byte* aes_dec_utf8_to_utf16le(const char* utf8, size_t* out_len) {
  size_t n = strlen(utf8);
  Byte* out = (Byte*)malloc((n + 1) * 2);
  size_t o = 0;
  size_t i = 0;
  if (!out) return NULL;
  while (i < n) {
    UInt32 cp;
    unsigned char c = (unsigned char)utf8[i];
    size_t extra;
    if (c < 0x80) { cp = c; extra = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else { cp = 0xFFFD; extra = 0; } /* invalid byte → replacement char */
    i++;
    for (size_t k = 0; k < extra && i < n; k++, i++) {
      cp = (cp << 6) | ((unsigned char)utf8[i] & 0x3F);
    }
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out[o++] = (Byte)(0xD8 | (cp >> 18));
      out[o++] = (Byte)((cp >> 10) & 0xFF);
      out[o++] = (Byte)(0xDC | ((cp >> 8) & 0x03));
      out[o++] = (Byte)(cp & 0xFF);
    } else {
      out[o++] = (Byte)(cp & 0xFF);
      out[o++] = (Byte)((cp >> 8) & 0xFF);
    }
  }
  if (out_len) *out_len = o;
  return out;
}

/* Parse 7zAES coder properties into cycles/salt/iv. Returns 0 on success. */
static int aes_dec_parse_props(const Byte* props, size_t props_size,
                               unsigned* num_cycles_power, const Byte** salt,
                               size_t* salt_size, Byte iv[16]) {
  Byte first, second;
  size_t iv_size;
  if (!props || props_size < 2) return -1;

  first = props[0];
  *num_cycles_power = first & 0x3F;
  if ((first & 0xC0) == 0) return -1; /* no salt/iv bits → malformed */

  second = props[1];
  /* Per 7-Zip: size = (nibble + 1) when the presence bit is set, else 0. */
  if (first & 0x80)
    *salt_size = (size_t)(second >> 4) + 1;
  else
    *salt_size = 0;
  if (first & 0x40)
    iv_size = (size_t)(second & 0x0F) + 1;
  else
    iv_size = 0;

  if (props_size < 2 + *salt_size + iv_size) return -1;

  *salt = props + 2;
  memset(iv, 0, 16);
  if (iv_size > 0) {
    if (iv_size > 16) return -1;
    memcpy(iv, props + 2 + *salt_size, iv_size);
  }
  return 0;
}

int z7_aes_decode(const Byte* props, size_t props_size, const char* password,
                  Byte* data, size_t data_size) {
  unsigned num_cycles_power;
  const Byte* salt;
  size_t salt_size;
  Byte iv[16];
  Byte key[AES_DEC_KEY_SIZE];
  Byte* password16;
  size_t password16_len = 0;
  UInt32 ivAes[AES_NUM_IVMRK_WORDS];

  if (!password || !password[0]) return Z7_AES_ERR_NO_PASSWORD;
  if (data_size == 0) return Z7_AES_OK;
  if ((data_size & (AES_DEC_BLOCK - 1)) != 0) return Z7_AES_ERR_DATA;

  if (aes_dec_parse_props(props, props_size, &num_cycles_power, &salt, &salt_size, iv) != 0)
    return Z7_AES_ERR_UNSUPPORTED;

  password16 = aes_dec_utf8_to_utf16le(password, &password16_len);
  if (!password16) return Z7_AES_ERR_MEM;

  aes_dec_ensure_init();
  aes_dec_derive_key(password16, password16_len, num_cycles_power, salt, salt_size, key);
  memset(password16, 0, password16_len);
  free(password16);

  /* ivAes: [0..3] = IV/state, [4..] = AES-256 round keys. */
  AesCbc_Init(ivAes, iv);
  Aes_SetKey_Dec(ivAes + 4, key, AES_DEC_KEY_SIZE);
  g_AesCbc_Decode(ivAes, data, data_size / AES_DEC_BLOCK);

  memset(key, 0, sizeof(key));
  memset(ivAes, 0, sizeof(ivAes));
  return Z7_AES_OK;
}

int z7_aes_is_aes_method(UInt32 method_id) {
  return method_id == Z7_AES_METHOD_ID;
}