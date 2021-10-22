// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_PROGRAM_H
#define ACL_PROGRAM_H

#include "acl.h"
#include "acl_types.h"

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// Allocate a kernel in this program.
cl_kernel acl_program_alloc_kernel(cl_program);

// Forget a previously-allocated kernel.
void acl_program_forget_kernel(cl_program program, cl_kernel kernel);

// Invalidate all builds. Used for testing.
void acl_program_invalidate_builds(cl_program program);

// Schedule an eager programming of the device onto the device op queue.
// Return a positive number if we succeeded, 0 otherwise.
int acl_submit_program_device_op(cl_event event);

// Program or reprogram the FPGA.
ACL_EXPORT
void acl_program_device(void *user_data, acl_device_op_t *op);

std::string acl_compute_hash_dir_name(cl_context context,
                                      const std::string &hash);

void acl_program_dump_dev_prog(acl_device_program_info_t *dev_prog);

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif
