// Copyright (C) 2017-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

/* Interface to zlib for Package editor
 *
 * This dynamically loads zlib, avoiding build dependencies.
 */

#ifndef ZLIB_INTERFACE_H
#define ZLIB_INTERFACE_H

#include "zlib.h"

#ifdef __cplusplus
extern "C" {
#endif

// Interface to zlib routines, handling DLL loading first.
int zlib_deflateInit(z_streamp, int);
int zlib_deflate(z_streamp, int);
int zlib_deflateEnd(z_streamp);
int zlib_inflateInit(z_streamp);
int zlib_inflate(z_streamp, int);
int zlib_inflateEnd(z_streamp);

#ifdef __cplusplus
}
#endif

#endif /* ZLIB_INTERFACE_H */
