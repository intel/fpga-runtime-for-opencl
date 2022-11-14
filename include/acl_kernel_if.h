// Copyright (C) 2013-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_KERNEL_IF_H
#define ACL_KERNEL_IF_H

#ifndef _WIN32
#include <stdint.h>
#endif

#include "acl_bsp_io.h"
#include "acl_hal.h"
#include "acl_types.h"

#include <optional>
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

// *********************** Data types **************************

typedef struct {
  uintptr_t address;
  uintptr_t bytes;
} acl_kernel_if_addr_range;

typedef struct {
  unsigned int physical_device_id;

  // Accelerator details
  unsigned int num_accel;
  int volatile **accel_job_ids; //[num_accel][accel_invoc_queue_depth]
  int *accel_queue_front;
  int *accel_queue_back;
  acl_kernel_if_addr_range *accel_csr;
  acl_kernel_if_addr_range *accel_perf_mon;
  unsigned int *accel_num_printfs;

  std::vector<std::optional<acl_streaming_kernel_control_info>>
      streaming_control_signal_names;

  // Track potential hangs
  time_ns last_kern_update;

  // Track current kernel cra segment
  uintptr_t cur_segment;

  acl_bsp_io io;

  // csr_version is absent if there is no accelerators or cra_ring_root doesn't
  // exist
  std::optional<unsigned int> csr_version;

  // The real kern->cra_ring_root_exist value is initialized in the
  // acl_kernel_if_update() function (reading the actual auto-discovery string
  // at that time).
  // However, the kern->cra_ring_root_exist is referenced in the
  // acl_kernel_if_post_pll_config_init() to check whether it's OK to read from
  // the CSR. And acl_kernel_if_post_pll_config_init() can be called prior to
  // the acl_kernel_if_update(), using a dummy auto-discovery string.
  // Therefore we must set a default value. (Otherwise, it's reading an
  // uninitialized value)

  // The reason of setting it to false:
  // Currently the cra read function will not be called in the early
  // acl_kernel_if_post_pll_config_init() call, because the kern->num_accel is 0
  // with dummy auto-discovery string, there is an if statement guard that. With
  // the default value to false, it will hit the assertions in the cra
  // read/write functions in case those are accidentally invoked too early,
  // e.g., in a future code refactoring.
  bool cra_ring_root_exist = false;

  // Depth of hardware kernel invocation queue
  unsigned int *accel_invoc_queue_depth;

  // Track which of the kernels is the autorun profiling kernel (-1 if none)
  int autorun_profiling_kernel_id;

  // Track debug printf activity
  time_ns last_printf_dump = 0;

  // CRA address offset for backwards compatibility
  unsigned int cra_address_offset = 8;
} acl_kernel_if;

// *********************** Public functions **************************

int acl_kernel_if_init(acl_kernel_if *kern, acl_bsp_io bsp_io,
                       acl_system_def_t *sysdef);
int acl_kernel_if_update(const acl_device_def_autodiscovery_t &devdef,
                         acl_kernel_if *kern);
int acl_kernel_if_is_valid(acl_kernel_if *kern);
int acl_kernel_if_post_pll_config_init(acl_kernel_if *kern);

void acl_kernel_if_register_callbacks(
    acl_kernel_update_callback kernel_update,
    acl_profile_callback profile_update,
    acl_process_printf_buffer_callback process_printf);

void acl_kernel_if_close(acl_kernel_if *kern);

void acl_kernel_if_launch_kernel(acl_kernel_if *kern,
                                 acl_kernel_invocation_wrapper_t *wrapper);
void acl_kernel_if_unstall_kernel(acl_kernel_if *kern, int activation_id);
void acl_kernel_if_update_status(acl_kernel_if *kern);
void acl_kernel_if_check_kernel_status(acl_kernel_if *kern);
void acl_kernel_if_reset(acl_kernel_if *kern);

// HAL-internal profile hardware access functions
int acl_kernel_if_get_profile_data(acl_kernel_if *kern, cl_uint accel_id,
                                   uint64_t *data, unsigned int length);
int acl_kernel_if_reset_profile_counters(acl_kernel_if *kern, cl_uint accel_id);
int acl_kernel_if_disable_profile_counters(acl_kernel_if *kern,
                                           cl_uint accel_id);
int acl_kernel_if_enable_profile_counters(acl_kernel_if *kern,
                                          cl_uint accel_id);
int acl_kernel_if_set_profile_shared_control(acl_kernel_if *kern,
                                             cl_uint accel_id);
int acl_kernel_if_set_profile_start_cycle(acl_kernel_if *kern, cl_uint accel_id,
                                          uint64_t value);
int acl_kernel_if_set_profile_stop_cycle(acl_kernel_if *kern, cl_uint accel_id,
                                         uint64_t value);

#ifdef __cplusplus
}
#endif

#endif // ACL_KERNEL_IF
