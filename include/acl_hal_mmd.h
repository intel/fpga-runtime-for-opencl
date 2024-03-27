// Copyright (C) 2013-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_HAL_MMD_H
#define ACL_HAL_MMD_H

#ifndef ACL_HAL_MMD_EXPORT
#define ACL_HAL_MMD_EXPORT __declspec(dllimport)
#endif

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// Versioning constants, address maps, and bit/byte positionings used   //
// in acl_kernel_if and acl_pll                                         //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

//// acl_kernel_if
// Versioning: This value must be read from addr 0
// For unit tests to work, this defines must match the one in the unit test
// header file
#define KERNEL_VERSION_ID (0xa0c00001)
#define KERNEL_ROM_VERSION_ID (0xa0c00002)
// Version number in the top 16-bits of the 32-bit status register.  Used
// to verify that the hardware and HAL have an identical view of the CSR
// address map.
#define CSR_VERSION_ID_18_1 (3)
#define CSR_VERSION_ID_19_1 (4)
#define CSR_VERSION_ID_23_1 (5)
#define CSR_VERSION_ID CSR_VERSION_ID_23_1

// Address map
// For unit tests to work, these defines must match those in the unit test
// header file
#define OFFSET_KERNEL_VERSION_ID ((dev_addr_t)0x0000)
#define OFFSET_KERNEL_CRA_SEGMENT ((dev_addr_t)0x0020)
#define OFFSET_SW_RESET ((dev_addr_t)0x0030)
#define OFFSET_KERNEL_CRA ((dev_addr_t)0x1000)
#define OFFSET_CONFIGURATION_ROM ((dev_addr_t)0x2000)

// Addressses for Kernel System ROM
#define OFFSET_KERNEL_ROM_LOCATION_MSB (0x3ffffff8)
#define OFFSET_KERNEL_ROM_LOCATION_LSB (0x3ffffffc)
#define OFFSET_KERNEL_MAX_ADDRESS (0x3fffffff)

#define KERNEL_CRA_SEGMENT_SIZE (0x1000)
#define KERNEL_ROM_SIZE_BYTES_READ 4
#define KERNEL_ROM_SIZE_BYTES 8

// Byte offsets into the CRA:
// For CSR version >= 5 byte offsets are pushed back with the proper
// value except for the CSR later on in the runtime execution
#define KERNEL_OFFSET_CSR 0
#define KERNEL_OFFSET_PRINTF_BUFFER_SIZE 0x4
#define KERNEL_OFFSET_CSR_PROFILE_CTRL 0xC
#define KERNEL_OFFSET_CSR_PROFILE_DATA 0x10
#define KERNEL_OFFSET_CSR_PROFILE_START_CYCLE 0x18
#define KERNEL_OFFSET_CSR_PROFILE_STOP_CYCLE 0x20
#define KERNEL_OFFSET_FINISH_COUNTER 0x28
#define KERNEL_OFFSET_INVOCATION_IMAGE 0x30

// CSR version >= 5 byte offsets
#define KERNEL_OFFSET_START_REG 0x8

// Backwards compatibility with CSR_VERSION_ID 3
#define KERNEL_OFFSET_INVOCATION_IMAGE_181 0x28

// Bit positions
#define KERNEL_CSR_START 0
#define KERNEL_CSR_DONE 1
#define KERNEL_CSR_STALLED 3
#define KERNEL_CSR_UNSTALL 4
#define KERNEL_CSR_PROFILE_TEMPORAL_STATUS 5
#define KERNEL_CSR_PROFILE_TEMPORAL_RESET 6
#define KERNEL_CSR_LAST_STATUS_BIT KERNEL_CSR_PROFILE_TEMPORAL_RESET
#define KERNEL_CSR_STATUS_BITS_MASK                                            \
  ((unsigned)((1 << (KERNEL_CSR_LAST_STATUS_BIT + 1)) - 1))
#define KERNEL_CSR_LMEM_INVALID_BANK 11
#define KERNEL_CSR_LSU_ACTIVE 12
#define KERNEL_CSR_WR_ACTIVE 13
#define KERNEL_CSR_BUSY 14
#define KERNEL_CSR_RUNNING 15
#define KERNEL_CSR_FIRST_VERSION_BIT 16
#define KERNEL_CSR_LAST_VERSION_BIT 31

#define KERNEL_CSR_PROFILE_SHIFT64_BIT 0
#define KERNEL_CSR_PROFILE_RESET_BIT 1
#define KERNEL_CSR_PROFILE_ALLOW_PROFILING_BIT 2
#define KERNEL_CSR_PROFILE_LOAD_BUFFER_BIT 3
#define KERNEL_CSR_PROFILE_SHARED_CONTROL_BIT1 4
#define KERNEL_CSR_PROFILE_SHARED_CONTROL_BIT2 5

#define CONFIGURATION_ROM_BYTES 4096

#define RESET_TIMEOUT (2 * 1000 * 1000 * 1000)

//// acl_pll
// Address map
// For unit tests to work, these defines must match those in the unit test
// header file
#define OFFSET_PLL_VERSION_ID ((dev_addr_t)0x000)
#define OFFSET_ROM ((dev_addr_t)0x400)
#define OFFSET_RECONFIG_CTRL ((dev_addr_t)0x200)
#define OFFSET_RECONFIG_CTRL_20NM ((dev_addr_t)0x800)
#define OFFSET_COUNTER ((dev_addr_t)0x100)
#define OFFSET_RESET ((dev_addr_t)0x110)
#define OFFSET_LOCK ((dev_addr_t)0x120)

// Constants
#define MAX_KNOWN_SETTINGS 100
#define MAX_POSSIBLE_FMAX 2000000
#define MAX_RECONFIG_RETRIES 3
#define RECONFIG_TIMEOUT (1000000000ll)
#define CLK_MEASUREMENT_PERIOD (16 * 1024 * 1024)

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// Implementation of HAL that builds on top of aocl_mmd (board vendor   //
// visible), acl_pll, and acl_kernel_if.                                //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include <stddef.h>
#include <stdio.h>
#include <string>

#include "acl.h"
#include "acl_hal.h"

#include <MMD/aocl_mmd.h>

extern acl_kernel_update_callback acl_kernel_update_fn;
extern acl_process_printf_buffer_callback acl_process_printf_buffer_fn;

// Interface to simulator to describe memory on the 'real' device.
typedef struct {
  unsigned long long start; /* memory start location */
  unsigned long long size;  /* size of this memory */
} aocl_mmd_memory_info_t;

// Interface to simulator to describe streaming kernel arguments that are
// excluded from the invocation image. Streaming arguments are passed to the
// simulator by calling aocl_mmd_simulation_streaming_kernel_args(), before
// writing the kernel invocation image containing non-streaming arguments.
struct aocl_mmd_streaming_kernel_arg_info_t {
  // unique identifier for the bus-functional model (BFM)
  std::string name;
  // argument value
  std::vector<char> value;
};

// MMD interface ID cache populated by calling aocl_mmd_get_info with
// corresponding interface MMD info ID
typedef struct {
  int kernel_interface = -1;
  int pll_interface = -1;
  int memory_interface = -1;
} aocl_mmd_interface_info_t;

// MMD Version checking
// Since MMD version changes only with major releases it is safe to assume
// this is a float with at most one decimal
#define MMDVERSION_MATCH(A, B) ((float)(A) == (float)(B))
#define MMDVERSION_LESSTHAN(A, B) ((float)(A) < (float)(B))

typedef struct {
  std::string library_name;
  void *mmd_library;

  int (*aocl_mmd_get_offline_info)(
      aocl_mmd_offline_info_t, // requested_info_id,
      size_t,                  // param_value_size,
      void *,                  // param_value,
      size_t *                 // param_size_ret
  );

  int (*aocl_mmd_get_info)(int handle, aocl_mmd_info_t requested_info_id,
                           size_t param_value_size, void *param_value,
                           size_t *param_size_ret);

  int (*aocl_mmd_open)(const char *name);

  int (*aocl_mmd_close)(int handle);

  int (*aocl_mmd_set_interrupt_handler)(int handle,
                                        aocl_mmd_interrupt_handler_fn fn,
                                        void *user_data);

  int (*aocl_mmd_set_device_interrupt_handler)(
      int handle, aocl_mmd_device_interrupt_handler_fn, void *user_data);

  int (*aocl_mmd_set_status_handler)(int handle, aocl_mmd_status_handler_fn fn,
                                     void *user_data);

  int (*aocl_mmd_yield)(int handle);

  int (*aocl_mmd_read)(int handle, aocl_mmd_op_t op, size_t len, void *dst,
                       int mmd_interface, size_t offset);

  int (*aocl_mmd_write)(int handle, aocl_mmd_op_t op, size_t len,
                        const void *src, int mmd_interface, size_t offset);

  int (*aocl_mmd_copy)(int handle, aocl_mmd_op_t op, size_t len,
                       int mmd_interface, size_t src_offset, size_t dst_offset);

  // DEPRECATED. Use aocl_mmd_program instead
  int (*aocl_mmd_reprogram)(int handle, void *user_data, size_t size);

  void *(*aocl_mmd_shared_mem_alloc)(int handle, size_t size,
                                     unsigned long long *device_ptr_out);

  void (*aocl_mmd_shared_mem_free)(int handle, void *host_ptr, size_t size);

  // Only populated if MPSIM is active.
  void (*aocl_mmd_simulation_device_info)(int handle, int num_memories,
                                          aocl_mmd_memory_info_t *mem_info);

  // Host Channel calls
  int (*aocl_mmd_hostchannel_create)(int handle, char *channel_name,
                                     size_t queue_depth, int direction);

  int (*aocl_mmd_hostchannel_destroy)(int handle, int channel);

  void *(*aocl_mmd_hostchannel_get_buffer)(int handle, int channel,
                                           size_t *buffer_size, int *status);

  size_t (*aocl_mmd_hostchannel_ack_buffer)(int handle, int channel,
                                            size_t send_size, int *status);

  // This is the new aocl_mmd_program API for MMD later than 14.1. Acl hal will
  // choose either one according to mmd version
  int (*aocl_mmd_program)(int handle, void *user_data, size_t size,
                          aocl_mmd_program_mode_t program_mode);

  void *(*aocl_mmd_host_alloc)(int *handles, size_t num_devices, size_t size,
                               size_t alignment,
                               aocl_mmd_mem_properties_t *properties,
                               int *error);

  int (*aocl_mmd_free)(void *mem);

  void *(*aocl_mmd_shared_alloc)(int handle, size_t size, size_t alignment,
                                 aocl_mmd_mem_properties_t *properties,
                                 int *error);

  double mmd_version;

  // Passes streaming kernel argument names and values to simulator.
  void (*aocl_mmd_simulation_streaming_kernel_args)(
      int handle,
      const std::vector<aocl_mmd_streaming_kernel_arg_info_t> &streaming_args);

  // Submits streaming kernel control start signal to simulator.
  void (*aocl_mmd_simulation_streaming_kernel_start)(
      int handle, const std::string &signal_name, const int accel_id,
      const bool accel_has_agent_args);

  // Queries streaming kernel control done signal from simulator.
  // Returns non-negative number of finished kernels invocations.
  //
  // It is the responsibility of the simulator to ensure that any kernel
  // invocations that finish *while* this function is invoked are properly
  // accounted and returned in a subsequent invocation of this function.
  void (*aocl_mmd_simulation_streaming_kernel_done)(
      int handle, const std::string &signal_name, unsigned int &finish_counter);

  // Pass kernel-id to csr-address mapping read from the current binary
  // to the simulation runtime, so that it can detect which start register
  // has been written by the runtime.
  void (*aocl_mmd_simulation_set_kernel_cra_address_map)(
      int handle, const std::vector<uintptr_t> &kernel_csr_address_map);

  // Read and Write from/into the specific device global address. They are only
  // supported on the simulator device for now.
  int (*aocl_mmd_simulation_device_global_interface_read)(
      int handle, const char *interface_name, void *host_addr, size_t dev_addr,
      size_t size);
  int (*aocl_mmd_simulation_device_global_interface_write)(
      int handle, const char *interface_name, const void *host_addr,
      size_t dev_addr, size_t size);

  // Return the side band signal buffer corresponding to the side band signal
  // port identifier Get a pointer to the mmd buffer for the host channel
  // Simulation only mmd call as of 2024.1. HW MMD developer needs to implement
  // this function in the future To support hostpipe sideband signals.
  void *(*aocl_mmd_hostchannel_get_sideband_buffer)(
      int handle, int channel, aocl_mmd_hostchannel_port_id_t port_id,
      size_t *buffer_size, int *status);

} acl_mmd_dispatch_t;

typedef struct {
  int handle;
  std::string name;
  acl_mmd_dispatch_t *mmd_dispatch;
  aocl_mmd_interface_info_t mmd_ifaces;
} acl_mmd_device_t;

#endif
