// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_CONTEXT_H
#define ACL_CONTEXT_H

#include <CL/opencl.h>

#include "acl_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// Update data structures in response to async msgs from devices.
// Do this as part of a waiting loop, along with acl_hal_yield().
void acl_update_context(cl_context context);
void acl_idle_update(cl_context context);

int acl_context_uses_device(cl_context context, cl_device_id device);

acl_kernel_invocation_wrapper_t *
acl_get_unused_kernel_invocation_wrapper(cl_context context);

void acl_context_print_hung_device_status(cl_context context);

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif
