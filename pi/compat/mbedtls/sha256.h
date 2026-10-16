#pragma once
#include <cstddef>
#include <cstdint>
typedef struct {
  uint32_t state[8];
  uint64_t bits;
  uint8_t buf[64];
  size_t len;
} mbedtls_sha256_context;
void mbedtls_sha256_init(mbedtls_sha256_context* c);
void mbedtls_sha256_free(mbedtls_sha256_context* c);
int mbedtls_sha256_starts(mbedtls_sha256_context* c, int is224);
int mbedtls_sha256_update(mbedtls_sha256_context* c, const unsigned char* in, size_t len);
int mbedtls_sha256_finish(mbedtls_sha256_context* c, unsigned char out[32]);
int mbedtls_sha256(const unsigned char* in, size_t len, unsigned char out[32], int is224);
