/* 7zAesDec.h -- 7zAES (AES-256-CBC + SHA-256 KDF) decoder.
 * See 7zAesDec.c for the algorithm description. */
#ifndef ZIP7_INC_7Z_AES_DEC_H
#define ZIP7_INC_7Z_AES_DEC_H

#include "7zTypes.h"

EXTERN_C_BEGIN

/* 7zAES coder method id. */
#define Z7_AES_METHOD_ID ((UInt32)0x06F10701)

/* Return codes. */
#define Z7_AES_OK 0
#define Z7_AES_ERR_NO_PASSWORD (-1)
#define Z7_AES_ERR_UNSUPPORTED (-2)
#define Z7_AES_ERR_DATA (-3)
#define Z7_AES_ERR_MEM (-4)

/* Decrypt `data_size` bytes of `data` in place using the 7zAES coder
 * properties `props` and the UTF-8 `password`.
 * `data_size` must be a multiple of 16 (the whole packed stream).
 * Returns Z7_AES_OK or a Z7_AES_ERR_* code. */
int z7_aes_decode(const Byte* props, size_t props_size, const char* password,
                  Byte* data, size_t data_size);

/* true when `method_id` is the 7zAES coder. */
int z7_aes_is_aes_method(UInt32 method_id);

EXTERN_C_END

#endif