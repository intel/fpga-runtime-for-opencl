// Copyright (C) 2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CHECK_COPY_OVERLAP_H
#define CHECK_COPY_OVERLAP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Use internal visibility since this symbol is never called from another
// dynamic shared object, neither directly nor through a function pointer.
#ifdef __GNUC__
#pragma GCC visibility push(internal)
#endif

unsigned int check_copy_overlap(const size_t src_origin[],
                                const size_t dst_origin[],
                                const size_t region[], const size_t row_pitch,
                                const size_t slice_pitch);

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#ifdef __cplusplus
}
#endif

#endif // CHECK_COPY_OVERLAP_H
