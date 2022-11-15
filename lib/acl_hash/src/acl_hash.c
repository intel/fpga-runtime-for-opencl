// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// Hashing
// =======
//
// RFC 3174.
// http://en.wikipedia.org/wiki/SHA-1

#include "acl_hash/acl_hash.h"

// want assert
#include <assert.h>

// want memset
#include <string.h>

//////////////////////////////
// Local functions prototypes

static void l_accum(acl_hash_sha1_context_t *c, const unsigned char *buf,
                    size_t len);
static void l_accum_block(acl_hash_sha1_context_t *c, const unsigned char *buf);
static void l_close(acl_hash_sha1_context_t *c, char *digest_buf);
static uint32_t l_leftrotate(uint32_t v, unsigned bits);
static uint32_t l_read_bigendian_i32(const unsigned char *buf);
static void l_write_bigendian_hex_i32(uint32_t v, char *buf);

//////////////////////////////
// ACL internal API

int acl_hash_init_sha1(acl_hash_context_t *ctx) {
  if (ctx == 0) {
    return 0;
  }
  ctx->is_open = 1;
  ctx->alg.sha1.len = 0;
  ctx->alg.sha1.h0 = 0x67452301;
  ctx->alg.sha1.h1 = 0xefcdab89;
  ctx->alg.sha1.h2 = 0x98badcfe;
  ctx->alg.sha1.h3 = 0x10325476;
  ctx->alg.sha1.h4 = 0xc3d2e1f0;
  return 1;
}

int acl_hash_add(acl_hash_context_t *ctx, const void *buf, size_t len) {
  if (ctx == 0) {
    return 0;
  }
  if (buf == 0) {
    return 0;
  }
  if (!ctx->is_open) {
    return 0;
  }

  l_accum(&(ctx->alg.sha1), (unsigned char *)buf, len);

  return 1;
}

int acl_hash_add_file(acl_hash_context_t *ctx, const char *filename) {
  ctx = ctx;
  filename = filename;
  return 0;
}

int acl_hash_hexdigest(acl_hash_context_t *ctx, char *digest_buf,
                       size_t digest_buf_size) {
  if (ctx == 0) {
    return 0;
  }
  if (digest_buf_size < ACL_HASH_SHA1_DIGEST_BUFSIZE) {
    return 0;
  }
  if (digest_buf == 0) {
    return 0;
  }

  // Once you compute a digest, then you can't add more data to digest.
  ctx->is_open = 0;

  l_close(&(ctx->alg.sha1), digest_buf);

  return 1;
}

//////////////////////////////
// Local functions

static void l_accum(acl_hash_sha1_context_t *c, const unsigned char *buf,
                    size_t len) {
  size_t buf_idx = 0;
  size_t tail_idx = c->len & 63;

  if (len == 0)
    return; // nothing to do!

  if (tail_idx) {
    // Write into the tail buffer and add that.
    while ((tail_idx < 64) && (buf_idx < len)) {
      c->tail[tail_idx] = buf[buf_idx];
      c->len++;
      buf_idx++;
      tail_idx++;
    }
    if (tail_idx == 64) {
      // We've already counted the blocks.
      // Just compute incremental hash.
      l_accum_block(c, c->tail);
    } else {
      return;
    }
  }

  if (len >= 64) { // Otherwise the unsigned math doesn't work!
    for (; buf_idx <= len - 64; buf_idx += 64) {
      // Have at least 64 bytes to add.
      c->len += 64;
      l_accum_block(c, buf + buf_idx);
    }
    assert(len - buf_idx < 64);
  }

  // Pick up the remainder.  Wait to accumulate the block next time.
  for (tail_idx = 0; buf_idx < len; buf_idx++, tail_idx++) {
    c->tail[tail_idx] = buf[buf_idx];
    c->len++;
  }
}

static void l_accum_block(acl_hash_sha1_context_t *ctx,
                          const unsigned char *buf) {
  unsigned i;
  uint32_t w[80];
  uint32_t a = ctx->h0;
  uint32_t b = ctx->h1;
  uint32_t c = ctx->h2;
  uint32_t d = ctx->h3;
  uint32_t e = ctx->h4;
  uint32_t f = 0;
  uint32_t k = 0;

  // Load initial part of w[].
  for (i = 0; i < 16; i++) {
    w[i] = l_read_bigendian_i32(buf + 4 * i);
  }
  // Extend to rest of w
  for (i = 16; i < 80; i++) {
    w[i] = l_leftrotate((w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]), 1);
  }

  // Main loop.
  for (i = 0; i < 80; i++) {
    uint32_t temp;
    if (i < 20) {
      f = (b & c) | ((~b) & d);
      k = 0x5a827999;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ed9eba1;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8f1bbcdc;
    } else if (i < 80) {
      f = b ^ c ^ d;
      k = 0xca62c1d6;
    }

    temp = l_leftrotate(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = l_leftrotate(b, 30);
    b = a;
    a = temp;
  }

  ctx->h0 += a;
  ctx->h1 += b;
  ctx->h2 += c;
  ctx->h3 += d;
  ctx->h4 += e;
}

static void l_close(acl_hash_sha1_context_t *c, char *digest_buf) {
  unsigned char extra[64];
  unsigned i;
  unsigned raw_tail_len = (1 /* 1-byte */ + 8 /* length field */ + c->len) & 63;
  unsigned num_zero_fill_bytes = raw_tail_len > 0 ? (64 - raw_tail_len) : 0;
  uint64_t user_bits = 8 * c->len;

  extra[0] = 0x80; // append '1' bit. Assume big endian even within byte.
  l_accum(c, extra, 1);

  // Zero fill as needed.
  memset(extra, 0, num_zero_fill_bytes);
  l_accum(c, extra, num_zero_fill_bytes);

  // User length of message in bits.
  // It's big-endian.
  for (i = 0; i < 8; i++) {
    extra[7 - i] = user_bits & 0xff;
    user_bits >>= 8;
  }
  l_accum(c, extra, 8);
  assert(0 == (c->len & 63));

  l_write_bigendian_hex_i32(c->h0, digest_buf);
  l_write_bigendian_hex_i32(c->h1, digest_buf + 8);
  l_write_bigendian_hex_i32(c->h2, digest_buf + 16);
  l_write_bigendian_hex_i32(c->h3, digest_buf + 24);
  l_write_bigendian_hex_i32(c->h4, digest_buf + 32);
  digest_buf[40] = 0;
}

#ifdef _MSC_VER
#pragma warning(push)
// The MSVC /sdl flag turns this false-positive warning into an error:
// C4146: unary minus operator applied to unsigned type, result still unsigned
// https://developercommunity.visualstudio.com/t/c4146-unary-minus-operator-applied-to-unsigned-typ/884520
#pragma warning(disable : 4146)
#endif

// See Safe, Efficient, and Portable Rotate in C/C++ by John Regehr.
// https://blog.regehr.org/archives/1063
static uint32_t l_leftrotate(uint32_t v, uint32_t bits) {
  assert(bits < 32);
  return (v << bits) | (v >> ((-bits) & 31));
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

static uint32_t l_read_bigendian_i32(const unsigned char *buf) {
  // Need to cast to uchar so we don't sign extend when widening out to 32 bits.
  uint32_t byte3 = buf[0];
  uint32_t byte2 = buf[1];
  uint32_t byte1 = buf[2];
  uint32_t byte0 = buf[3];
  uint32_t result = (byte3 << 24) | (byte2 << 16) | (byte1 << 8) | (byte0);
  return result;
}

static void l_write_bigendian_hex_i32(uint32_t v, char *buf) {
  int i;
  const char *hexchar = "0123456789abcdef";
  for (i = 0; i < 8; i++) {
    buf[7 - i] = hexchar[v & 15];
    v >>= 4;
  }
}
