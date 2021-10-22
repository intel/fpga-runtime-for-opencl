// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_HASH_H
#define ACL_HASH_H

// Hashing
// =======
//
// Compute SHA-1 digests for byte sequences of up to 2^64-9 bytes.
// Beyond 2^64 - 9 bytes, it fails silently.
//
// References:
//    - RFC 3174.
//    - http://en.wikipedia.org/wiki/SHA-1
//
// Validated against Perl 5.8.8 Digest::SHA1 module, and sample
// code in RFC 3174.
//
// Example:
//
//    acl_hash_context_t ctx;
//    char digest[41];
//    if ( acl_hash_init_sha1( &ctx )
//    &&   acl_hash_add( &ctx, "abc", 3 )
//    &&   acl_hash_add( &ctx, "defg", 4 )
//    &&   acl_hash_hexdigest( &ctx, digest, 41 ) ) {
//       // Now I've got the hext digest!
//    } else {
//       // error somewhere along the way.
//    }

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef _MSC_VER
typedef unsigned __int64 uint64_t;
typedef unsigned __int32 uint32_t;
#else
#include <stdint.h>
#endif

/* The number of bytes required to store a NUL terminated hex string
 * representing an SHA-1 hash.
 * SHA-1 is 160 bits, so 40 hex chars plus a NUL terminator.
 */
#define ACL_HASH_SHA1_DIGEST_BUFSIZE 41

typedef struct {
  // From SHA-1.    http://en.wikipedia.org/wiki/SHA-1
  uint64_t len; // Number of bytes in the user data (so far)
  uint32_t h0, h1, h2, h3, h4;
  // The (len % 64) most recent bytes to be added.
  // tail[0] is the earliest data byte, and so on.
  char tail[64];
} acl_hash_sha1_context_t;

typedef struct {
  int is_open; // Still accepting more additions?
  union {
    acl_hash_sha1_context_t sha1;
  } alg;
} acl_hash_context_t;

// Each of these returns 0 for failure, non-zero for success.
int acl_hash_init_sha1(acl_hash_context_t *ctx);
int acl_hash_add(acl_hash_context_t *ctx, const void *buf, size_t len);
int acl_hash_add_file(acl_hash_context_t *ctx, const char *filename);

// Get a digest.  Once this is called, you can't further add data to be
// digested.
// Buf can be NULL, in which case no digest is written out.
// Return true if we could compute the digest, and it fits in buf_size
// characters (including the terminating NUL).
int acl_hash_hexdigest(acl_hash_context_t *ctx, char *digest_buf,
                       size_t digest_buf_size);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif
