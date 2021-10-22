// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_PRINTF_H
#define ACL_PRINTF_H

#include "acl.h"
#include "acl_types.h"
#include "acl_visibility.h"

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// Enqueue printf buffer dump
ACL_EXPORT
void acl_schedule_printf_buffer_pickup(int activation_id, int size,
                                       int overflow);

// Print the printf data associated with the given deviced operation
// and hence kernel execution instance.
ACL_EXPORT
void acl_process_printf_buffer(void *user_data, acl_device_op_t *op);

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif
