// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_HAL_H
#define ACL_HAL_H

/**
  \file acl_hal.h
  \brief The ACL Hardware Abstraction Layer.

  The ACL HAL provides an abstraction of the hardware layer ...
*/

#ifndef WEAK
#ifdef _WIN32
#define WEAK
#else
#define WEAK __attribute__((weak))
#endif
#endif

#include <string>

#include <CL/cl_ext_intelfpga.h>

#include "acl.h" // for acl_system_def_t
#include "acl_visibility.h"

#ifndef ACL_HAL_EXPORT
#define ACL_HAL_EXPORT ACL_VISIBILITY_DEFAULT
#endif

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// Visual Studio doesn't include stdint.h, so doesn't have utin64_t.  Defining
// as a VS type when using that compiler.
#ifdef _MSC_VER
typedef unsigned __int64 uint64_t;
#endif

#if defined(__cplusplus)
extern "C" {
#endif

// The program mode
#define ACL_PROGRAM_PRESERVE_MEM 1
#define ACL_PROGRAM_UNPRESERVE_MEM 0

#define ACL_PROGRAM_FAILED -1
#define ACL_PROGRAM_CANNOT_PRESERVE_GLOBAL_MEM -2

struct acl_kernel_invocation_wrapper_t;
struct acl_pkg_file;

/// @name Callback type declarations
///@{
typedef void (*acl_event_update_callback)(cl_event event, int new_status);
typedef void (*acl_kernel_update_callback)(int activation_id, cl_int status);
typedef void (*acl_profile_callback)(int activation_id);
typedef void (*acl_device_update_callback)(
    unsigned physical_device_id, CL_EXCEPTION_TYPE_INTEL exception_type,
    void *user_private_info, size_t user_cb);
typedef void (*acl_process_printf_buffer_callback)(int activation_id, int size,
                                                   int debug_dump_printf);
///@}

typedef unsigned int mem_properties_t;

/// This struct defines the interface for the ACL Hardware Abstraction Layer.
typedef struct {
  /// Initialize the given device.  Blocking.
  void (*init_device)(const acl_system_def_t *sysdef);

  /// Give up the host processor thread.
  /// Do this instead of spinning the CPU waiting for something to occur.
  void (*yield)(cl_uint num_devices, const cl_device_id *devices);

  /// Get the time in nanoseconds.
  cl_ulong (*get_timestamp)(void);

  /**
    @name Memory transfers.
   If the event argument is 0, then this is a blocking transfer: it must
   complete before the function returns.
   Otherwise, they are non-blocking: the call may return before the
   transfer is complete.  When they are done, the event is marked
   with execution status CL_COMPLETE or a negative value indicating an
   error. */
  ///@{
  /// Host mem -> Host mem
  void (*copy_hostmem_to_hostmem)(cl_event event, const void *src, void *dest,
                                  size_t size);
  /// Host mem -> Global mem
  void (*copy_hostmem_to_globalmem)(cl_event event, const void *src, void *dest,
                                    size_t size);
  /// Global mem -> Host mem
  void (*copy_globalmem_to_hostmem)(cl_event event, const void *src, void *dest,
                                    size_t size);
  /// Global mem -> Global mem
  void (*copy_globalmem_to_globalmem)(cl_event event, const void *src,
                                      void *dest, size_t size);
  ///@}

  /// Callback registration
  void (*register_callbacks)(acl_event_update_callback event_update,
                             acl_kernel_update_callback kernel_update,
                             acl_profile_callback profile_callback,
                             acl_device_update_callback device_update,
                             acl_process_printf_buffer_callback process_printf);

  /// A kernel launch involves:
  ///    Copy the command queue id and kernel value args to global memory
  ///    Then invoke the kernel.
  /// The kernel replies by having something call acl_receive_kernel_update with
  /// the command queue id and status arg CL_RUNNING and CL_COMPLETE.
  void (*launch_kernel)(
      unsigned int physical_device_id,
      struct acl_kernel_invocation_wrapper_t *invocation_wrapper);

  void (*unstall_kernel)(unsigned int physical_device_id, int activation_id);

  /// Program the FPGA device with the given binary.
  /// The caller promises that the interface is described by the devdef
  /// argument.
  ///
  /// The status returned:
  ///  - 0 : program succeeds
  ///  - #ACL_PROGRAM_FAILED (-1) : program failed
  ///  - #ACL_PROGRAM_CANNOT_PRESERVE_GLOBAL_MEM (-2) : memory-preserved program
  ///  failed
  int (*program_device)(unsigned int physical_device_id,
                        const acl_device_def_t *devdef,
                        const struct acl_pkg_file *binary,
                        int acl_program_mode);

  /// Query on-die temperature if available.  Temperature reported in degrees
  /// Celsius.  Returns true if successfully read temperature from hardware.
  cl_bool (*query_temperature)(unsigned int physical_device_id, cl_int *temp);
  /// Get device official name
  int (*get_device_official_name)(unsigned int physical_device_id, char *name,
                                  size_t size);
  /// Get device vendor name
  int (*get_device_vendor_name)(unsigned int physical_device_id, char *name,
                                size_t size);

  /// Allocate physically-contiguous memory.
  void *(*legacy_shared_alloc)(cl_context context, size_t size,
                               unsigned long long *device_ptr_out);
  /// Free physically-contiguous memory.
  void (*legacy_shared_free)(cl_context context, void *vptr, size_t size);

  // ********************* Profile hardware accessors *********************
  /** @name Profile hardware accessors
   The following profile functions return 0 on success.
  */
  ///@{
  /// Populate \param data with profiling information from the device.
  int (*get_profile_data)(unsigned int physical_device_id,
                          unsigned int accel_id, uint64_t *data,
                          unsigned int length);
  /// Reset the profile counters to zero.
  int (*reset_profile_counters)(unsigned int physical_device_id,
                                unsigned int accel_id);
  /// Stop the profile counters from incrementing.
  int (*disable_profile_counters)(unsigned int physical_device_id,
                                  unsigned int accel_id);
  /// Allow the pofile counters to increment.
  int (*enable_profile_counters)(unsigned int physical_device_id,
                                 unsigned int accel_id);
  /// Configure shared counters for the accelerator.
  int (*set_profile_shared_control)(unsigned int physical_device_id,
                                    unsigned int accel_id);
  /// Set the cycle count for when the profile counters will start counting.
  int (*set_profile_start_cycle)(unsigned int physical_device_id,
                                 unsigned int accel_id, uint64_t cycle);
  /// Set the cycle count for when the profile counters will stop counting.
  int (*set_profile_stop_cycle)(unsigned int physical_device_id,
                                unsigned int accel_id, uint64_t cycle);
  ///@}

  /// Returns 1 if SVM is supported, and teh type of SVM memory in value
  int (*has_svm_memory_support)(unsigned int physical_device_id, int *value);
  /// Returns 1 if physical mem is supported
  int (*has_physical_mem)(unsigned int physical_device_id);

  /// Get pointer to board specific extension functions
  void *(*get_board_extension_function_address)(
      const char *func_name, unsigned int physical_device_id);

  int (*pll_reconfigure)(unsigned int physical_device_id,
                         const char *pll_settings_str);
  void (*reset_kernels)(cl_device_id device);
  /// Create a host channel in the mmd and get a handle to it
  int (*hostchannel_create)(unsigned int physical_device_id, char *channel_name,
                            size_t num_packets, size_t packet_size,
                            int direction);
  /// Destroy the host channel with the particular channel handle
  int (*hostchannel_destroy)(unsigned int physical_device_id,
                             int channel_handle);
  /// Pull read_size of data from the device into host_buffer
  size_t (*hostchannel_pull)(unsigned int physical_device_id,
                             int channel_handle, void *host_buffer,
                             size_t read_size, int *status);
  /// Push write_size of data to the device from host_buffer
  size_t (*hostchannel_push)(unsigned int physical_device_id,
                             int channel_handle, const void *host_buffer,
                             size_t write_size, int *status);
  /// Get a pointer to the mmd buffer for the host channel
  void *(*hostchannel_get_buffer)(unsigned int physical_device_id,
                                  int channel_handle, size_t *buffer_size,
                                  int *status);
  /// Tell the mmd that you're done reading/writing ack_size in the channel
  /// This will be starting from the pointer returned by the get_buffer call
  size_t (*hostchannel_ack_buffer)(unsigned int physical_device_id,
                                   int channel_handle, size_t ack_size,
                                   int *status);

  void (*get_device_status)(cl_uint num_devices, const cl_device_id *devices);

  int (*get_debug_verbosity)();

  /// Tries to open the given list of devices
  int (*try_devices)(cl_uint num_devices, const cl_device_id *devices,
                     cl_platform_id platform);

  /// Closes the given list of devices
  int (*close_devices)(cl_uint num_devices, const cl_device_id *devices);

  /// Allocates USM host memory
  void *(*host_alloc)(const std::vector<cl_device_id> devices, size_t size,
                      size_t alignment, mem_properties_t *properties,
                      int *error);

  /// Frees allocated memory by the MMD
  int (*free)(cl_context context, void *ptr);

  /// Allocate USM shared memory
  void *(*shared_alloc)(cl_device_id device, size_t size, size_t alignment,
                        mem_properties_t *properties, int *error);

  void (*simulation_streaming_kernel_start)(unsigned int physical_device_id,
                                            const std::string &signal_name,
                                            const int accel_id);
  void (*simulation_streaming_kernel_done)(unsigned int physical_device_id,
                                           const std::string &signal_name,
                                           unsigned int &finish_counter);

  void (*simulation_set_kernel_cra_address_map)(
      unsigned int physical_device_id,
      const std::vector<uintptr_t> &kernel_csr_address_map);

  size_t (*read_csr)(unsigned int physical_device_id, uintptr_t offset,
                     void *ptr, size_t size);

  size_t (*write_csr)(unsigned int physical_device_id, uintptr_t offset,
                      const void *ptr, size_t size);

  /// device global read and write function pointers
  int (*simulation_device_global_interface_read)(
      unsigned int physical_device_id, const char *interface_name,
      void *host_addr, size_t dev_addr, size_t size);
  int (*simulation_device_global_interface_write)(
      unsigned int physical_device_id, const char *interface_name,
      const void *host_addr, size_t dev_addr, size_t size);

} acl_hal_t;

/// Linked list of MMD library names to load.
typedef struct acl_mmd_library_names_s {
  /// Name of the MMD library.
  std::string library_name;
  /// Pointer to the next MMD library.
  struct acl_mmd_library_names_s *next;
} acl_mmd_library_names_t;

/// Return 0 if setting failed (e.g. invalid pointers), 1 otherwise
int acl_set_hal(const acl_hal_t *hal);
const acl_hal_t *acl_get_hal(void);
void acl_reset_hal(void);
extern int debug_mode;
int acl_print_debug_msg(const char *msg, ...);

ACL_HAL_EXPORT const acl_hal_t *
acl_mmd_get_system_definition(acl_system_def_t *system,
                              acl_mmd_library_names_t *_libraries_to_load);

extern acl_mmd_library_names_t *libraries_to_load;

#if defined(__cplusplus)
} /* extern "C" */
#endif

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif
