// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_H
#define ACL_H

#include <array>
#include <assert.h>
#include <string>
#include <unordered_map>
#include <vector>

#include <CL/cl_ext.h>

#include "acl_visibility.h"

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

// When compiling C++, both GCC and visual studio support the inline keyword.
// GCC also supports it when compiling C. However, visual studio does not and
// instead requires you to use __inline.
#if !defined(__cplusplus) && defined(_MSC_VER)
#define inline __inline
#endif

/* Must accomodate most strict alignment among all types.
 * The worst is long16, or 8 * 16 = 128.
 * By setting it to 1024 bytes we allow more efficient
 * implementation of hardware loading or storing large
 * arrays of contiguous data
 */

#define ACL_MEM_ALIGN (1024)

/* Use ACL_ALIGNED to force alignment to ACL_MEM_ALIGN.
 * For windows we need to use 1024 directly for correct # of parentheses
 */
#ifdef _MSC_VER
#define ACL_ALIGNED __declspec(align(1024))
#else
#define ACL_ALIGNED __attribute__((aligned(ACL_MEM_ALIGN)))
#endif

#define ACL_MAX_GLOBAL_MEM (32)
#define MAX_NAME_SIZE (1204)

/////
///// Part 1.
/////

/* Types required to define the HAL's interface to the accelerators
 * in the hardware.
 *
 * For each kernel accelerator block, we need to know the address of
 * its control-status-registers, and optionally its performance monitor
 * registers.  These are Avalon slave interfaces, each with an address
 * range.
 * @{
 *
 * (Hint: We need this definition here so that when we load a program from
 * binary at runtime, we can tell the HAL to switch its accelerator
 * interfaces.)
 */
typedef unsigned int acl_avalon_addr_t; // 32 bits for now.

typedef struct {
  acl_avalon_addr_t address;
  acl_avalon_addr_t num_bytes;
} acl_avalon_slave_address_t;

typedef struct {
  std::string name;
  acl_avalon_slave_address_t csr;
  acl_avalon_slave_address_t perf_mon;
} acl_hal_accel_def_t;

/* @} */

/////
///// Part 2.
/////

/* Types required to define everything the host library needs to know about
 * the programmed FPGA, or a binary program (SOF) that can be loaded onto
 * the FPGA at runtime.
 * @{
 */

/* An address range type.
 * "begin" - the first storage location in the range. (inclusive endpoint)
 *
 * "next" - the first storage location beyond the range. (exclusive endpoint)
 * That is, the storage locations are
 *    ((char *)begin) through (((char *)end)-1)
 * This struct must remain trivially copyable.
 */
typedef struct {
  void *begin;
  void *next;
} acl_addr_range_t;

/* These data types are used to describe a kernel's interface, and
 * therefore also a device's available APIS.
 */
typedef enum {
  ACL_ARG_ADDR_NONE,
  ACL_ARG_ADDR_LOCAL,
  ACL_ARG_ADDR_GLOBAL,
  ACL_ARG_ADDR_CONSTANT /* a subset of global */
} acl_kernel_arg_addr_space_t;
typedef enum {
  ACL_ARG_BY_VALUE, /* values are presented by-value, i.e. just sent straight to
                       the kernel */
  ACL_ARG_MEM_OBJ,  /* values are memory objects */
  ACL_ARG_SAMPLER   /* values are samplers */
} acl_kernel_arg_category_t;
typedef enum {
  ACL_ARG_TYPE_NONE = 0,
  ACL_ARG_TYPE_CONST = 1,
  ACL_ARG_TYPE_RESTRICT = 2,
  ACL_ARG_TYPE_VOLATILE = 4,
  ACL_ARG_TYPE_PIPE = 8
} acl_kernel_arg_type_qualifier_t; // Can be either pipe, none, or a combination
                                   // of const, restrict, and volatile.
typedef enum {
  ACL_ARG_ACCESS_NONE,
  ACL_ARG_ACCESS_READ_ONLY,
  ACL_ARG_ACCESS_WRITE_ONLY,
  ACL_ARG_ACCESS_READ_WRITE
} acl_kernel_arg_access_qualifier_t; // this is defaulted to none, for non-pipe
                                     // and non-image args.

struct acl_streaming_kernel_arg_info {
  // name of the streaming interface at device image boundary
  std::string interface_name;
};

// This defines everything "interface" of a kernel argument.
// Be sure to keep this consistent with l_kernel_interface_match() in
// acl_kernel.cpp. This struct must remain trivially copyable.
typedef struct {
  acl_kernel_arg_addr_space_t addr_space; // This is in 1-1 correspondance with
                                          // cl_kernel_arg_address_qualifier
  acl_kernel_arg_category_t category;
  unsigned int size; /* How many bytes does it occupy?  If it's local, then this
                        is the size of the local storage.  */
  unsigned int aspace_number; // The actual ID number - bump allocator needs to
                              // track multiple local aspaces
  unsigned int lmem_size_bytes; // Only used for local mem - provides mem bytes
                                // allocated for this arg
  unsigned int
      alignment; /* For pointer arguments, what is the required alignment of the
                    argument.  Not defined for non-pointer arguments */
  bool host_accessible; // Only used when type_qualifier is pipe
  std::string
      pipe_channel_id; // Only used when type_qualifier is true, name of the
                       // channel interface binded to the kernel attr
  std::string buffer_location;
  // Extra information to be returned to user via clGetKernelArgInfo
  acl_kernel_arg_access_qualifier_t
      access_qualifier; // this is defaulted to none, for non-pipe and non-image
                        // args.
  acl_kernel_arg_type_qualifier_t
      type_qualifier; // Can be either pipe, none, or a combination of const,
                      // restrict, and volatile.
  // These strings will be empty if the arg info is not available.
  // No white spaces. e.g., "unsigned int" becomes "uint". However,
  // according to conformance tests, for structs and unions a space is
  // allowed, e.g., "struct mystruct"
  std::string type_name;
  std::string name;

  bool streaming_arg_info_available;
  acl_streaming_kernel_arg_info streaming_arg_info;
} acl_kernel_arg_info_t;

// This struct must remain trivially copyable.
typedef struct {
  std::string name;
  std::vector<acl_kernel_arg_info_t> args;
} acl_kernel_interface_t;

// Static demand (function scope locals) for a local address space
// This struct must remain trivially copyable.
typedef struct {
  unsigned int aspace_id;
  unsigned int static_demand;
} acl_local_aspace_info;

// This struct must remain trivially copyable.
typedef struct {
  unsigned index;
  std::string format_string;
} acl_printf_info_t;

// Signal names for streaming control kernel.
struct acl_streaming_kernel_control_info {
  std::string start;
  std::string done;
};

/* The definition of a single accelerator.
 * It can run a single kernel type.
 * We assume binary compilation only.
 * This struct must remain trivially copyable.
 */
typedef struct {
  unsigned int
      id; // Uniquely identifies this accelerator.  A handle used by the HAL.
  acl_addr_range_t mem; /* The kernel's local mem. */
  acl_kernel_interface_t iface;
  std::vector<acl_local_aspace_info> local_aspaces;
  size_t compile_work_group_size
      [3];                             /* value of
                                          __attribute__((reqd_work_group_size(X,Y,Z)))
                                          qualifier. 0,0,0 if none. */
  unsigned int is_workgroup_invariant; /* True if the kernel code can't tell if
                                          there is more than one workgroup. */
  unsigned int is_workitem_invariant;  /* True if the kernel code can't tell if
                                          there is more than one workitem. */
  unsigned int num_vector_lanes; /* Number of times the kernel is vectorized.
                                    Default value is 1 if non-vectorized. */
  unsigned int profiling_words_to_readback; /* Number of words in the profile
                                               scan chain for this kernel */
  unsigned int max_work_group_size; /* value of scalarized Maximum work-group
                                       size attribute */
  unsigned int
      max_global_work_dim; /* value of __attribute__((max_global_work_dim(N)))
                              qualifier. 3 if none. */
  std::vector<acl_printf_info_t> printf_format_info; /* info for each printf */
  unsigned int max_work_group_size_arr
      [3]; /* value of Maximum work-group size attribute controlled by
              __attribute__((max_work_group_size(X,Y,Z))) */
  unsigned int
      uses_global_work_offset; /* True if kernel can be enqueued with a non-zero
                                  global_work_offset. Controlled by
                                  __attribute__((uses_global_work_offset(N))) */
  unsigned int
      fast_launch_depth; /* How many kernels can be buffered on the device, 0
                            means no buffering just one can execute*/
  unsigned int is_sycl_compile; /* [1] SYCL compile; [0] OpenCL compile*/

  bool streaming_control_info_available;
  acl_streaming_kernel_control_info streaming_control_info;
} acl_accel_def_t;

/* An ACL system definition.
 *
 * Defines address ranges for the supported memory subspaces.
 * Defines the number of accelerators and their properties.
 *
 * In general, this tells the generic portion of the runtime system
 * everything it needs to know about a compiled program, i.e. a SOF
 * loaded into the FPGA.
 * The compiler generates a description string that goes with the SOF, and
 * that string is parsed in the "auto-discovery" phase to populate
 * this data structure.
 * Note that in all existing real flows, we only have one live device.
 */

/* Must match definition in aoc.pl */
#define ACL_MPSIM_DEVICE_NAME "SimulatorDevice"
/* Specifies which device simulator should simulate */
#define INTELFPGA_SIM_DEVICE_SPEC_DIR "INTELFPGA_SIM_DEVICE_SPEC_DIR"

#define ACL_RAND_HASH_SIZE 40

#ifdef __arm__
// 32 devices take most of memory on ARM, so we only allow 16
#define ACL_MAX_DEVICE (16)

#define ACL_CREATE_DEVICE_ADDRESS(physical_device_id, dev_ptr) (dev_ptr)
#define ACL_GET_PHYSICAL_ID(dev_ptr) (0)
#define ACL_STRIP_PHYSICAL_ID(dev_ptr) ((void *)(dev_ptr))

#else // BSP Makefiles need to be in sync with ACL_MAX_DEVICE
#define ACL_MAX_DEVICE (128)
/*
 * WARNING: These functions must be kept in sync with ACL_MAX_DEVICE
 * and compiler's dynamic address space resolution.
 *
 * Bits 63-62 are used by compiler for global/local resolution
 * Bit 61 is used to mark device allocations
 * Bits 60-54 are used for device ID
 */
#define ACL_CREATE_DEVICE_ADDRESS(physical_device_id, dev_ptr)                 \
  ((void *)(((unsigned long long)dev_ptr) |                                    \
            ((unsigned long long)physical_device_id << 54)))

#define ACL_GET_PHYSICAL_ID(dev_ptr)                                           \
  (((unsigned long long)dev_ptr >> 54) & 0x7F)
#define ACL_DEVICE_ALLOCATION(dev_ptr)                                         \
  (((unsigned long long)dev_ptr >> 61) & 0x1)
#define ACL_STRIP_PHYSICAL_ID(dev_ptr)                                         \
  ((void *)((unsigned long long)dev_ptr & 0x3FFFFFFFFFFFFF))
#endif

// Copy of acl::board::GlobalMemConfig::Type since runtime is C99.
// Keep in sync with acl::board::GlobalMemConfig::Type in
// acl/lib/acl_boardspec/include/BoardSpec.h.
typedef enum {
  ACL_GLOBAL_MEM_SHARED_VIRTUAL = 0,
  ACL_GLOBAL_MEM_SHARED_PHYSICAL,
  ACL_GLOBAL_MEM_DEVICE_PRIVATE,

  ACL_GLOBAL_MEM_TYPE_COUNT
} acl_system_global_mem_type_t;

// Keep in sync with acl::board::GlobalMemConfig::AllocationType in
// acl/lib/acl_boardspec/include/BoardSpec.h.
typedef enum {
  ACL_GLOBAL_MEM_UNDEFINED_ALLOCATION = 0,
  ACL_GLOBAL_MEM_HOST_ALLOCATION = 1,
  ACL_GLOBAL_MEM_SHARED_ALLOCATION = 2,
  ACL_GLOBAL_MEM_DEVICE_ALLOCATION = 4
} acl_system_global_mem_allocation_type_t;

// The memory definition shouldn't change across reprograms.
// We do allow burst_interleaved to change though, so only read
// burst_interleaved from last programmed aocx
typedef struct {
  // The physical range of memory addresses corresponding to the physical size
  // of the global memory.
  acl_addr_range_t range;
  acl_system_global_mem_type_t type;
  unsigned int num_global_banks;
  unsigned int burst_interleaved;
  std::string name;
  size_t config_addr;

  // If 'allocation_type' is set, it indicates that this interface is the one
  // used for the indicated type of allocation by the MMD.  If we request a
  // shared allocation from the MMD, the MMD will allocate this memory on the
  // interface that has 'shared' in its allocation_type.  See also the
  // corresponding comments in acl/lib/acl_boardspec/include/BoardSpec.h.
  acl_system_global_mem_allocation_type_t allocation_type;

  // The name of the global memory interface that will be used by the compiler
  // when an ambiguous pointer access is detected.
  std::string primary_interface;

  // A list of global memories that can be accessed through this global memory
  // interface.  Any interface which lists this one as its 'primary_interface'
  // must be included in this list.
  std::vector<std::string> can_access_list;

  // The usable range of memory addresses that is available to the user. It is a
  // subset of the physical range of a global memory. usable_range and physical
  // range may be equal but in some cases usable_range is smaller if memory is
  // reserved by the runtime.
  inline acl_addr_range_t get_usable_range() const {
    acl_addr_range_t result = range;

    // If the beginning memory address is 0, the usable range needs to be
    // truncated to ensure we don't use memory at address 0. Only the device
    // private memory needs to have the usable range adjusted because it uses
    // our custom allocator instead of malloc which never returns a memory
    // address of 0 for successful allocations.
    bool truncate_range =
        (result.begin == 0 && type == ACL_GLOBAL_MEM_DEVICE_PRIVATE);

    // If environment variable CL_ALLOW_GLOBAL_MEM_AT_NULL_ADDRESS_INTELFPGA is
    // set to non-zero value, don't truncate
    const char *enable_all_global_mem =
        std::getenv("CL_ALLOW_GLOBAL_MEM_AT_NULL_ADDRESS_INTELFPGA");
    if (enable_all_global_mem && (std::string(enable_all_global_mem) != "0")) {
      truncate_range = false;
    }

    if (truncate_range) {
      result.begin = (void *)ACL_MEM_ALIGN;
    }
    assert(result.begin < result.next &&
           "device private global memory is too small");

    return result;
  }

  std::string id = "-";
} acl_system_global_mem_def_t;

// Our allocator is optimized for simplicity, and for the case where there
// are only a few largish blocks in the system at a time.
//
// The scheme is as follows:
//   A Region is a contiguous set of addresses to be managed as a single
//   pool.  It is specified by an address range.
//   We assume the beginning address has the most general or stringent
//   alignment, e.g. 4 or 8 byte.
//
//   A region may be shared between multiple contexts (e.g. the host memory).
//   So all the regions are owned by the platform object.
//
//   Each Region is associated with a linked list of allocated
//   non-overlapping address ranges.
//   The list is ordered by beginning address.
//
//   Allocation is first-fit.
//   Deallocation just removes the allocated range from the list.
//
//   The allocator itself needs to dynamically provide link objects to the
//   algorithm.  We keep them on a linked list, threaded via the link
//   objects themselves.

// forward declare for use in acl_mem_region_t
typedef struct acl_block_allocation_t acl_block_allocation_t;

typedef struct acl_mem_region_t {
  int is_user_provided; // True if the user provides the memory. In that case
                        // the allocation problem is trivial: just accept what
                        // the user gives us.

  // There are two types of storage tracked with memory regions:
  //    a) raw memory on the host
  //    b) cl_mem buffer objects, either those created explicitly by the user,
  //    or
  //       those created internally by ACL.
  // The next two flags describe the "home" location of the storage.
  // If the enclosing object is a cl_mem buffer object, then the "home" location
  // is the logical location of the buffer, as specified by the cl_mem_flags
  // provided at buffer creation time. The actual runtime location of the
  // writable copy of the buffer is tracked by the
  // _cl_mem->writable_copy_on_host flag.
  int is_host_accessible;   // True if the home location of the storage can be
                            // addressed directly by the host.
  int is_device_accessible; // True if the home location of the storage can be
                            // addressed directly by the device.

  int uses_host_system_malloc;

  // The contiguous addresses being managed, if not using the host system
  // malloc. This is always aligned to 128 bytes, the minimum alignment of any
  // OpenCL basic type.
  acl_addr_range_t range;

  // block allocation objects associated with this region are kept in a linked
  // list
  acl_block_allocation_t *first_block;

} acl_mem_region_t;

// forward declare for use in acl_block_allocation_t
struct _cl_mem;

/* Block of memory allocated per cl_mem object
 * range - address range the memory is allocated to
 * region - acl_mem_region_t the memory is allocated to
 * next_block_in_region - for linked list of blocks
 * mem_obj - back pointer to cl_mem object
 */
struct acl_block_allocation_t {
  acl_addr_range_t range;
  acl_mem_region_t *region;
  acl_block_allocation_t *next_block_in_region;
  struct _cl_mem *mem_obj; // back pointer to cl_mem object
};

typedef struct {
  acl_addr_range_t range;
  struct _cl_mem *mem;
  struct _cl_device_id *device;
  cl_mem_alloc_flags_intel alloc_flags;
  cl_unified_shared_memory_type_intel type;
  cl_uint alignment; // May not be needed. Track for now.
} acl_usm_allocation_t;

typedef struct {
  std::string name;
  bool is_host_to_dev;
  bool is_dev_to_host;
  unsigned data_width;
  unsigned max_buffer_depth;
} acl_hostpipe_info_t;

typedef class acl_device_program_info_t *acl_device_program_info;

// Can't directly use the definitions inside aocl_mmd.h because everything
// above acl_hal_mmd.h needs to be MMD agnostic.
/**
 *  If not set allocation function is not supported, even if other capabilities
 * are set.
 */
#define ACL_MEM_CAPABILITY_SUPPORTED (1 << 0)
/**
 *  Supports atomic access to the memory by either the host or device.
 */
#define ACL_MEM_CAPABILITY_ATOMIC (1 << 1)
/**
 *  Supports concurrent access to the memory either by host or device
 */
#define ACL_MEM_CAPABILITY_CONCURRENT (1 << 2)
/**
 *  Memory can be accessed by multiple devices at the same time.
 */
#define ACL_MEM_CAPABILITY_P2P (1 << 3)

// Enum values here need to match the SPIRV spec for device global in
// https://github.com/intel/llvm/blob/44c6437684d64aba82d5a3de0e4bbe21d2b1f7ce/sycl/doc/design/spirv-extensions/SPV_INTEL_global_variable_decorations.asciidoc
// ACL_DEVICE_GLOBAL_HOST_ACCESS_TYPE_COUNT is used for validation
// in autodiscovery string parsing and should remain the last constant
// in the enum.
typedef enum {
  ACL_DEVICE_GLOBAL_HOST_ACCESS_READ_ONLY,
  ACL_DEVICE_GLOBAL_HOST_ACCESS_WRITE_ONLY,
  ACL_DEVICE_GLOBAL_HOST_ACCESS_READ_WRITE,
  ACL_DEVICE_GLOBAL_HOST_ACCESS_NONE,

  ACL_DEVICE_GLOBAL_HOST_ACCESS_TYPE_COUNT
} acl_device_global_host_access_t;

// Definition of device global.
struct acl_device_global_mem_def_t {
  uint64_t address;
  uint32_t size;
  acl_device_global_host_access_t host_access;
  // every device global has the same value for can_skip_programming
  bool can_skip_programming;
  bool implement_in_csr;
  // every device global has the same value for reset_on_reuse
  bool reset_on_reuse;
};

// Mapping of logical to physical host pipes.
struct acl_hostpipe_mapping {
  std::string logical_name;
  std::string physical_name; // chan_id in the board_spec.xml
  bool implement_in_csr;
  std::string csr_address; // Store this as string as this value can be '-' for
                           // non-CSR pipe.
  bool is_read;
  bool is_write;
  unsigned pipe_width;
  unsigned pipe_depth;

  // Matches the protocol_name enum in
  // https://github.com/intel/llvm/blob/sycl/sycl/include/sycl/ext/intel/experimental/pipe_properties.hpp
  // Set a default value in case it's missing.

  int protocol = -1; // avalon_streaming = 0, avalon_streaming_uses_ready = 1
                     // avalon_mm = 2, avalon_mm_uses_ready = 3

  // Introduced in 2024.2
  int is_stall_free = -1; // -1 means unset, set value is 0 or 1;
};

// Mapping of sideband signals to logical pipe

struct acl_sideband_signal_mapping {
  std::string logical_name;
  unsigned port_identifier;
  unsigned port_offset;   // bit
  unsigned sideband_size; // bit
};

// Must match the definition in the compiler
// Analysis/FPGAAnalysis/Utils/StreamParameters.h
enum signal_type {
  SignalUnknown = -1,
  AvalonData = 0,
  AvalonSop = 1,
  AvalonEop = 2,
  AvalonEmpty = 3
};

// Part of acl_device_def_t where members are populated from the information
// in the autodiscovery string. This will get updated every time the device
// is programmed with a new device binary as the new binary would contain a
// different autodiscovery string
typedef struct acl_device_def_autodiscovery_t {
  std::string name;
  std::string binary_rand_hash;
  unsigned int is_big_endian;
  std::vector<acl_accel_def_t> accel;
  std::vector<acl_hal_accel_def_t> hal_info; // Used by the HAL only.

  // If device definition was loaded from autodiscovery from earlier than 19.3,
  // num_global_mem_systems is 0.
  // num_global_mem_systems == 0 will be used for backwards compatibility flows.
  unsigned int num_global_mem_systems;
  std::array<acl_system_global_mem_def_t, ACL_MAX_GLOBAL_MEM> global_mem_defs;

  std::vector<acl_hostpipe_info_t> acl_hostpipe_info;

  // Device global definition.
  std::unordered_map<std::string, acl_device_global_mem_def_t>
      device_global_mem_defs;
  bool cra_ring_root_exist =
      true; // Set the default value to true for backwards compatibility flows.

  std::vector<acl_hostpipe_mapping> hostpipe_mappings;
  std::vector<acl_sideband_signal_mapping> sideband_signal_mappings;
} acl_device_def_autodiscovery_t;

typedef struct acl_device_def_t {
  // Information obtained from MMD when acl_hal_mmd tries the device
  unsigned int physical_device_id; // The ID of the physical device that the we
                                   // need to "talk" to
  unsigned int concurrent_reads;   // # of reads that can happen at one time
  unsigned int concurrent_writes;  // # of writes that can happen at one time
  unsigned int max_inflight_mem_ops; // max # of memory ops that can happen
                                     // concurrently
  unsigned int host_capabilities;
  unsigned int shared_capabilities;
  unsigned int device_capabilities;
  size_t min_host_mem_alignment;

  // autodiscovery information that changes every reprogram
  acl_device_def_autodiscovery_t autodiscovery_def;
} acl_device_def_t;

typedef struct {
  unsigned int num_devices;
  acl_device_def_t device[ACL_MAX_DEVICE];
} acl_system_def_t;

typedef struct acl_device_op_t *acl_device_op;

/* Initialize the system with the given accelerator definitions.
 * Call this *after* initializing the HAL.
 * This must be called before any OpenCL APIs are called.
 * Returns non-zero if successful, 0 otherwise.
 */
int acl_init(const acl_system_def_t *sys);

/* @} */

#if defined(__cplusplus)
} /* extern "C" */
#endif

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif
