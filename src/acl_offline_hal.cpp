// Copyright (C) 2012-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// HALs for use when we only have offline devices or emulates them.
//
// Device memory is emulated via host memory.
// Kernels launch and complete right away, but do no computation.
//

// System headers.
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// External library headers.
#include <CL/opencl.h>
#include <acl_threadsupport/acl_threadsupport.h>

// Internal headers.
#include <acl_hal.h>
#include <acl_hostch.h>
#include <acl_offline_hal.h>
#include <acl_support.h>
#include <acl_thread.h>
#include <acl_types.h>
#include <acl_util.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

//////////////////////////////
// Global variables

static int acl_offline_hal_close_devices(cl_uint num_devices,
                                         const cl_device_id *devices);
static int acl_offline_hal_try_devices(cl_uint num_devices,
                                       const cl_device_id *devices,
                                       cl_platform_id platform);
static void acl_offline_hal_init_device(const acl_system_def_t *sysdef) {
  sysdef = sysdef;
}
static cl_ulong acl_offline_hal_get_timestamp(void);
static void acl_offline_hal_copy_hostmem_to_hostmem(cl_event event,
                                                    const void *src, void *dest,
                                                    size_t size);
static void acl_offline_hal_copy_hostmem_to_globalmem(cl_event event,
                                                      const void *src,
                                                      void *dest, size_t size);
static void acl_offline_hal_copy_globalmem_to_hostmem(cl_event event,
                                                      const void *src,
                                                      void *dest, size_t size);
static void acl_offline_hal_copy_globalmem_to_globalmem(cl_event event,
                                                        const void *src,
                                                        void *dest,
                                                        size_t size);
static void acl_offline_hal_register_callbacks(
    acl_event_update_callback event_update,
    acl_kernel_update_callback kernel_update,
    acl_profile_callback profile_update,
    acl_device_update_callback device_update,
    acl_process_printf_buffer_callback process_printf);
static void
acl_offline_hal_launch_kernel(unsigned int physical_id,
                              acl_kernel_invocation_wrapper_t *wrapper);
static void acl_offline_hal_unstall_kernel(unsigned int physical_id,
                                           int activation_id);
static int acl_offline_hal_program_device(unsigned int physical_id,
                                          const acl_device_def_t *devdef,
                                          const struct acl_pkg_file *binary,
                                          int acl_program_mode);
static cl_bool acl_offline_hal_query_temperature(unsigned int physical_id,
                                                 cl_int *temp);
static int
acl_offline_hal_get_device_official_name(unsigned int physical_device_id,
                                         char *name, size_t size);
static int
acl_offline_hal_get_device_vendor_name(unsigned int physical_device_id,
                                       char *name, size_t size);
static int acl_offline_hal_get_profile_data(unsigned int physical_device_id,
                                            unsigned int accel_id,
                                            uint64_t *data,
                                            unsigned int length);
static int
acl_offline_hal_reset_profile_counters(unsigned int physical_device_id,
                                       unsigned int accel_id);
static int
acl_offline_hal_disable_profile_counters(unsigned int physical_device_id,
                                         unsigned int accel_id);
static int
acl_offline_hal_enable_profile_counters(unsigned int physical_device_id,
                                        unsigned int accel_id);
static int
acl_offline_hal_set_profile_shared_control(unsigned int physical_device_id,
                                           unsigned int accel_id);
static int
acl_offline_hal_set_profile_start_cycle(unsigned int physical_device_id,
                                        unsigned int accel_id, uint64_t value);
static int
acl_offline_hal_set_profile_stop_cycle(unsigned int physical_device_id,
                                       unsigned int accel_id, uint64_t value);
static int
acl_offline_hal_has_svm_memory_support(unsigned int physical_device_id,
                                       int *value);
static int acl_offline_hal_has_physical_mem(unsigned int physical_device_id);
static void *acl_offline_hal_get_board_extension_function_address(
    const char *func_name, unsigned int physical_device_id);
static int acl_offline_hal_pll_reconfigure(unsigned int physical_device_id,
                                           const char *pll_settings_str);
static void acl_offline_hal_reset_kernels(cl_device_id device);

static acl_event_update_callback acl_offline_hal_event_callback = NULL;
static acl_kernel_update_callback acl_offline_hal_kernel_callback = NULL;
static acl_profile_callback acl_offline_hal_profile_callback = NULL;
static acl_device_update_callback acl_offline_hal_device_callback = NULL;
static acl_process_printf_buffer_callback
    acl_offline_process_printf_buffer_callback = NULL;

static const acl_hal_t acl_offline_hal = {
    acl_offline_hal_init_device,
    0,
    acl_offline_hal_get_timestamp,
    acl_offline_hal_copy_hostmem_to_hostmem,
    acl_offline_hal_copy_hostmem_to_globalmem,
    acl_offline_hal_copy_globalmem_to_hostmem,
    acl_offline_hal_copy_globalmem_to_globalmem,
    acl_offline_hal_register_callbacks,
    acl_offline_hal_launch_kernel,
    acl_offline_hal_unstall_kernel,
    acl_offline_hal_program_device,
    acl_offline_hal_query_temperature,
    acl_offline_hal_get_device_official_name,
    acl_offline_hal_get_device_vendor_name,
    NULL,
    NULL,
    acl_offline_hal_get_profile_data,
    acl_offline_hal_reset_profile_counters,
    acl_offline_hal_disable_profile_counters,
    acl_offline_hal_enable_profile_counters,
    acl_offline_hal_set_profile_shared_control,
    acl_offline_hal_set_profile_start_cycle,
    acl_offline_hal_set_profile_stop_cycle,
    acl_offline_hal_has_svm_memory_support,
    acl_offline_hal_has_physical_mem,
    NULL,
    acl_offline_hal_get_board_extension_function_address,
    acl_offline_hal_pll_reconfigure,
    acl_offline_hal_reset_kernels,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    acl_offline_hal_try_devices,
    acl_offline_hal_close_devices,
    NULL,
    NULL};

//////////////////////////////
// A very simple HAL

const acl_hal_t *acl_get_offline_hal(void) {
  acl_assert_locked();
  return &acl_offline_hal;
}

static cl_ulong acl_offline_hal_get_timestamp(void) {
  acl_assert_locked();
  return acl_get_time_ns();
}

static void acl_offline_hal_copy_hostmem_to_hostmem(cl_event event,
                                                    const void *src, void *dest,
                                                    size_t size) {
  acl_assert_locked();

  acl_offline_hal_event_callback(
      event, CL_RUNNING); // in "real life" this in response to a hw message
  acl_print_debug_msg(" Copying %zu bytes from %p to %p event %p\n", size, src,
                      dest, event);
  memmove(dest, src, size);
  acl_offline_hal_event_callback(
      event, CL_COMPLETE); // in "real life" this in response to a hw message
}

// The other variants get their own function
static void acl_offline_hal_copy_hostmem_to_globalmem(cl_event event,
                                                      const void *src,
                                                      void *dest, size_t size) {
  acl_assert_locked();

  // For offline purposes, the same.
  acl_offline_hal_copy_hostmem_to_hostmem(event, src, dest, size);
}
static void acl_offline_hal_copy_globalmem_to_hostmem(cl_event event,
                                                      const void *src,
                                                      void *dest, size_t size) {
  acl_assert_locked();

  // For offline purposes, the same.
  acl_offline_hal_copy_hostmem_to_hostmem(event, src, dest, size);
}
static void acl_offline_hal_copy_globalmem_to_globalmem(cl_event event,
                                                        const void *src,
                                                        void *dest,
                                                        size_t size) {
  acl_assert_locked();

  // For offline purposes, the same.
  acl_offline_hal_copy_hostmem_to_hostmem(event, src, dest, size);
}

static cl_bool acl_offline_hal_query_temperature(unsigned int physical_id,
                                                 cl_int *temp) {
  acl_assert_locked();

  *temp = 0; // Avoid Windows warning
  physical_id = physical_id;
  return 1; // Fake success
}

static int
acl_offline_hal_get_device_official_name(unsigned int physical_device_id,
                                         char *name, size_t size) {
  static const char *the_name = "Offline Device";
  acl_assert_locked();
  physical_device_id = physical_device_id; // Avoid Windows warning
  if (size > strnlen(the_name, MAX_NAME_SIZE) + 1)
    size = strnlen(the_name, MAX_NAME_SIZE) + 1;
  strncpy(name, the_name, size);
  return (int)size;
}

static int
acl_offline_hal_get_device_vendor_name(unsigned int physical_device_id,
                                       char *name, size_t size) {
  static const char *the_name = "Intel(R) Corporation";
  acl_assert_locked();
  physical_device_id = physical_device_id; // Avoid Windows warning
  if (size > strnlen(the_name, MAX_NAME_SIZE) + 1)
    size = strnlen(the_name, MAX_NAME_SIZE) + 1;
  strncpy(name, the_name, size);
  return (int)size;
}

static void acl_offline_hal_register_callbacks(
    acl_event_update_callback event_update,
    acl_kernel_update_callback kernel_update,
    acl_profile_callback profile_update,
    acl_device_update_callback device_update,
    acl_process_printf_buffer_callback process_printf) {
  acl_assert_locked();
  acl_offline_hal_event_callback = event_update;
  acl_offline_hal_kernel_callback = kernel_update;
  acl_offline_hal_profile_callback = profile_update;
  acl_offline_hal_device_callback = device_update;
  acl_offline_process_printf_buffer_callback = process_printf;
}

// Send a message to the accelerator controller to start the kernel.
// When emulating an offline device, just set it to running and then
// complete.
static void acl_offline_hal_launch_kernel(
    unsigned int physical_id,
    acl_kernel_invocation_wrapper_t *invocation_wrapper) {
  cl_int activation_id = invocation_wrapper->image->activation_id;
  acl_assert_locked();

  physical_id = physical_id;
  // For emulating an offline device, just say we start and finish right away.
  acl_offline_hal_kernel_callback(activation_id, CL_RUNNING);
  acl_offline_hal_kernel_callback(activation_id, CL_COMPLETE);
}

static void acl_offline_hal_unstall_kernel(unsigned int physical_id,
                                           int activation_id) {
  activation_id =
      activation_id; // avoid warning and hence build break on Windows.
  physical_id = physical_id;
  // nothing, kernel printf is not supported
}

static int acl_offline_hal_program_device(unsigned int physical_id,
                                          const acl_device_def_t *devdef,
                                          const struct acl_pkg_file *binary,
                                          int acl_program_mode) {
  devdef = devdef;
  binary = binary;
  physical_id = physical_id;
  acl_program_mode = acl_program_mode;
  return 0; // Signal success
}

static int acl_offline_hal_get_profile_data(unsigned int physical_device_id,
                                            unsigned int accel_id,
                                            uint64_t *data,
                                            unsigned int length) {
  unsigned i;
  physical_device_id =
      physical_device_id; // avoid warning and hence build break on Windows.
  accel_id = accel_id;
  for (i = 0; i < length; ++i) {
    data[i] = 0;
  }
  length = length;
  return 0; // Signal success
}

static int
acl_offline_hal_reset_profile_counters(unsigned int physical_device_id,
                                       unsigned int accel_id) {
  physical_device_id =
      physical_device_id; // avoid warning and hence build break on Windows.
  accel_id = accel_id;
  return 0; // Signal success
}

static int
acl_offline_hal_disable_profile_counters(unsigned int physical_device_id,
                                         unsigned int accel_id) {
  physical_device_id =
      physical_device_id; // avoid warning and hence build break on Windows.
  accel_id = accel_id;
  return 0; // Signal success
}

static int
acl_offline_hal_enable_profile_counters(unsigned int physical_device_id,
                                        unsigned int accel_id) {
  physical_device_id =
      physical_device_id; // avoid warning and hence build break on Windows.
  accel_id = accel_id;
  return 0; // Signal success
}

static int
acl_offline_hal_set_profile_shared_control(unsigned int physical_device_id,
                                           unsigned int accel_id) {
  physical_device_id =
      physical_device_id; // avoid warning and hence build break on Windows.
  accel_id = accel_id;
  return 0; // Signal success
}

static int
acl_offline_hal_set_profile_start_cycle(unsigned int physical_device_id,
                                        unsigned int accel_id, uint64_t value) {
  physical_device_id =
      physical_device_id; // avoid warning and hence build break on Windows.
  accel_id = accel_id;
  value = value;
  return 0; // Signal success
}

static int
acl_offline_hal_set_profile_stop_cycle(unsigned int physical_device_id,
                                       unsigned int accel_id, uint64_t value) {
  physical_device_id =
      physical_device_id; // avoid warning and hence build break on Windows.
  accel_id = accel_id;
  value = value;
  return 0; // Signal success
}

static int
acl_offline_hal_has_svm_memory_support(unsigned int physical_device_id,
                                       int *value) {
  acl_assert_locked();
  physical_device_id = physical_device_id; // Avoid Windows warning
  *value = (CL_DEVICE_SVM_COARSE_GRAIN_BUFFER |
            CL_DEVICE_SVM_FINE_GRAIN_BUFFER | CL_DEVICE_SVM_FINE_GRAIN_SYSTEM);
  return 1;
}

static int acl_offline_hal_has_physical_mem(unsigned int physical_device_id) {
  acl_assert_locked();
  physical_device_id = physical_device_id; // Avoid Windows warning
  return 1;
}

static int acl_offline_hal_pll_reconfigure(unsigned int physical_device_id,
                                           const char *pll_settings_str) {
  physical_device_id = physical_device_id;
  pll_settings_str = pll_settings_str;
  return 0; // Signal success
}

static void acl_offline_hal_reset_kernels(cl_device_id device) {
  device = device;
}

static int acl_offline_hal_try_devices(cl_uint num_devices,
                                       const cl_device_id *devices,
                                       cl_platform_id platform) {
  cl_uint i;
  platform = platform; // Avoid windows warning on unused parameters

  // offline devices are always half duplex
  for (i = 0; i < num_devices; i++) {
    devices[i]->def.concurrent_reads = 1;
    devices[i]->def.concurrent_writes = 1;
    devices[i]->def.max_inflight_mem_ops = 1;
    devices[i]->def.host_capabilities = 0;
    devices[i]->def.shared_capabilities = 0;
    devices[i]->def.device_capabilities = 0;
    devices[i]->def.min_host_mem_alignment = 0;
    devices[i]->opened_count++;
  }
  return 0;
}

static int acl_offline_hal_close_devices(cl_uint num_devices,
                                         const cl_device_id *devices) {
  unsigned int idevice;
  // Avoid the windows warnings
  num_devices = num_devices;
  devices = devices;

  for (idevice = 0; idevice < num_devices; idevice++) {
    assert(devices[idevice]->opened_count > 0);
    devices[idevice]->opened_count--;
  }
  return 0;
}

static void *acl_offline_hal_get_board_extension_function_address(
    const char *func_name, unsigned int physical_device_id) {
  // Avoiding Windows warnings
  func_name = func_name;
  physical_device_id = physical_device_id;

  return NULL;
}
#ifdef __GNUC__
#pragma GCC visibility pop
#endif
