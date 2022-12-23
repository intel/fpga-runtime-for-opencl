// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_GLOBALS_H
#define ACL_GLOBALS_H

#include "acl.h"
#include "acl_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// Returns 0 if it's not valid.
acl_system_def_t *acl_present_board_def(void);
int acl_present_board_is_valid(void);

// Can't use ACL after this.
// Undoes acl_init().
void acl_reset(void);
// Version of reset used in unit test only
void acl_reset_join_thread(void);

// Initializes the HAL and loads the builtin system definition.
//
// In normal flows this calls into the HAL to probe the device
// to get the system definition.
//
// It also supports an "offline device" flow.
// If environment variable CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA
// is set, then it names a device that we make sure will be available.
//
// If the device name is prefixed by "+" then that offline device
// is made available in addition to devices found via HAL probing.
// Otherwise, *only* the offline device is made available, and
// we use a simple HAL that emulates interaction with the device.
// The key differences are:
//    - device global memory is really in host memory
//    - kernels don't do computation: they just complete immediately.
//
// This function returns CL_TRUE if a hal is initialized and CL_FALSE
// if it is not.
cl_bool acl_init_from_hal_discovery(void);

// Looks at environment variable CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA.
// If it exists and is prefixed by "+" then:
//    Return a pointer to the device name (without the "+" prefix).
//    Set *use_offline_ret_only = 1
// If it exists and is not prefixed by "+" then
//    Return a pointer to the device name.
//    Set *use_offline_ret_only = 0
#define ACL_CONTEXT_OFFLINE_AND_AUTODISCOVERY 0
#define ACL_CONTEXT_OFFLINE_ONLY 1
#define ACL_CONTEXT_MSIM 3
#define ACL_CONTEXT_MPSIM 4
const char *acl_get_offline_device_user_setting(int *use_offline_only_ret);

ACL_EXPORT
extern struct _cl_platform_id acl_platform;

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif
