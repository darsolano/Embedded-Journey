/*
 * ws_crypto.c
 *
 *  Created on: Dec 13, 2025
 *      Author: Daruin Solano
 */


#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"

void ws_sha1(const uint8_t *data, size_t len, uint8_t out[20])
{
    mbedtls_sha1(data, len, out);
}

int ws_base64(const uint8_t *in, size_t in_len, char *out, size_t out_len)
{
    size_t olen = 0;
    if (mbedtls_base64_encode((unsigned char*)out, out_len,
                              &olen, in, in_len) != 0)
        return -1;
    out[olen] = 0;
    return (int)olen;
}
