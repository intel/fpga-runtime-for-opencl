// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_KERNEL_H
#define ACL_KERNEL_H

#include "acl.h"
#include "acl_types.h"
#include "acl_visibility.h"

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

// Find the accelerator implementing the given kernel.
const acl_accel_def_t *
acl_find_accel_def(cl_program program, const std::string &kernel_name,
                   const acl_device_binary_t *&dev_bin_ret, cl_int *errcode_ret,
                   cl_context context, cl_device_id which_device);

// Reset the contents of the kernel.
void acl_reset_kernel(cl_kernel kernel);

// Return number of non-null mem args.  Exposed for unit testing purposes.
int acl_num_non_null_mem_args(cl_kernel kernel);

// Submit a set of ops to the device op queue, to launch a kernel.
// This may also imply reprogramming the device and transferring buffers
// to and from the device.
// Return a positive number if we committed device ops, zero otherwise.
int acl_submit_kernel_device_op(cl_event event);

// Called by device op to start the kernel.
// Export this on GCC because it's used as a function pointer.
ACL_EXPORT
void acl_launch_kernel(void *user_data, acl_device_op_t *op);

// Called when we get a kernel interrupt indicating that profiling data is ready
ACL_EXPORT
void acl_profile_update(int activation_id);

// This should be called by the HAL, to receive notification of RUNNING and
// COMPLETE state transitions, and used printf buffer size
ACL_EXPORT
void acl_receive_kernel_update(int activation_id, cl_int status);

// Used to check if one of the kernel arguments needs to be mapped to the device
// When unmapping subbuffers we may transfer memory that is currently used
// by another kernel. Command_queue uses this function to check if it is
// safe to submit a kernel with subbuffers to the device_op_queue
int acl_kernel_has_unmapped_subbuffers(acl_mem_migrate_t *mem_migration);

// Checks if the program currently loaded on the passed-in device contains
// any device globals with reprogram init mode. When a kernel is submitted
// for the first time and this function returns true, a force reprogram will
// be scheduled even when the kernel binary hash matches the hash of the
// currently loaded program.
bool acl_device_has_reprogram_device_globals(cl_device_id device);

cl_int set_kernel_arg_mem_pointer_without_checks(cl_kernel kernel,
                                                 cl_uint arg_index,
                                                 void *arg_value);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif
