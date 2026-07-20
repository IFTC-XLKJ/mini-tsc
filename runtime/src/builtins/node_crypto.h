#ifndef NODE_CRYPTO_H
#define NODE_CRYPTO_H

#include "runtime.h"

typedef struct HashContext {
  int algorithm; /* 0=md5, 1=sha1, 2=sha256, 3=sha512 */
  union {
    /* MD5 */
    struct { uint32_t state[4]; uint64_t count; uint8_t buffer[64]; } md5;
    /* SHA-1 */
    struct { uint32_t state[5]; uint64_t count; uint8_t buffer[64]; } sha1;
    /* SHA-256 */
    struct { uint32_t state[8]; uint64_t count; uint8_t buffer[64]; } sha256;
    /* SHA-512 */
    struct { uint64_t state[8]; uint64_t count; uint8_t buffer[128]; } sha512;
  } ctx;
} HashContext;

Value node_crypto_randomBytes(Value size);
Value node_crypto_randomUUID(void);
Value node_crypto_createHash(Value algorithm);
Value node_crypto_createHmac(Value algorithm, Value key);
Value node_crypto_hashUpdate(Value hashVal, Value data);
Value node_crypto_hashDigest(Value hashVal, Value encoding);
Value node_crypto_pbkdf2Sync(Value password, Value salt, Value iterations, Value keylen, Value digest);
Value node_crypto_pbkdf2(Value password, Value salt, Value iterations, Value keylen, Value digest, Value callback);
Value node_crypto_md5(Value data);
Value node_crypto_sha1(Value data);
Value node_crypto_sha256(Value data);
Value node_crypto_sha512(Value data);
Value node_crypto_hmac_sha256(Value key, Value data);
Value node_crypto_scryptSync(Value password, Value salt, Value keylen);

#endif /* NODE_CRYPTO_H */
