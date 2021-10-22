// Copyright (C) 2014-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_PROFILER_H
#define ACL_PROFILER_H

#include "acl.h"
#include "acl_types.h"

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#define ACL_PROFILE_AUTORUN_KERNEL_NAME                                        \
  "Intel_Internal_Collect_Autorun_Profiling" // Same as AlteraCLParams.h
#define ACL_PROFILER_SHARED_COUNTER "ACL_INTERAL_SHARED_CONTROL_SETTING"

void acl_init_profiler();

// Query whether profiling is enabled
unsigned long is_profile_enabled();

// Query whether the profiler is on
unsigned long is_profile_timer_on();

void acl_enable_profiler_scan_chain(acl_device_op_t *op);

// Return non-zero when it actually read the scan chain, 0 when it doesn't do
// anything.
int acl_process_profiler_scan_chain(acl_device_op_t *op);

// Return non-zero when it actually read the scan chain, 0 when it doesn't do
// anything.
int acl_process_autorun_profiler_scan_chain(unsigned int physical_device_id,
                                            unsigned int accel_id);

// Sets the start timestamp for the autorun kernels
void acl_set_autorun_start_time();

// Gets and sets the shared counter control value
void set_env_shared_counter_val();

// Query what the shared counter control is set to
// 0-3 => valid control setting (see ip/src/common/lsu_top.v.tmpl,
// ip/src/common/hld_iord.sv.tmpl, & ip/src/common/hld_iowr.sv.tmpl for what 0-3
// represents) -1 => shared counters are off
int get_env_profile_shared_counter_val();

// Open the profiler output file
// Return 1 if successful and 0 otherwise
int acl_open_profiler_file();

// Close the profiler file
int acl_close_profiler_file();

#if defined(__cplusplus)
} /* extern "C" */
#endif

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif
