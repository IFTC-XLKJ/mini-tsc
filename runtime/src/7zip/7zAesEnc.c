/* 7zAesEnc.c -- 7zAES (AES-256-CBC + SHA-256 KDF) encoder for the
 * vendored 7-Zip subset. Symmetric counterpart of 7zAesDec.c.
 *
 * 7zAES coder properties (writer side), matching 7-Zip / p7zip / py7zr:
 *   props[0] = numCyclesPower | 0x80 (salt present) | 0x40 (iv present)
 *   props[1] = (saltSize-1) << 4 | (ivSize-1)
 *   followed by `saltSize` random salt bytes, then `ivSize` random iv bytes.
 *
 *   key = KDF(password_utf16le, numCyclesPower, salt, SHA-256)
 *   out = AES-256-CBC(key, iv, plaintext)
 *
 * The 7zAES coder's declared unpack size is the ORIGINAL plaintext length, so
 * the caller must pad the packed buffer to a multiple of 16 before calling
 * (zero padding; the decoder ignores trailing padding bytes).
 */

#include "7zAesEnc.h"

#include "Aes.h"
#include "Sha256.h"
#include "7zTypes.h"

#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define AES_ENC_BLOCK 16
#define AES_ENC_KEY_SIZE 32
#define AES_ENC_NUM_CYCLES_POWER 15   /* ~32768 KDF rounds, 7-Zip default */
#define AES_ENC_SALT_SIZE 8
#define AES_ENC_IV_SIZE 15

static int g_aes_enc_tables_ready = 0;

static void aes_enc_ensure_init(void) {
  if (!g_aes_enc_tables_ready) {
    AesGenTables();
    g_aes_enc_tables_ready = 1;
  }
  Sha256Prepare();
}

/* Derive the 32-byte AES key from a UTF-16LE password, salt and cycle power.
 * Mirrors 7zAesDec.c's KDF so archives interoperate with 7-Zip / p7zip. */
static void aes_enc_derive_key(const Byte* password16, size_t password16_len,
                               unsigned num_cycles_power, const Byte* salt,
                               size_t salt_size, Byte key[AES_ENC_KEY_SIZE]) {
  Byte counter[8];

  if (num_cycles_power == 0x3F) {
    /* Degenerate case: key = (salt || password || zeros(32))[0..31], no hash. */
    Byte buf[32];
    size_t o = 0;
    size_t i;
    memset(buf, 0, sizeof(buf));
    for (i = 0; i < salt_size && o < sizeof(buf); i++) buf[o++] = salt[i];
    for (i = 0; i < password16_len && o < sizeof(buf); i++) buf[o++] = password16[i];
    memcpy(key, buf, 32);
    return;
  }

  {
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

/* Fill `n` bytes with random material.
 * Windows: RtlGenRandom (no extra link requirement).
 * Other platforms: /dev/urandom, with a time-derived seed fallback so a
 * missing entropy source does not hard-fail. */
static void aes_enc_random(Byte* data, size_t n) {
#ifdef _WIN32
  RtlGenRandom(data, (ULONG)n);
#else
  FILE* f = fopen("/dev/urandom", "rb");
  if (f) {
    size_t got = fread(data, 1, n, f);
    fclose(f);
    if (got < n)
      for (size_t i = got; i < n; i++)
        data[i] = (Byte)(i ^ (i * 2654435761u) ^ 0xA5u);
    return;
  }
  for (size_t i = 0; i < n; i++)
    data[i] = (Byte)(i ^ (i * 2654435761u));
#endif
}

/* UTF-8 → UTF-16LE (no BOM), malloc'd. Caller frees. Same encoding the 7-Zip
 * / py7zr key derivation expects. Returns NULL on OOM. */
static Byte* aes_enc_utf8_to_utf16le(const char* utf8, size_t* out_len) {
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
    else { cp = 0xFFFD; extra = 0; }
    i++;
    for (size_t k = 0; k < extra && i < n; k++, i++)
      cp = (cp << 6) | ((unsigned char)utf8[i] & 0x3F);
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

int z7_aes_encode(const char* password,
                  Byte* out_props, size_t* out_props_size,
                  Byte* data, size_t data_size) {
  Byte* password16;
  size_t password16_len = 0;
  Byte key[AES_ENC_KEY_SIZE];
  unsigned char ivAes[AES_NUM_IVMRK_WORDS];
  const Byte* iv = out_props + 2 + AES_ENC_SALT_SIZE;
  size_t numBlocks = data_size / AES_ENC_BLOCK;

  if (!password || !password[0]) return Z7_AES_ERR_NO_PASSWORD;
  if (data_size % AES_ENC_BLOCK != 0) return Z7_AES_ERR_DATA;
  if (out_props && data_size != 0) {
    /* props buffer must hold 2 + salt + iv bytes */
  }

  out_props[0] = (Byte)(AES_ENC_NUM_CYCLES_POWER | 0xC0);
  out_props[1] = (Byte)(((AES_ENC_SALT_SIZE - 1) << 4) | (AES_ENC_IV_SIZE - 1));
  aes_enc_random(out_props + 2, AES_ENC_SALT_SIZE + AES_ENC_IV_SIZE);

  password16 = aes_enc_utf8_to_utf16le(password, &password16_len);
  if (!password16) return Z7_AES_ERR_MEM;

  aes_enc_derive_key(password16, password16_len, AES_ENC_NUM_CYCLES_POWER,
                     out_props + 2, AES_ENC_SALT_SIZE, key);
  memset(password16, 0, password16_len);
  free(password16);

  aes_enc_ensure_init();

  memcpy(ivAes, iv, 16);
  memset(ivAes + 16, 0, AES_NUM_IVMRK_WORDS - 16);
  AesCbc_Init((UInt32*)ivAes, ivAes);
  Aes_SetKey_Enc((UInt32*)(ivAes + 4), key, AES_ENC_KEY_SIZE);
  if (numBlocks > 0)
    g_AesCbc_Encode((UInt32*)ivAes, data, numBlocks);

  memset(key, 0, sizeof(key));
  memset(ivAes, 0, sizeof(ivAes));

  if (out_props_size)
    *out_props_size = 2 + AES_ENC_SALT_SIZE + AES_ENC_IV_SIZE;
  return Z7_AES_ENC_OK;
}

size_t z7_aes_pad_to_block(size_t size) {
  return ((size + (AES_ENC_BLOCK - 1)) & ~(size_t)(AES_ENC_BLOCK - 1));
}

UInt32 z7_aes_method_id(void) {
  return Z7_AES_METHOD_ID;
}

size_t z7_aes_props_size(void) {
  return 2 + AES_ENC_SALT_SIZE + AES_ENC_IV_SIZE;
}
