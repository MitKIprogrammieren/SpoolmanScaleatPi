#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"

#include <cstring>
#include <vector>

namespace {
const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void block(mbedtls_sha256_context* c, const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = c->state[0], b = c->state[1], cc = c->state[2], d = c->state[3];
  uint32_t e = c->state[4], f = c->state[5], g = c->state[6], h = c->state[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + S1 + ch + K[i] + w[i];
    uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
    uint32_t mj = (a & b) ^ (a & cc) ^ (b & cc);
    uint32_t t2 = S0 + mj;
    h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
  }
  c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d;
  c->state[4] += e; c->state[5] += f; c->state[6] += g; c->state[7] += h;
}
}  // namespace

void mbedtls_sha256_init(mbedtls_sha256_context* c) { memset(c, 0, sizeof(*c)); }
void mbedtls_sha256_free(mbedtls_sha256_context* c) { memset(c, 0, sizeof(*c)); }
int mbedtls_sha256_starts(mbedtls_sha256_context* c, int) {
  static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memcpy(c->state, iv, sizeof(iv));
  c->bits = 0;
  c->len = 0;
  return 0;
}
int mbedtls_sha256_update(mbedtls_sha256_context* c, const unsigned char* in, size_t len) {
  c->bits += (uint64_t)len * 8;
  while (len) {
    size_t n = 64 - c->len < len ? 64 - c->len : len;
    memcpy(c->buf + c->len, in, n);
    c->len += n; in += n; len -= n;
    if (c->len == 64) { block(c, c->buf); c->len = 0; }
  }
  return 0;
}
int mbedtls_sha256_finish(mbedtls_sha256_context* c, unsigned char out[32]) {
  uint64_t bits = c->bits;
  uint8_t pad = 0x80;
  mbedtls_sha256_update(c, &pad, 1);
  uint8_t z = 0;
  while (c->len != 56) mbedtls_sha256_update(c, &z, 1);
  uint8_t l[8];
  for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
  mbedtls_sha256_update(c, l, 8);
  for (int i = 0; i < 8; i++) {
    out[4 * i] = (uint8_t)(c->state[i] >> 24);
    out[4 * i + 1] = (uint8_t)(c->state[i] >> 16);
    out[4 * i + 2] = (uint8_t)(c->state[i] >> 8);
    out[4 * i + 3] = (uint8_t)c->state[i];
  }
  return 0;
}
int mbedtls_sha256(const unsigned char* in, size_t len, unsigned char out[32], int is224) {
  mbedtls_sha256_context c;
  mbedtls_sha256_init(&c);
  mbedtls_sha256_starts(&c, is224);
  mbedtls_sha256_update(&c, in, len);
  mbedtls_sha256_finish(&c, out);
  return 0;
}

const mbedtls_md_info_t* mbedtls_md_info_from_type(mbedtls_md_type_t t) {
  static const mbedtls_md_info_t sha256 = {MBEDTLS_MD_SHA256};
  return t == MBEDTLS_MD_SHA256 ? &sha256 : nullptr;
}

int mbedtls_md_hmac(const mbedtls_md_info_t* info, const unsigned char* key, size_t keylen,
                    const unsigned char* input, size_t ilen, unsigned char* output) {
  if (!info || info->type != MBEDTLS_MD_SHA256) return -1;
  uint8_t k[64] = {0};
  if (keylen > 64) mbedtls_sha256(key, keylen, k, 0);
  else memcpy(k, key, keylen);
  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
  uint8_t inner[32];
  mbedtls_sha256_context c;
  mbedtls_sha256_init(&c);
  mbedtls_sha256_starts(&c, 0);
  mbedtls_sha256_update(&c, ipad, 64);
  mbedtls_sha256_update(&c, input, ilen);
  mbedtls_sha256_finish(&c, inner);
  mbedtls_sha256_starts(&c, 0);
  mbedtls_sha256_update(&c, opad, 64);
  mbedtls_sha256_update(&c, inner, 32);
  mbedtls_sha256_finish(&c, output);
  return 0;
}

static int b64val(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

int mbedtls_base64_decode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen) {
  std::vector<unsigned char> out;
  out.reserve(slen * 3 / 4);
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < slen; i++) {
    unsigned char c = src[i];
    if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
    int v = b64val(c);
    if (v < 0) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((unsigned char)(acc >> bits));
    }
  }
  *olen = out.size();
  if (!dst || dlen < out.size()) return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
  if (!out.empty()) memcpy(dst, out.data(), out.size());
  return 0;
}

int mbedtls_base64_encode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t need = ((slen + 2) / 3) * 4;
  *olen = need + 1;
  if (!dst || dlen < need + 1) return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
  size_t o = 0;
  for (size_t i = 0; i < slen; i += 3) {
    uint32_t v = (uint32_t)src[i] << 16;
    if (i + 1 < slen) v |= (uint32_t)src[i + 1] << 8;
    if (i + 2 < slen) v |= src[i + 2];
    dst[o++] = (unsigned char)t[(v >> 18) & 63];
    dst[o++] = (unsigned char)t[(v >> 12) & 63];
    dst[o++] = i + 1 < slen ? (unsigned char)t[(v >> 6) & 63] : '=';
    dst[o++] = i + 2 < slen ? (unsigned char)t[v & 63] : '=';
  }
  dst[o] = 0;
  *olen = o;
  return 0;
}
