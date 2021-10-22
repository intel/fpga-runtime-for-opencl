// Copyright (C) 2015-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_SAMPLER_H
#define ACL_SAMPLER_H

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

// Defines to match bitfield in clang
// cl_addressing_mode

#define CLK_ADDRESS_NONE 0x00;

#define CLK_ADDRESS_MIRRORED_REPEAT 0x01

#define CLK_ADDRESS_REPEAT 0x02

#define CLK_ADDRESS_CLAMP_TO_EDGE 0x03

#define CLK_ADDRESS_CLAMP 0x04

// cl_sampler_info

#define CLK_NORMALIZED_COORDS_FALSE 0x00

#define CLK_NORMALIZED_COORDS_TRUE 0x08

// filter mode

#define CLK_FILTER_NEAREST 0x00

#define CLK_FILTER_LINEAR 0x10

#if defined(__cplusplus)
} /* extern "C" */
#endif

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif
