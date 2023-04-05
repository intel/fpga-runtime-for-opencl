// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <assert.h>
#include <optional>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

// External library headers.
#include <CL/opencl.h>
#include <acl_threadsupport/acl_threadsupport.h>

// Internal headers.
#include <acl.h>
#include <acl_auto.h>
#include <acl_auto_configure.h>
#include <acl_device_op.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_icd_dispatch.h>
#include <acl_kernel.h>
#include <acl_platform.h>
#include <acl_printf.h>
#include <acl_shipped_board_cfgs.h> // for acl_shipped_board_cfgs[]
#include <acl_support.h>
#include <acl_thread.h>
#include <acl_types.h>
#include <acl_util.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

//////////////////////////////
// Global variables
// Platform is not initialized, and don't use absent devices
// Need to "export" it since its address is taken in some unit tests.
ACL_EXPORT
struct _cl_platform_id acl_platform = {
    nullptr // Dispatch is empty
    ,
    0 // acl_platform is not initialized
    ,
    0 // default value for device_exception_platform_counter
    ,
    "" // No offline device specified by an environment variable, as far as we
       // know right now.
};

// Used to detect if user is creating contexts/getting platform ids in multiple
// processes.
int platform_owner_pid;
// ID of the thread that created platform.
// Used to detect if user is doing multithreading.
int platform_owner_tid = -1;

cl_platform_id acl_get_platform() { return &acl_platform; }

// Static global copies of shipped board definitions.
// Need either this storage, or a way to deep copy the accelerator
// definitions.
static std::vector<std::optional<acl_system_def_t>> shipped_board_defs;

//////////////////////////////
// Local functions.
static void l_initialize_offline_devices(int offline_mode);
static void l_initialize_devices(const acl_system_def_t *present_board_def,
                                 int offline_mode, unsigned int num_devices,
                                 const cl_device_id *devices);
static void l_add_device(int idx);

//////////////////////////////
// OpenCL API

// Return info about available platforms.
// Can be used to query the number of available platforms, or to get their
// ids, or both.
ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clGetPlatformIDsIntelFPGA(cl_uint num_entries, cl_platform_id *platforms,
                          cl_uint *num_platforms_ret) {
  std::scoped_lock lock{acl_mutex_wrapper};

  // Set this in case of early return due to error in other arguments.
  if (num_platforms_ret) {
    *num_platforms_ret = 1;
  }

  if (platforms && num_entries <= 0) {
    return CL_INVALID_VALUE;
  }
  if (num_platforms_ret == 0 && platforms == 0) {
    return CL_INVALID_VALUE;
  }

  // We want to support two kinds of flows:
  //
  //    1. Where there is a valid board available for the host to use,
  //    e.g. in the PCIe slot.  It will have at least a blank OpenCL SOF
  //    programmed into it which responds to auto-discovery with a
  //    configuration string.
  //
  //    2. Offline compilation mode.  Where the user can use a context
  //    property (or environment variable) to forcibly select a device
  //    from the list of boards we support.
  //
  // Advertise all supported boards via device ids.
  // But normally only the discovered one is marked as available.
  // If in offline compilation mode, only the
  //

  // If we don't have a valid system here, check if the autodiscovery can find
  // it. We also need to get the HAL definition.
  if (!acl_platform.initialized) {
    cl_bool result;
    // Load definitions of devices we can target:
    //    - Probed devices we find actually attached to the host (probed on
    //    PCIe), if any,
    //    - Followed by followed by the list of devices we ship.
    // Only probed devices will be marked with .present == 1.
    //
    // In the end this calls back into acl_init_platform which also sets
    // acl_platform.initialized = 1.
    result = acl_init_from_hal_discovery();
    if (!result) {
      return CL_PLATFORM_NOT_FOUND_KHR;
    }
  }
  if (!acl_get_hal()) {
    return CL_PLATFORM_NOT_FOUND_KHR;
  }
  if (!acl_platform.initialized) {
    return CL_PLATFORM_NOT_FOUND_KHR;
  }

  // Return some data
  if (platforms) {
    platforms[0] = &acl_platform;
  }
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetPlatformIDs(cl_uint num_entries,
                                                 cl_platform_id *platforms,
                                                 cl_uint *num_platforms_ret) {
  return clGetPlatformIDsIntelFPGA(num_entries, platforms, num_platforms_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clIcdGetPlatformIDsKHR(cl_uint num_entries, cl_platform_id *platforms,
                       cl_uint *num_platforms_ret) {
  return clGetPlatformIDsIntelFPGA(num_entries, platforms, num_platforms_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetPlatformInfoIntelFPGA(
    cl_platform_id platform, cl_platform_info param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret) {
  const char *str = 0;
  size_t result_len;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_platform_is_valid(platform)) {
    return CL_INVALID_PLATFORM;
  }

  VALIDATE_ARRAY_OUT_ARGS(param_value_size, param_value, param_value_size_ret,
                          0);

  switch (param_name) {
  // We don't offer an online compiler.
  case CL_PLATFORM_PROFILE:
    str = acl_platform.profile;
    break;
  case CL_PLATFORM_VERSION:
    str = acl_platform.version;
    break;
  case CL_PLATFORM_NAME:
    str = acl_platform.name;
    break;
  case CL_PLATFORM_VENDOR:
    str = acl_platform.vendor;
    break;
  case CL_PLATFORM_EXTENSIONS:
    str = acl_platform.extensions;
    break;
  case CL_PLATFORM_ICD_SUFFIX_KHR:
    str = acl_platform.suffix;
    break;
  default:
    return CL_INVALID_VALUE;
    break;
  }
  assert(str);
  result_len = strnlen(str, MAX_NAME_SIZE) + 1; // Remember the terminating NUL

  if (param_value) {
    // Actually try to return the string.
    if (param_value_size < result_len) {
      // Buffer is too small to hold the return value.
      return CL_INVALID_VALUE;
    }
    strncpy((char *)param_value, str, result_len);
  }

  if (param_value_size_ret) {
    *param_value_size_ret = result_len;
  }

  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetPlatformInfo(
    cl_platform_id platform, cl_platform_info param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret) {
  return clGetPlatformInfoIntelFPGA(platform, param_name, param_value_size,
                                    param_value, param_value_size_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clUnloadPlatformCompilerIntelFPGA(cl_platform_id platform) {
  // Not fully implemented yet.
  std::scoped_lock lock{acl_mutex_wrapper};
  if (!acl_platform_is_valid(platform)) {
    return CL_INVALID_PLATFORM;
  }
  // For the sake of MSVC compiler warnings.
  // We don't have any platform compilers, so unloading is successful!
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clUnloadPlatformCompiler(cl_platform_id platform) {
  return clUnloadPlatformCompilerIntelFPGA(platform);
}

//////////////////////////////
// Internals

int acl_platform_is_valid(cl_platform_id platform) {
  // We only support one value!
  return platform == &acl_platform;
}

const char *acl_platform_extensions() {
  return "cl_khr_byte_addressable_store" // yes, we have correct access to
                                         // individual bytes!
         " cles_khr_int64" // yes, we support 64-bit ints in the embedded
                           // profile

         " cl_khr_icd"
#if ACL_SUPPORT_IMAGES == 1
         " cl_khr_3d_image_writes"
#endif
#if ACL_SUPPORT_DOUBLE == 1
         " cl_khr_fp64"
#endif
#ifdef ACL_120
         " cl_khr_global_int32_base_atomics"
         " cl_khr_global_int32_extended_atomics"
         " cl_khr_local_int32_base_atomics"
         " cl_khr_local_int32_extended_atomics"
#endif
#ifndef __arm__
         " cl_intel_unified_shared_memory"
#endif
         " cl_intel_create_buffer_with_properties"
         " cl_intel_mem_channel_property"
         " cl_intel_mem_alloc_buffer_location"
         " cl_intel_program_scope_host_pipe";
}

// Initialize the internal bookkeeping based on the system definition
// provided to us.
void acl_init_platform(void) {
  acl_assert_locked();

  acl_platform.dispatch = &acl_icd_dispatch;
  for (int i = 0; i < ACL_MAX_DEVICE; i++) {
    acl_platform.device[i].dispatch = &acl_icd_dispatch;
  }

  acl_platform.initialized = 1;

  // Debug mode to find leaks.
  acl_platform.track_leaked_objects = (0 != acl_getenv("ACL_TRACK_LEAKS"));
  acl_platform.cl_obj_head = 0;

  // Set offline_device property
  const char *offline_device =
      acl_get_offline_device_user_setting(&acl_platform.offline_mode);
  if (offline_device) {
    if (acl_platform.offline_mode == ACL_CONTEXT_MPSIM) {
      acl_platform.offline_device = ACL_MPSIM_DEVICE_NAME;
    } else {
      acl_platform.offline_device = offline_device;
    }
  }

  acl_platform.name = "Intel(R) FPGA SDK for OpenCL(TM)";
  acl_platform.vendor = "Intel(R) Corporation";
  acl_platform.suffix = "IntelFPGA";
#ifdef ACL_120
  acl_platform.version =
      "OpenCL 1.2 Intel(R) FPGA SDK for OpenCL(TM), Version " ACL_VERSION_PLAIN;
#else
  acl_platform.version =
      "OpenCL 1.0 Intel(R) FPGA SDK for OpenCL(TM), Version " ACL_VERSION_PLAIN;
#endif
  // The extensions string specifies the extensions supported by our framework.
  // To add an extension, append a flag name and separate it from others using a
  // space.
  acl_platform.extensions = acl_platform_extensions();

  acl_platform.profile = "EMBEDDED_PROFILE";
  acl_platform.hal = acl_get_hal();

  if (acl_platform.hal == nullptr) {
    return;
  }

  // User is supposed to interact with runtime only on a single process.
  // Keeping the pid of that process.
  if (platform_owner_pid == 0) {
    platform_owner_pid = acl_get_pid();
  }

  if (platform_owner_tid == 0) {
    platform_owner_tid = acl_get_thread_id();
  }

  // Register the callbacks now that we have a HAL
  acl_platform.hal->register_callbacks(
      acl_set_execution_status, acl_receive_kernel_update, acl_profile_update,
      acl_receive_device_exception, acl_schedule_printf_buffer_pickup);

  // These are the OpenCL runtime objects that can be allocated.
  // See the UML object diagram for the OpenCL 1.1 runtime for ownership
  // rules.  (Figure 2.1, page 20 in the OpenCL 1.1 spec.)
  //
  // Ownership is as follows:
  //    The Context has a composition relation with:
  //       CommandQueue
  //       Event
  //       MemObject
  //       Sampler
  //       Program
  //    That means the Context object is responsible for teardown of those
  //    other objects with which is is associated.
  //
  //    The CommandQueue has a composition relation with:
  //       Event  (presumably different instances than the ones associated
  //       with CommandQueue?)
  //
  //    The Device objects are not owned by any other object.

  // We use reference counts in two ways:
  //
  //    Transitive destruction from owner objects to the composition
  //    children.
  //
  //    All objects: Tracking whether a statically allocated object is in use.

  // This is NULL when there is no device attached to the host.  This
  // occurs in "offline capture" mode when we have set
  // CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA to the name of the device to emulate
  // having.
  acl_platform.initial_board_def = acl_present_board_def();

  switch (acl_platform.offline_mode) {
  case ACL_CONTEXT_OFFLINE_AND_AUTODISCOVERY:
    acl_platform.num_devices =
        acl_platform.initial_board_def->num_devices +
        (offline_device ? 1 : 0); // the devices in the board def + 1 for the
                                  // offline device, if it exists
    break;
  case ACL_CONTEXT_MPSIM:
    acl_platform.num_devices =
        acl_platform.initial_board_def
            ->num_devices; // Simulator has its mmd, so it is loaded like an
                           // actual online device
    break;
  case ACL_CONTEXT_OFFLINE_ONLY:
    acl_platform.num_devices = 1; // Only the offline device
    break;
  }

  for (unsigned int i = 0; i < acl_platform.num_devices; i++) {
    // initialize static information for these devices
    l_add_device(static_cast<int>(i));
  }

  l_initialize_offline_devices(acl_platform.offline_mode);

  // Device operation queue.
  acl_init_device_op_queue(&acl_platform.device_op_queue);

  // Initialize sampler allocator.
  for (int i = 0; i < ACL_MAX_SAMPLER; i++) {
    cl_sampler sampler = &(acl_platform.sampler[i]);
    sampler->id = i;
    acl_reset_ref_count(sampler);
    // Hook up the free chain.
    sampler->link = (i == ACL_MAX_SAMPLER - 1 ? ACL_OPEN : i + 1);
  }
  // Now initialize the regions.
  // Note: The free cl_mem list will be shared between all regions.
  acl_platform.free_sampler_head = 0;

  // Cache various limits.

  acl_platform.max_compute_units = 1; // minimum allowed
  acl_platform.max_constant_args = 8; // minimum allowed
  acl_platform.max_param_size = 256;  // minimum allowed for embedded profile.
                                      // full profile is 1KB. Affects HW too.
  acl_platform.max_work_item_dimensions = 3; // minimum allowed

  // Want this positive, even if user puts it into a signed int
  acl_platform.max_work_group_size = 0x7fffffff;
  acl_platform.max_work_item_sizes = acl_platform.max_work_group_size;

  acl_platform.mem_base_addr_align = ACL_MEM_ALIGN * 8;

  // Maximum size of the internal buffer that
  // holds the output of printf calls from a kernel
  acl_platform.printf_buffer_size = ACL_PRINTF_BUFFER_TOTAL_SIZE;
  // printf buffer size override
  {
    const char *override = 0;
    override = acl_getenv("ACL_PRINTF_BUFFER_SIZE");
    if (override) {
      // There was a string.
      char *endptr = 0;
      long value = strtol(override, &endptr, 10);
      if (endptr == override // no valid characters
          || *endptr         // an invalid character
          || (value <= 0 || value > (long)ACL_PRINTF_BUFFER_TOTAL_SIZE)) {
        fprintf(stderr,
                "Warning: Invalid value in enviornment variable "
                "ACL_PRINTF_BUFFER_SIZE. Falling back to default size\n");
        fprintf(stderr,
                "         ACL_PRINTF_BUFFER_SIZE must be between 0 and %u\n",
                ACL_PRINTF_BUFFER_TOTAL_SIZE);
      } else {
        // Was ok.
        acl_platform.printf_buffer_size = (unsigned int)value;
      }
    }
  }

  acl_platform.min_data_type_align_size = ACL_MEM_ALIGN;

  acl_platform.single_fp_config =
      CL_FP_ROUND_TO_NEAREST | CL_FP_INF_NAN; // minimum allowed
  acl_platform.double_fp_config = // Minimum allowed in OpenCL 1.0 - 1.2,
                                  // provided doubles are supported
      CL_FP_FMA | CL_FP_ROUND_TO_NEAREST | CL_FP_ROUND_TO_ZERO |
      CL_FP_ROUND_TO_INF | CL_FP_INF_NAN | CL_FP_DENORM;
  acl_platform.queue_properties =
      CL_QUEUE_PROFILING_ENABLE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE;

  // Set up capturing of kernel sources.
  {
    const char *capture_base_path = acl_getenv("ACL_CAPTURE_BASE");
    if (capture_base_path &&
        strnlen(capture_base_path, MAX_NAME_SIZE) < ACL_MAX_PATH) {
      strncpy(&(acl_platform.capture_base_path[0]), capture_base_path,
              ACL_MAX_PATH - 1);
      acl_platform.capture_base_path[ACL_MAX_PATH - 1] = '\0';
      acl_platform.next_capture_id = 0;
    } else {
      acl_platform.capture_base_path[0] = '\0';
      acl_platform.next_capture_id = ACL_OPEN;
    }
  }

  acl_platform.max_pipe_args = 16;
  acl_platform.pipe_max_active_reservations = 1;
  acl_platform.pipe_max_packet_size = 1024;

  acl_platform.host_auto_mem.first_block = NULL;
  acl_platform.host_auto_mem.range.begin = 0;
  acl_platform.host_auto_mem.range.next = 0;

  acl_platform.global_mem.first_block = NULL;
  // The range will be determined from the memory on each device
  acl_platform.global_mem.range.begin = 0;
  acl_platform.global_mem.range.next = 0;

  acl_platform.host_user_mem.first_block = NULL;
  acl_platform.host_user_mem.range.begin = 0; // fake
  acl_platform.host_user_mem.range.next =
      (void *)(0xffffffffffffffffULL); // fake

  acl_platform.num_global_banks = 0;
}

void acl_finalize_init_platform(unsigned int num_devices,
                                const cl_device_id *devices) {
  int have_single_bank_with_shared_memory;
  acl_assert_locked();
  assert(num_devices > 0);

  l_initialize_devices(acl_present_board_def(), acl_platform.offline_mode,
                       num_devices, devices);

  if (is_SOC_device()) {
    size_t cur_num_banks =
        devices[0]->def.autodiscovery_def.global_mem_defs[0].num_global_banks;
    // In shared memory system, host memory is device accessible if have only
    // one bank (which can only be shared memory).
    have_single_bank_with_shared_memory = (cur_num_banks == 1);
  } else {
    have_single_bank_with_shared_memory = 0;
  }

  acl_platform.host_auto_mem.is_user_provided = 0;
  acl_platform.host_auto_mem.is_host_accessible = 1;
  acl_platform.host_auto_mem.is_device_accessible =
      have_single_bank_with_shared_memory; // assume separate memory "segment"
  acl_platform.host_auto_mem.uses_host_system_malloc = 1;

  acl_platform.global_mem.is_user_provided = 0;
  acl_platform.global_mem.is_host_accessible =
      have_single_bank_with_shared_memory; // assume separate memory "segment"
  acl_platform.global_mem.is_device_accessible = 1;
  acl_platform.global_mem.uses_host_system_malloc =
      have_single_bank_with_shared_memory;

  acl_platform.host_user_mem.is_user_provided = 1;
  acl_platform.host_user_mem.is_host_accessible = 1;
  acl_platform.host_user_mem.is_device_accessible = (int)is_SOC_device();
  acl_platform.host_user_mem.uses_host_system_malloc =
      have_single_bank_with_shared_memory;
}

static void l_show_devs(const char *prefix) {
  unsigned int i;
  acl_assert_locked();

  for (i = 0; i < acl_platform.num_devices; i++) {
    acl_print_debug_msg(
        " %s Device[%d] = %p %s present %d \n", (prefix ? prefix : ""), i,
        acl_platform.device[i].def.autodiscovery_def.name.c_str(),
        acl_platform.device[i].def.autodiscovery_def.name.c_str(),
        acl_platform.device[i].present);
  }
}

static void l_initialize_offline_devices(int offline_mode) {
  acl_platform.global_mem.range.begin = 0;
  acl_platform.global_mem.range.next = 0;

  // Parse the definitions of shipped boards, but just once.
  // Don't necessarily load them into the device list.
  //
  // See acl_shipped_board_cfgs.h which is generated via the
  // board_cfgs makefile target.
  // It's an array of shipped_board_cfg struct where name is board name, and
  // board_cfgs is the auto configuration string.
  shipped_board_defs.clear();
  for (const auto &board_cfg : acl_shipped_board_cfgs) {
    // This is always different storage.
    // We need this because l_add_device will just pointer-copy the
    // acl_device_def_t.
    auto &board_def = shipped_board_defs.emplace_back();
    board_def.emplace();
    std::string err_msg;
    if (!acl_load_device_def_from_str(
            board_cfg.cfg, board_def->device[0].autodiscovery_def, err_msg)) {
      board_def.reset();
      continue;
    }
    board_def->num_devices = 1;
  }

  if (offline_mode == ACL_CONTEXT_MPSIM) {
    auto &board_def = shipped_board_defs.emplace_back();
    board_def.emplace();
    std::string err_msg;
    if (!acl_load_device_def_from_str(acl_shipped_board_cfgs[0].cfg,
                                      board_def->device[0].autodiscovery_def,
                                      err_msg)) {
      board_def.reset();
    } else {
      board_def->num_devices = 1;
      // Need to change the name here or before we send the string
      board_def->device[0].autodiscovery_def.name = ACL_MPSIM_DEVICE_NAME;
    }
  }

  if (!acl_platform.offline_device.empty()) {
    unsigned int board_count = 1;
    int device_index = 0;
    if (offline_mode == ACL_CONTEXT_OFFLINE_AND_AUTODISCOVERY) {
      // In this case, we place the offline device at the end of the device list
      // (after the autodiscovered devices).
      device_index = (int)(acl_platform.num_devices - board_count);
    }

    // If the user specified an offline device, then load it.
    // Search the shipped board defs for the device:
    for (const auto &board_def : shipped_board_defs) {
      if (!board_def.has_value())
        continue;
      if (acl_platform.offline_device !=
          board_def->device[0].autodiscovery_def.name)
        continue;

      const bool is_present = (offline_mode == ACL_CONTEXT_MPSIM);
      for (unsigned j = 0; j < board_count; j++) {
        // Bail if not present and we haven't been told to use absent devices
        if (!is_present && acl_platform.offline_device !=
                               board_def->device[0].autodiscovery_def.name)
          continue;
        // Add HW specific device definition
        acl_platform.device[device_index].def =
            board_def->device[0]; // Struct Copy
        acl_platform.device[device_index].present = is_present;
        device_index++;
      }
    }
  }
  l_show_devs("offline");
}

// Initialize acl_platform with device information.
// Also determine global mem address range.
static void l_initialize_devices(const acl_system_def_t *present_board_def,
                                 int offline_mode, unsigned int num_devices,
                                 const cl_device_id *devices) {
  unsigned int i, j;
  acl_assert_locked();

  acl_print_debug_msg("\n\nReset device list:   %d\n", offline_mode);

  if (present_board_def) {
    acl_print_debug_msg("\n\nPresent board def:   %d\n",
                        present_board_def->num_devices);
  }

  // shipped_board_def populated earlier in l_initialize_offline_devices

  if (offline_mode == ACL_CONTEXT_OFFLINE_AND_AUTODISCOVERY ||
      offline_mode == ACL_CONTEXT_MPSIM) {
    unsigned int num_platform_devices = acl_platform.num_devices;
    if (!acl_platform.offline_device.empty() &&
        offline_mode != ACL_CONTEXT_MPSIM) {
      num_platform_devices -=
          1; // In this case there's an extra offline devices at the end of the
             // list. Do not check it.
    }

    // Then add the present devices, if any.
    if (present_board_def) { // Might be zero probed devices present
      for (i = 0; i < num_platform_devices; i++) {
        // Only add devices from the given device list
        for (j = 0; j < num_devices; j++) {
          if (&(acl_platform.device[i]) == devices[j]) {
            // Add HW specific device definition.
            // present_board_def->device[i] only gets updated in
            // acl_hal_mmd_try_devices if opened_count is 0, after which it
            // immediately increment opened_count. Therefore here we check if
            // opened_count is 1 to update device def on first device open. If
            // opened_count is greater than 1, device def would not be updated
            // and retains information from last program. This way we can avoid
            // reprogram when kernel is created with the same binary with the
            // one that is currently loaded, but in a different cl_context.
            if (acl_platform.device[i].opened_count == 1) {
              acl_platform.device[i].def =
                  present_board_def->device[i]; // Struct copy.
              acl_platform.device[i].present = 1;
            }
            break;
          }
        }
      }
    }
    l_show_devs("probed");

    // Initialize all the present devices via the HAL.
    if (present_board_def) {
      acl_platform.hal->init_device(present_board_def);
    }
  }
}

/* Intitializes the static information for the device. This is agnostic of
   Simulator or HW.*/
static void l_add_device(int idx) {
  acl_assert_locked();
  acl_print_debug_msg("adding device %d\n", idx);

  // Add the device.
  cl_device_id device = &(acl_platform.device[idx]);
  device->id = idx;
  device->platform = &acl_platform;
  acl_reset_ref_count(device);
  device->type = CL_DEVICE_TYPE_ACCELERATOR;
  device->vendor_id = 0x1172; // Altera's PCIe vendor ID.
  device->version =
      acl_platform.version; // Just use the same as the platform version.

  // Track ACDS version, must be in \d+\.\d+ form, according to OpenCL spec.
  device->driver_version = ACL_VERSION_PLAIN_FOR_DRIVER_QUERY;

  device->min_local_mem_size = 16 * 1024; // Min value for OpenCL full profile.
  device->address_bits = 64;              // Yes, our devices are 64-bit.
}

// These functions check to see if a given object is known to the system.
// acl_*_is_valid( * );
// This is simple because everything is statically allocated.

int acl_device_is_valid_ptr(cl_device_id device) {
  unsigned int i;
  acl_assert_locked();

  for (i = 0; i < acl_platform.num_devices; i++) {
    if (device == &(acl_platform.device[i])) {
      return 1;
    }
  }
  return 0;
}

int acl_kernel_is_valid_ptr(cl_kernel kernel) {
  acl_assert_locked();

#ifdef REMOVE_VALID_CHECKS
  return 1;
#else
  // This has to be fast because it's used in many fundamental APIs.
  // We can assume the id field has been set.
  if (acl_is_valid_ptr(kernel)) {
    return 1;
  }

  return 0;
#endif
}

int acl_sampler_is_valid_ptr(cl_sampler sampler) {
  acl_assert_locked();
#ifdef REMOVE_VALID_CHECKS
  return 1;
#else
  // MemObjects belong to the Platform
  if (sampler == 0) {
    return 0;
  }
  {
    // Check for valid access range, before trying to access the id field.
    const cl_sampler first_sampler = &(acl_platform.sampler[0]);
    const cl_sampler last_sampler =
        &(acl_platform.sampler[ACL_MAX_SAMPLER - 1]);
    int id;
    if ((first_sampler - sampler) > 0)
      return 0;
    if ((sampler - last_sampler) > 0)
      return 0;
    id = sampler->id;
    if (id < 0 || id >= ACL_MAX_SAMPLER) {
      return 0;
    }
    if (sampler == &(acl_platform.sampler[id])) {
      return 1;
    }
  }
  return 0;
#endif
}

int acl_pipe_is_valid_pointer(cl_mem mem_obj, cl_kernel kernel) {
  acl_assert_locked();

  if (mem_obj == 0) {
    return 0;
  }
  {
    for (const auto &pipe : kernel->program->context->pipe_vec) {
      if (pipe == mem_obj) {
        assert((mem_obj)->mem_object_type == CL_MEM_OBJECT_PIPE);
        return 1;
      }
    }
  }
  return 0;
}

void acl_release_leaked_objects(void) {
  acl_assert_locked();

  if (acl_platform.track_leaked_objects) {
    acl_cl_object_node_t *node = acl_platform.cl_obj_head;
    while (node) {
      acl_cl_object_node_t *next = node->next;

      switch (node->type) {
      case ACL_OBJ_CONTEXT:
        clReleaseContext((cl_context)(node->object));
        break;
      case ACL_OBJ_MEM_OBJECT:
        clReleaseMemObject((cl_mem)(node->object));
        break;
      case ACL_OBJ_PROGRAM:
        clReleaseProgram((cl_program)(node->object));
        break;
      case ACL_OBJ_KERNEL:
        clReleaseKernel((cl_kernel)(node->object));
        break;
      case ACL_OBJ_COMMAND_QUEUE:
        clReleaseCommandQueue((cl_command_queue)(node->object));
        break;
      case ACL_OBJ_EVENT:
        clReleaseEvent((cl_event)(node->object));
        break;
      }
      acl_free(node);

      node = next;
    }
    acl_platform.cl_obj_head = 0;
  }
}

void acl_track_object(acl_cl_object_type_t type, void *object) {
  acl_assert_locked();

  if (acl_platform.track_leaked_objects) {
    acl_cl_object_node_t *node =
        (acl_cl_object_node_t *)acl_malloc(sizeof(acl_cl_object_node_t));
    if (node) {
      node->type = type;
      node->object = object;
      node->next = acl_platform.cl_obj_head;
      acl_platform.cl_obj_head = node;
    }
  }
}

void acl_untrack_object(void *object) {
  acl_assert_locked();

  if (acl_platform.track_leaked_objects) {
    acl_cl_object_node_t *node;
    acl_cl_object_node_t **referrer = &(acl_platform.cl_obj_head);
    while ((node = *referrer) != NULL) {
      if (node->object == object) {
        *referrer = node->next;
        acl_free(node);
        return;
      }
      referrer = &(node->next);
    }
  }
}

void acl_receive_device_exception(unsigned physical_device_id,
                                  CL_EXCEPTION_TYPE_INTEL exception_type,
                                  void *user_private_info, size_t user_cb) {
  // This function can potentially be called by a HAL that does not use the
  // ACL global lock, so we need to use acl_lock() instead of
  // acl_assert_locked(). However, the MMD HAL calls this function from a unix
  // signal handler, which can't lock mutexes, so we don't lock in that case.
  // All functions called from this one therefore have to use
  // acl_assert_locked_or_sig() instead of just acl_assert_locked().
  CL_EXCEPTION_TYPE_INTEL current_exception, listen_mask;

  std::unique_lock lock{acl_mutex_wrapper, std::defer_lock};
  if (!acl_is_inside_sig()) {
    lock.lock();
  }
  current_exception =
      acl_platform.device[physical_device_id].device_exception_status;
  listen_mask = acl_platform.device[physical_device_id].listen_mask;

  acl_platform.device[physical_device_id].device_exception_status =
      listen_mask & (current_exception | exception_type);

  // Provide private_info and cb to a user
  if (user_private_info && (listen_mask & exception_type)) {
    int exception_number = 0;
    CL_EXCEPTION_TYPE_INTEL exception_type_shifter = exception_type;
    for (unsigned int i = 0; i < sizeof(CL_EXCEPTION_TYPE_INTEL) * 8; ++i) {
      exception_type_shifter >>= 1;
      if (exception_type_shifter == 0)
        break;
      exception_number += 1;
    }
    if (exception_number < 64) {
      if (acl_platform.device[physical_device_id]
              .exception_private_info[exception_number]) {
        free(acl_platform.device[physical_device_id]
                 .exception_private_info[exception_number]);
        acl_platform.device[physical_device_id]
            .exception_private_info[exception_number] = NULL;
        acl_platform.device[physical_device_id].exception_cb[exception_number] =
            0;
      }

      acl_platform.device[physical_device_id]
          .exception_private_info[exception_number] = malloc(user_cb);
      memcpy(acl_platform.device[physical_device_id]
                 .exception_private_info[exception_number],
             user_private_info, user_cb);
      acl_platform.device[physical_device_id].exception_cb[exception_number] =
          user_cb;
    }
  }

  // Platform counter counts how many devices have at least one exception
  // Increment only if a device didn't have any exceptions at first, and this
  // exception wasn't filtered by listen_mask
  if (!current_exception &&
      acl_platform.device[physical_device_id].device_exception_status) {
    acl_platform.device_exception_platform_counter += 1;
  }

  if (listen_mask & exception_type) {
    // Signal all waiters so that acl_idle_update() calls user's exception
    // callback
    acl_signal_device_update();
  }
}

ACL_EXPORT
CL_API_ENTRY void CL_API_CALL
clTrackLiveObjectsIntelFPGA(cl_platform_id platform) {
  std::scoped_lock lock{acl_mutex_wrapper};

  if (platform == &acl_platform) {
    acl_platform.track_leaked_objects = 1;
  }
}

ACL_EXPORT
CL_API_ENTRY void CL_API_CALL clReportLiveObjectsIntelFPGA(
    cl_platform_id platform,
    void(CL_CALLBACK *report_fn)(void *, void *, const char *, cl_uint),
    void *user_data) {
  std::scoped_lock lock{acl_mutex_wrapper};

  if (platform == &acl_platform) {
    acl_cl_object_node_t *node = acl_platform.cl_obj_head;
    while (node) {
      acl_cl_object_node_t *next = node->next;
      const char *name = "<none!>";
      cl_uint refcount = 0;

      switch (node->type) {
      case ACL_OBJ_CONTEXT:
        name = "cl_context";
        refcount = acl_ref_count((cl_context)node->object);
        break;
      case ACL_OBJ_MEM_OBJECT:
        name = "cl_mem";
        refcount = acl_ref_count((cl_mem)node->object);
        break;
      case ACL_OBJ_PROGRAM:
        name = "cl_program";
        refcount = acl_ref_count((cl_program)node->object);
        break;
      case ACL_OBJ_KERNEL:
        name = "cl_kernel";
        refcount = acl_ref_count((cl_kernel)node->object);
        break;
      case ACL_OBJ_COMMAND_QUEUE:
        name = "cl_command_queue";
        refcount = acl_ref_count((cl_command_queue)node->object);
        break;
      case ACL_OBJ_EVENT:
        name = "cl_event";
        refcount = acl_ref_count((cl_event)node->object);
        break;
      }
      if (report_fn) {
        void *object = node->object;
        {
          acl_suspend_lock_guard lock{acl_mutex_wrapper};
          report_fn(user_data, object, name, refcount);
        }
      }

      node = next;
    }
  }
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif
