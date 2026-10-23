/* 7zAesEnc.h -- 7zAES (AES-256-CBC + SHA-256 KDF) encoder.
 * Symmetric counterpart of 7zAesDec.h. See 7zAesEnc.c for the layout. */
#ifndef ZIP7_INC_7Z_AES_ENC_H
#define ZIP7_INC_7Z_AES_ENC_H

#include "7zTypes.h"

EXTERN_C_BEGIN

/* Return codes (mirror the decoder's). */
#define Z7_AES_ENC_OK 0
#define Z7_AES_ERR_NO_PASSWORD (-1)
#define Z7_AES_ERR_DATA (-3)
#define Z7_AES_ERR_MEM (-4)

/* Random salt/iv size pair used by the writer. */
#define Z7_AES_SALT_SIZE 8
#define Z7_AES_IV_SIZE 15
#define Z7_AES_PROPS_TOTAL ((2) + Z7_AES_SALT_SIZE + Z7_AES_IV_SIZE)

/* Encrypt `data_size` bytes of `data` (size a multiple of 16) in place with
 * AES-256-CBC, writing the coder properties into `out_props`
 * (>= Z7_AES_PROPS_TOTAL bytes; *out_props_size set to the length).
 * `password` is UTF-8. Returns Z7_AES_ENC_OK or Z7_AES_ERR_*. */
int z7_aes_encode(const char* password, Byte* out_props, size_t* out_props_size,
                  Byte* data, size_t data_size);

/* Smallest multiple of 16 that is >= `size` (for padding the packed span). */
size_t z7_aes_pad_to_block(size_t size);

/* 7zAES coder method id (0x06F10701). */
UInt32 z7_aes_method_id(void);
#define Z7_AES_METHOD_ID 0x06F10701u

/* Total length of the coder properties block. */
size_t z7_aes_props_size(void);

EXTERN_C_END

#endif
