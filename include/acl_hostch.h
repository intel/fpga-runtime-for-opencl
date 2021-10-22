// Copyright (C) 2017-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_HOSTCH_H
#define ACL_HOSTCH_H

#include "acl.h"
#include "acl_types.h"
#include "acl_visibility.h"

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

// Bind a cl_pipe to a channel on a particular device
cl_int acl_bind_pipe_to_channel(cl_mem pipe, cl_device_id device,
                                const acl_device_def_autodiscovery_t &devdef);
// Process all the pending transactions on a cl_pipe
void acl_process_pipe_transactions(cl_mem pipe);
// Bind all cl_pipe in the context for the given device and process all the
// pending transactions
void acl_bind_and_process_all_pipes_transactions(
    cl_context context, cl_device_id device,
    const acl_device_def_autodiscovery_t &devdef);

#define HOST_TO_DEVICE 1
#define DEVICE_TO_HOST 0

#if defined(__cplusplus)
} /* extern "C" */
#endif

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif
