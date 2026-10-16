#pragma once
#include <cstddef>
typedef enum { MBEDTLS_MD_NONE = 0, MBEDTLS_MD_SHA256 = 9 } mbedtls_md_type_t;
typedef struct { mbedtls_md_type_t type; } mbedtls_md_info_t;
const mbedtls_md_info_t* mbedtls_md_info_from_type(mbedtls_md_type_t t);
int mbedtls_md_hmac(const mbedtls_md_info_t* info, const unsigned char* key, size_t keylen,
                    const unsigned char* input, size_t ilen, unsigned char* output);
