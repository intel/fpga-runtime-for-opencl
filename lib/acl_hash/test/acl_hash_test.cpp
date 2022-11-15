// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#include "CppUTest/TestHarness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acl_hash.c"
#include "acl_test.h"

TEST_GROUP(Hash) {
  enum { DIGEST_SIZE = 41 };
  void setup() { acl_hash_init_sha1(&m_ctx); }
  void teardown() {}

protected:
  acl_hash_context_t m_ctx;
  acl_hash_context_t m_ctx2;
  size_t m_dsize;
  char m_digest[DIGEST_SIZE];
};

TEST(Hash, link) {
  acl_hash_init_sha1(0);
  acl_hash_add(0, 0, 0);
  acl_hash_add_file(0, 0);
  acl_hash_hexdigest(0, 0, 0);
}

TEST(Hash, init) {
  CHECK_EQUAL(0, acl_hash_init_sha1(0));
  CHECK_EQUAL(1, acl_hash_init_sha1(&m_ctx));

  CHECK_EQUAL(1, m_ctx.is_open);
}

TEST(Hash, add_basic) {
  // bad context
  CHECK_EQUAL(0, acl_hash_add(0, 0, 0));

  CHECK_EQUAL(1, m_ctx.is_open);
  m_ctx2 = m_ctx;
  // No buffer
  CHECK_EQUAL(0, acl_hash_add(&m_ctx, 0, 0));

  // Can add with 0 buf len.
  const char *x = "abc";
  CHECK_EQUAL(1, acl_hash_add(&m_ctx, x, 0));
  CHECK_EQUAL(0, m_ctx.alg.sha1.len);

  // Can add with non-zero len
  CHECK_EQUAL(1, acl_hash_add(&m_ctx, x, 3));
  CHECK_EQUAL(3, m_ctx.alg.sha1.len);

  // Accumulate length.
  CHECK_EQUAL(1, acl_hash_add(&m_ctx, x, 3));
  CHECK_EQUAL(6, m_ctx.alg.sha1.len);

  // Can't add when it's closed
  m_ctx.is_open = 0;
  CHECK_EQUAL(0, acl_hash_add(&m_ctx, x, 0));

  // Zero out length
  CHECK_EQUAL(1, acl_hash_init_sha1(&m_ctx));
  CHECK_EQUAL(0, m_ctx.alg.sha1.len);
}

TEST(Hash, hexdigest_basic) {
  // bad context
  CHECK_EQUAL(0, acl_hash_hexdigest(0, 0, 0));

  CHECK_EQUAL(1, acl_hash_add(&m_ctx, "abc", 3));

  // Buffer too small
  CHECK_EQUAL(0, acl_hash_hexdigest(&m_ctx, 0, 0));
  CHECK_EQUAL(0, acl_hash_hexdigest(&m_ctx, 0, DIGEST_SIZE - 1));
  // Buffer is ok size.
  CHECK_EQUAL(1, acl_hash_hexdigest(&m_ctx, m_digest, DIGEST_SIZE));
  // Must pass a buffer in.
  CHECK_EQUAL(0, acl_hash_hexdigest(&m_ctx, 0, DIGEST_SIZE));
  // And now it's closed.
  CHECK_EQUAL(0, m_ctx.is_open);
}

TEST(Hash, hexdigest_empty) {
  // Empty string
  CHECK_EQUAL(1, acl_hash_init_sha1(&m_ctx));
  CHECK_EQUAL(1, acl_hash_add(&m_ctx, "", 0));
  CHECK_EQUAL(1, acl_hash_hexdigest(&m_ctx, m_digest, DIGEST_SIZE));
  printf("hash = %s\n", m_digest);
  CHECK_EQUAL(0, strcmp(m_digest, "da39a3ee5e6b4b0d3255bfef95601890afd80709"));
  CHECK_EQUAL(0, m_ctx.is_open);
}

TEST(Hash, hexdigest_abcde) {
  // Empty string
  CHECK_EQUAL(1, acl_hash_init_sha1(&m_ctx));
  CHECK_EQUAL(1, acl_hash_add(&m_ctx, "abcde", 5));
  CHECK_EQUAL(1, acl_hash_hexdigest(&m_ctx, m_digest, DIGEST_SIZE));
  printf("hash = %s\n", m_digest);
  CHECK_EQUAL(0, strcmp(m_digest, "03de6c570bfe24bfc328ccd7ca46b76eadaf4334"));
  CHECK_EQUAL(0, m_ctx.is_open);
}

TEST(Hash, hexdigest_abc) {
  // Match against code in RFC 3174.
  // Empty string
  CHECK_EQUAL(1, acl_hash_init_sha1(&m_ctx));
  CHECK_EQUAL(1, acl_hash_add(&m_ctx, "abc", 3));
  CHECK_EQUAL(1, acl_hash_hexdigest(&m_ctx, m_digest, DIGEST_SIZE));
  printf("hash = %s\n", m_digest);
  CHECK_EQUAL(0, strcmp(m_digest, "a9993e364706816aba3e25717850c26c9cd0d89d"));
  CHECK_EQUAL(0, m_ctx.is_open);
}

TEST(Hash, hexdigest_short) {
  // Short string
  CHECK_EQUAL(1, acl_hash_init_sha1(&m_ctx));
  CHECK_EQUAL(1, acl_hash_add(&m_ctx, "abc", 3));
  CHECK_EQUAL(1, acl_hash_hexdigest(&m_ctx, m_digest, DIGEST_SIZE));
  printf("hash = %s\n", m_digest);
  CHECK_EQUAL(0, strcmp(m_digest, "a9993e364706816aba3e25717850c26c9cd0d89d"));
  CHECK_EQUAL(0, m_ctx.is_open);
}

TEST(Hash, hexdigest_long_parts) {
  // Long string, in small parts.
  CHECK_EQUAL(1, acl_hash_init_sha1(&m_ctx));
  for (unsigned i = 0; i < 500; i++) {
    CHECK_EQUAL(6 * i, m_ctx.alg.sha1.len);
    acl_hash_add(&m_ctx, "Altera", 6);
    CHECK_EQUAL(1, m_ctx.is_open);
  }
  CHECK_EQUAL(6 * 500, m_ctx.alg.sha1.len);
  CHECK_EQUAL(1, m_ctx.is_open);
  CHECK_EQUAL(1, acl_hash_hexdigest(&m_ctx, m_digest, DIGEST_SIZE));
  CHECK_EQUAL(0, strcmp(m_digest, "86613b0ff1c3ec6415c40b6638a4b56d73baa800"));
  CHECK_EQUAL(0, m_ctx.is_open);
}

TEST(Hash, hexdigest_str64) {
  const char str64[] = "0123456780"
                       "0123456781"
                       "0123456782"
                       "0123456783"
                       "0123456784"
                       "0123456785"
                       "0123";
  CHECK_EQUAL(1, acl_hash_init_sha1(&m_ctx));
  CHECK_EQUAL(1, acl_hash_add(&m_ctx, str64, 64));
  CHECK_EQUAL(1, acl_hash_hexdigest(&m_ctx, m_digest, DIGEST_SIZE));
  CHECK_EQUAL(0, strcmp(m_digest, "e796e5f2867fcd8c58a2dcff481720ced23b227c"));
  CHECK_EQUAL(0, m_ctx.is_open);
}

TEST(Hash, hexdigest_str129) {
  const char str129[] = "0123456780"
                        "0123456781"
                        "0123456782"
                        "0123456783"
                        "0123456784"
                        "0123456785"
                        "0123"
                        "0123456780"
                        "0123456781"
                        "0123456782"
                        "0123456783"
                        "0123456784"
                        "0123456785"
                        "0123"
                        "y";

  CHECK_EQUAL(1, acl_hash_init_sha1(&m_ctx));
  CHECK_EQUAL(1, acl_hash_add(&m_ctx, str129, 129));
  CHECK_EQUAL(1, acl_hash_hexdigest(&m_ctx, m_digest, DIGEST_SIZE));
  CHECK_EQUAL(0, strcmp(m_digest, "7bcbe82f22dcf896409dd4759fc45ea83e4c05d2"));
  CHECK_EQUAL(0, m_ctx.is_open);
}

TEST_GROUP(Internal){};

TEST(Internal, Rotate) {
  CHECK_EQUAL(0x12345678, l_leftrotate(0x12345678, 0));
  CHECK_EQUAL(0x2468acf0, l_leftrotate(0x12345678, 1));
  CHECK_EQUAL(0x48d159e0, l_leftrotate(0x12345678, 2));
  CHECK_EQUAL(0x91a2b3c0, l_leftrotate(0x12345678, 3));
  CHECK_EQUAL(0x23456781, l_leftrotate(0x12345678, 4));
  CHECK_EQUAL(0x468acf02, l_leftrotate(0x12345678, 5));
  CHECK_EQUAL(0x8d159e04, l_leftrotate(0x12345678, 6));
  CHECK_EQUAL(0x1a2b3c09, l_leftrotate(0x12345678, 7));
  CHECK_EQUAL(0x34567812, l_leftrotate(0x12345678, 8));
  CHECK_EQUAL(0x68acf024, l_leftrotate(0x12345678, 9));
  CHECK_EQUAL(0xd159e048, l_leftrotate(0x12345678, 10));
  CHECK_EQUAL(0xa2b3c091, l_leftrotate(0x12345678, 11));
  CHECK_EQUAL(0x45678123, l_leftrotate(0x12345678, 12));
  CHECK_EQUAL(0x8acf0246, l_leftrotate(0x12345678, 13));
  CHECK_EQUAL(0x159e048d, l_leftrotate(0x12345678, 14));
  CHECK_EQUAL(0x2b3c091a, l_leftrotate(0x12345678, 15));
  CHECK_EQUAL(0x56781234, l_leftrotate(0x12345678, 16));
  CHECK_EQUAL(0xacf02468, l_leftrotate(0x12345678, 17));
  CHECK_EQUAL(0x59e048d1, l_leftrotate(0x12345678, 18));
  CHECK_EQUAL(0xb3c091a2, l_leftrotate(0x12345678, 19));
  CHECK_EQUAL(0x67812345, l_leftrotate(0x12345678, 20));
  CHECK_EQUAL(0xcf02468a, l_leftrotate(0x12345678, 21));
  CHECK_EQUAL(0x9e048d15, l_leftrotate(0x12345678, 22));
  CHECK_EQUAL(0x3c091a2b, l_leftrotate(0x12345678, 23));
  CHECK_EQUAL(0x78123456, l_leftrotate(0x12345678, 24));
  CHECK_EQUAL(0xf02468ac, l_leftrotate(0x12345678, 25));
  CHECK_EQUAL(0xe048d159, l_leftrotate(0x12345678, 26));
  CHECK_EQUAL(0xc091a2b3, l_leftrotate(0x12345678, 27));
  CHECK_EQUAL(0x81234567, l_leftrotate(0x12345678, 28));
  CHECK_EQUAL(0x02468acf, l_leftrotate(0x12345678, 29));
  CHECK_EQUAL(0x048d159e, l_leftrotate(0x12345678, 30));
  CHECK_EQUAL(0x091a2b3c, l_leftrotate(0x12345678, 31));
}
