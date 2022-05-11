// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <assert.h>
#include <string.h>

// External library headers.
#include <CL/opencl.h>

// Internal headers.
#include <acl.h>
#include <acl_globals.h>
#include <acl_mem.h>
#include <acl_support.h>
#include <acl_thread.h>
#include <acl_types.h>
#include <acl_util.h>
#include <acl_visibility.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

size_t l_get_device_official_name(unsigned int physical_device_id, char *name,
                                  size_t size, const char *raw_name);
size_t l_get_device_vendor_name(unsigned int physical_device_id, char *name,
                                size_t size);
static cl_int l_get_device_core_temperature(unsigned int physical_device_id);

//////////////////////////////
// OpenCL API

// Return info devices available to the user.
// **Include predefined devices like DMA**
// Can be used to query the number of available devices, or to get their
// ids, or both.
ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetDeviceIDsIntelFPGA(
    cl_platform_id platform, cl_device_type device_type, cl_uint num_entries,
    cl_device_id *devices, cl_uint *num_devices) {
  cl_int status = CL_SUCCESS;
  cl_uint num_matched = 0;
  acl_lock();

  if (!acl_platform_is_valid(platform)) {
    UNLOCK_RETURN(CL_INVALID_PLATFORM);
  }
  UNLOCK_VALIDATE_ARRAY_OUT_ARGS(num_entries, devices, num_devices, 0);

  switch (device_type) {
  case CL_DEVICE_TYPE_CPU:
  case CL_DEVICE_TYPE_GPU:
    // We don't have these.
    // At least, we don't support sending Kernels to the host CPU.
    break;

  case CL_DEVICE_TYPE_ALL:
  case CL_DEVICE_TYPE_DEFAULT:
  case CL_DEVICE_TYPE_ACCELERATOR:
  case 0xFFFFFFFFFFFFFFFF: {
    unsigned int idev;
    // Include the predefined devices.
    for (idev = 0; idev < acl_platform.num_devices; idev++) {
      if (devices && num_matched < num_entries) {
        devices[num_matched] = &(acl_platform.device[idev]);
      }
      num_matched++;
    }
  } break;

  default:
    UNLOCK_RETURN(CL_INVALID_DEVICE_TYPE);
    break;
  }

  if (num_matched == 0) {
    status = CL_DEVICE_NOT_FOUND;
  }
  if (num_devices) {
    *num_devices = num_matched;
  }

  UNLOCK_RETURN(status);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetDeviceIDs(cl_platform_id platform,
                                               cl_device_type device_type,
                                               cl_uint num_entries,
                                               cl_device_id *devices,
                                               cl_uint *num_devices) {
  return clGetDeviceIDsIntelFPGA(platform, device_type, num_entries, devices,
                                 num_devices);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetDeviceInfoIntelFPGA(
    cl_device_id device, cl_device_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret) {
  char name_buf[MAX_NAME_SIZE];
  acl_result_t result;
  cl_context context = 0;
  acl_lock();

#ifndef REMOVE_VALID_CHECKS
  if (!acl_device_is_valid_ptr(device)) {
    UNLOCK_RETURN(CL_INVALID_DEVICE);
  }
  UNLOCK_VALIDATE_ARRAY_OUT_ARGS(param_value_size, param_value,
                                 param_value_size_ret, 0);
#endif

  RESULT_INIT;

  // For these properties, the device needs to have been opened before (but not
  // necessarily be currently opened) In case the device has never been opened
  // before, we need to open it by
  //  creating a context, then read the info, then release the context after.
  //  This step is to create a context. If that fails, then just return the
  //  error status.
  if (!device->has_been_opened) {
    cl_int status;
    switch (param_name) {
    case CL_DEVICE_ENDIAN_LITTLE:
    case CL_DEVICE_GLOBAL_MEM_SIZE:
    case CL_DEVICE_LOCAL_MEM_SIZE:
    case CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE:
    case CL_DEVICE_MAX_MEM_ALLOC_SIZE:
    case CL_DEVICE_VENDOR:
      context = clCreateContext(0, 1, &device, NULL, NULL, &status);
      if (status != CL_SUCCESS) {
        UNLOCK_RETURN(status);
      }
      break;
    }
  }
  // else, the devices has been opened before, we just need to read the info. No
  // extra work (e.g. creating the context) needed

  // For these properties, the device needs to be currently opened.
  // In case the device is not currently opened, we need to open it by
  //  creating a context, then read the info, then release the context after.
  //  This step is to create a context. If that fails, then just return the
  //  error status.
  //    - CL_DEVICE_AVAILABLE is a special case, we confirm we can open the
  //    device,
  //      but if we fail we simply set the result to 0, not returning an error
  std::string builtinkernels;
  if (!device->opened_count) {
    cl_int status;
    switch (param_name) {
    case CL_DEVICE_NAME:
    case CL_DEVICE_CORE_TEMPERATURE_INTELFPGA:
    case CL_DEVICE_SVM_CAPABILITIES:
    case CL_DEVICE_AVAILABLE:
      context = clCreateContext(0, 1, &device, NULL, NULL, &status);
      if (status != CL_SUCCESS) {
        if (param_name == CL_DEVICE_AVAILABLE) { // special case
          RESULT_BOOL(0);                        // it must not be available
        } else {
          UNLOCK_RETURN(status);
        }
      } else if (param_name == CL_DEVICE_AVAILABLE) {
        RESULT_BOOL(
            1); // successfully create a context means device is available
      }
      break;
    }
  } else { // the device is currently opened. No extra work (e.g. creating the
           // context) needed
    // For CL_DEVICE_AVAILABLE, if the device is currently opened, then the
    // device is available.
    if (param_name == CL_DEVICE_AVAILABLE) {
      RESULT_BOOL(1);
    }
    // For other properties, we just need to read the info.
  }

  // Read the info and set the result value
  switch (param_name) {
  case CL_DEVICE_ADDRESS_BITS:
    RESULT_UINT(device->address_bits);
    break;
  // Assume all devices are EMBEDDED_PROFILE, with no kernel compiler
  case CL_DEVICE_COMPILER_AVAILABLE:
  case CL_DEVICE_LINKER_AVAILABLE:
    RESULT_BOOL(0);
    break;
  case CL_DEVICE_ENDIAN_LITTLE:
    RESULT_BOOL(device->def.autodiscovery_def.is_big_endian ? (cl_bool)CL_FALSE
                                                            : (cl_bool)CL_TRUE);
    break;
  case CL_DEVICE_ERROR_CORRECTION_SUPPORT:
    RESULT_BOOL(0);
    break;

  case CL_DEVICE_EXTENSIONS:
    RESULT_STR(acl_platform.extensions);
    break;

  case CL_DEVICE_GLOBAL_MEM_CACHE_TYPE:
    RESULT_INT(CL_READ_ONLY_CACHE);
    break;
  case CL_DEVICE_GLOBAL_MEM_CACHE_SIZE:
    RESULT_ULONG(32768);
    break;
  case CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE:
    RESULT_INT(0);
    break;
  case CL_DEVICE_GLOBAL_MEM_SIZE: {
    auto gmem_id =
        static_cast<size_t>(acl_get_default_device_global_memory(device->def));
    cl_ulong size =
        ACL_RANGE_SIZE(device->def.autodiscovery_def.global_mem_defs[gmem_id]
                           .get_usable_range());
#ifdef __arm__
    // on SoC board, two DDR systems are not equivalent
    // so only half can be accessed with a single alloc.
    size /= 2;
#endif
    RESULT_ULONG(size);
    break;
  }

  case CL_DEVICE_IMAGE_SUPPORT:
#if ACL_SUPPORT_IMAGES == 1
    RESULT_BOOL(CL_TRUE);
#else
    RESULT_BOOL(CL_FALSE);
#endif
    break;

  case CL_DEVICE_LOCAL_MEM_TYPE:
    RESULT_ENUM(CL_LOCAL);
    break;
  case CL_DEVICE_LOCAL_MEM_SIZE:
    RESULT_ULONG(device->min_local_mem_size);
    break;

  case CL_DEVICE_MAX_CLOCK_FREQUENCY:
    RESULT_INT(1000);
    break; // A gigahertz...
  case CL_DEVICE_MAX_COMPUTE_UNITS:
    RESULT_INT(acl_platform.max_compute_units);
    break;
  case CL_DEVICE_MAX_CONSTANT_ARGS:
    RESULT_UINT(acl_platform.max_constant_args);
    break;

  // "desktop" profile says global memory must be at least 128MB
  // "embedded" profile says global memory must be at least 1MB
  case CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE: {
    // Constant memory is global memory.
    // However conformance_test_api min_max_constant_buffer_size
    // expects to allocate two buffers of the size we say here.
    // So be a shade conservative and cut it down by 4.
    auto gmem_id =
        static_cast<size_t>(acl_get_default_device_global_memory(device->def));
    cl_ulong size =
        ACL_RANGE_SIZE(device->def.autodiscovery_def.global_mem_defs[gmem_id]
                           .get_usable_range()) /
        4;
#ifdef __arm__
    // see above
    size /= 2;
#endif
    RESULT_ULONG(size);
  } break;
  case CL_DEVICE_MAX_MEM_ALLOC_SIZE: {
    auto gmem_id =
        static_cast<size_t>(acl_get_default_device_global_memory(device->def));
    cl_ulong size =
        ACL_RANGE_SIZE(device->def.autodiscovery_def.global_mem_defs[gmem_id]
                           .get_usable_range());
#ifdef __arm__
    // on SoC board, two DDR systems are not equivalent
    // so only half can be accessed with a single alloc.

    // If user uses only device memory on 2DDR board, they can use size/2 mem
    const char *override =
        acl_getenv("CL_DEVICE_DISABLE_ARM_MEMSIZE_SAFEGUARD");
    if (override) {
      size = size / 2;
    } else {
      size = size / 8;
    }
#endif
    RESULT_ULONG(size);
  } break;

  case CL_DEVICE_MAX_PARAMETER_SIZE:
    RESULT_SIZE_T(acl_platform.max_param_size);
    break;
  // Constant memory is global memory, so just consider all of global mem.
  case CL_DEVICE_MAX_WORK_GROUP_SIZE:
    RESULT_SIZE_T(acl_platform.max_work_group_size);
    break;
  case CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS:
    RESULT_INT(acl_platform.max_work_item_dimensions);
    break;
  case CL_DEVICE_MAX_WORK_ITEM_SIZES:
    RESULT_SIZE_T3(acl_platform.max_work_item_sizes,
                   acl_platform.max_work_item_sizes,
                   acl_platform.max_work_item_sizes);
    break;
  case CL_DEVICE_MEM_BASE_ADDR_ALIGN:
    RESULT_UINT(acl_platform.mem_base_addr_align);
    break;
  case CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE:
    RESULT_INT(acl_platform.min_data_type_align_size);
    break;
  case CL_DEVICE_NAME:
    l_get_device_official_name(device->def.physical_device_id, name_buf,
                               MAX_NAME_SIZE,
                               device->def.autodiscovery_def.name.c_str());
    RESULT_STR(name_buf);
    break;
  case CL_DEVICE_BUILT_IN_KERNELS: {
    for (acl_accel_def_t accel : device->def.autodiscovery_def.accel) {
      builtinkernels.append(accel.iface.name);
      builtinkernels.append(";");
    }
    if (builtinkernels.length() > 0) {
      builtinkernels.pop_back();
    } // remove trailing semicolon
    RESULT_STR(builtinkernels.c_str());
  } break;
  case CL_DEVICE_PLATFORM:
    RESULT_PTR(&acl_platform);
    break;
  case CL_DEVICE_PRINTF_BUFFER_SIZE:
    RESULT_SIZE_T(acl_platform.printf_buffer_size);
    break;
  case CL_DEVICE_PREFERRED_INTEROP_USER_SYNC:
    RESULT_BOOL(CL_TRUE);
    break;
  case CL_DEVICE_PARENT_DEVICE:
    RESULT_PTR(NULL);
    break;
  case CL_DEVICE_PARTITION_MAX_SUB_DEVICES:
    RESULT_UINT(0);
    break;
  case CL_DEVICE_PARTITION_PROPERTIES:
    RESULT_INT(0);
    break;
  case CL_DEVICE_PARTITION_AFFINITY_DOMAIN:
    RESULT_BITFIELD(0);
    break;
  case CL_DEVICE_PARTITION_TYPE:
    RESULT_INT(0);
    break;
  case CL_DEVICE_REFERENCE_COUNT:
    RESULT_UINT(1);
    break;
  // Assume 32 bits is good
  case CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR:
    RESULT_INT(4);
    break;
  case CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT:
    RESULT_INT(2);
    break;
  case CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT:
    RESULT_INT(1);
    break;
  case CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG:
    RESULT_INT(1);
    break;
  case CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT:
    RESULT_INT(1);
    break;
  case CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE:
    RESULT_INT(ACL_SUPPORT_DOUBLE != 0);
    break; // We support cl_khr_fp64
  case CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF:
    RESULT_INT(0);
    break; // We don't support half
  case CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR:
    RESULT_INT(4);
    break;
  case CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT:
    RESULT_INT(2);
    break;
  case CL_DEVICE_NATIVE_VECTOR_WIDTH_INT:
    RESULT_INT(1);
    break;
  case CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG:
    RESULT_INT(1);
    break;
  case CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT:
    RESULT_INT(1);
    break;
  case CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE:
    RESULT_INT(ACL_SUPPORT_DOUBLE != 0);
    break; // We support cl_khr_fp64
  case CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF:
    RESULT_INT(0);
    break; // We don't support half

  case CL_DEVICE_PROFILE:
    RESULT_STR("EMBEDDED_PROFILE");
    break; // no kernel compiler
  case CL_DEVICE_VENDOR:
    l_get_device_vendor_name(device->def.physical_device_id, name_buf,
                             MAX_NAME_SIZE);
    RESULT_STR(name_buf);
    break;

  case CL_DEVICE_PROFILING_TIMER_RESOLUTION:
    RESULT_SIZE_T(1);
    break; // guess
  case CL_DEVICE_MAX_SAMPLERS:
    RESULT_INT(ACL_MAX_SAMPLER);
    break;
  case CL_DEVICE_HALF_FP_CONFIG:
    RESULT_INT(0);
    break;
  case CL_DEVICE_EXECUTION_CAPABILITIES:
    RESULT_BITFIELD(CL_EXEC_KERNEL);
    break;

  // CL_DEVICE_QUEUE_ON_HOST_PROPERTIES replaces CL_DEVICE_QUEUE_PROPERTIES in
  // OpenCL 2.0. Both macros are the same value so this is backwards compatible.
  case CL_DEVICE_QUEUE_ON_HOST_PROPERTIES:
    RESULT_BITFIELD(acl_platform.queue_properties);
    break;
  case CL_DEVICE_SINGLE_FP_CONFIG:
    RESULT_BITFIELD(acl_platform.single_fp_config);
    break;
  case CL_DEVICE_DOUBLE_FP_CONFIG:
    RESULT_BITFIELD(acl_platform.double_fp_config);
    break;
  case CL_DEVICE_TYPE:
    RESULT_BITFIELD(device->type);
    break;
  case CL_DEVICE_VENDOR_ID:
    RESULT_UINT(device->vendor_id);
    break;
  case CL_DEVICE_VERSION:
    RESULT_STR(device->version);
    break;
  case CL_DRIVER_VERSION:
    RESULT_STR(device->driver_version);
    break;

  case CL_DEVICE_HOST_UNIFIED_MEMORY:
    RESULT_BOOL(0);
    break;
  case CL_DEVICE_OPENCL_C_VERSION:
    // The OpenCL 1.2 api conformance test forces there to be a space after the
    // version number.
#ifdef ACL_120
    RESULT_STR("OpenCL C 1.2 ");
    break;
#else
    RESULT_STR("OpenCL C 1.0 ");
    break;
#endif
  // Image stuff. Conformance tests still query these
  case CL_DEVICE_MAX_READ_IMAGE_ARGS:
  case CL_DEVICE_MAX_WRITE_IMAGE_ARGS: // This option is only for backward
                                       // compatibility, MAX_READ_WRITE should
                                       // be used instead.
  case CL_DEVICE_MAX_READ_WRITE_IMAGE_ARGS:
    RESULT_INT(128);
    break;
  case CL_DEVICE_IMAGE2D_MAX_WIDTH:
  case CL_DEVICE_IMAGE2D_MAX_HEIGHT:
    RESULT_SIZE_T(16384);
    break;
  case CL_DEVICE_IMAGE3D_MAX_WIDTH:
  case CL_DEVICE_IMAGE3D_MAX_HEIGHT:
  case CL_DEVICE_IMAGE3D_MAX_DEPTH:
    RESULT_SIZE_T(2048);
    break;
  case CL_DEVICE_IMAGE_MAX_BUFFER_SIZE:
    RESULT_SIZE_T(65536);
    break; // Minimum is 65536.
  case CL_DEVICE_IMAGE_MAX_ARRAY_SIZE:
    RESULT_SIZE_T(2048);
    break;
  case CL_DEVICE_IMAGE_PITCH_ALIGNMENT:
    RESULT_UINT(2);
    break; // must be a power of 2.
  case CL_DEVICE_IMAGE_BASE_ADDRESS_ALIGNMENT:
    RESULT_UINT(acl_platform.mem_base_addr_align);
    break;
  case CL_DEVICE_CORE_TEMPERATURE_INTELFPGA:
    RESULT_INT(l_get_device_core_temperature(device->def.physical_device_id));
    break;

  // Returns a bit field listing all of the supported types of SVM memory
  // suppoted by this device
  case CL_DEVICE_SVM_CAPABILITIES: {
    int memories_supported;
    acl_get_hal()->has_svm_memory_support(device->def.physical_device_id,
                                          &memories_supported);
    RESULT_INT(memories_supported);
  } break;
  case CL_DEVICE_PREFERRED_PLATFORM_ATOMIC_ALIGNMENT:
    RESULT_UINT(0);
    break; // Aligned to the natural size of the type
  case CL_DEVICE_PREFERRED_GLOBAL_ATOMIC_ALIGNMENT:
    RESULT_UINT(0);
    break; // Aligned to the natural size of the type
  case CL_DEVICE_PREFERRED_LOCAL_ATOMIC_ALIGNMENT:
    RESULT_UINT(0);
    break; // Aligned to the natural size of the type
  case CL_DEVICE_MAX_PIPE_ARGS:
    RESULT_UINT(acl_platform.max_pipe_args);
    break;
  case CL_DEVICE_PIPE_MAX_ACTIVE_RESERVATIONS:
    RESULT_UINT(acl_platform.pipe_max_active_reservations);
    break;
  case CL_DEVICE_PIPE_MAX_PACKET_SIZE:
    RESULT_UINT(acl_platform.pipe_max_packet_size);
    break;

  // USM properties
  case CL_DEVICE_HOST_MEM_CAPABILITIES_INTEL:
  case CL_DEVICE_DEVICE_MEM_CAPABILITIES_INTEL:
  case CL_DEVICE_SINGLE_DEVICE_SHARED_MEM_CAPABILITIES_INTEL: {
    unsigned int capabilities;
    if (param_name == CL_DEVICE_HOST_MEM_CAPABILITIES_INTEL) {
      capabilities = device->def.host_capabilities;
    } else if (param_name == CL_DEVICE_DEVICE_MEM_CAPABILITIES_INTEL) {
      // Device allocations are supported for all legacy devices
      // Device allocations managed by runtime in legacy devices
      capabilities = ACL_MEM_CAPABILITY_SUPPORTED;
    } else {
      capabilities = device->def.shared_capabilities;
    }

    bool supported = capabilities & ACL_MEM_CAPABILITY_SUPPORTED;
    bool concurrent = capabilities & ACL_MEM_CAPABILITY_CONCURRENT;
    bool atomic = capabilities & ACL_MEM_CAPABILITY_ATOMIC;

    if (!supported) {
      RESULT_BITFIELD(0);
    }
    if (atomic && concurrent) {
      RESULT_BITFIELD(CL_UNIFIED_SHARED_MEMORY_CONCURRENT_ATOMIC_ACCESS_INTEL);
    }
    if (concurrent) {
      RESULT_BITFIELD(CL_UNIFIED_SHARED_MEMORY_CONCURRENT_ACCESS_INTEL);
    }
    if (atomic) {
      RESULT_BITFIELD(CL_UNIFIED_SHARED_MEMORY_ATOMIC_ACCESS_INTEL);
    }
    if (supported) {
      RESULT_BITFIELD(CL_UNIFIED_SHARED_MEMORY_ACCESS_INTEL);
    }
  } break;
  case CL_DEVICE_CROSS_DEVICE_SHARED_MEM_CAPABILITIES_INTEL: {
    RESULT_BITFIELD(0);
  } break;

  case CL_DEVICE_SHARED_SYSTEM_MEM_CAPABILITIES_INTEL: {
    RESULT_BITFIELD(0);
  } break;

  default:
    break;
  }
  if (context) {
    clReleaseContext(context);
  }

  if (result.size == 0) {
    // We didn't implement the enum. Error out semi-gracefully.
    UNLOCK_RETURN(CL_INVALID_VALUE);
  }

  if (param_value) {
    // Actually try to return the string.
    if (param_value_size < result.size) {
      // Buffer is too small to hold the return value.
      UNLOCK_RETURN(CL_INVALID_VALUE);
    }
    RESULT_COPY(param_value, param_value_size);
  }

  if (param_value_size_ret) {
    *param_value_size_ret = result.size;
  }
  UNLOCK_RETURN(CL_SUCCESS);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetDeviceInfo(cl_device_id device,
                                                cl_device_info param_name,
                                                size_t param_value_size,
                                                void *param_value,
                                                size_t *param_value_size_ret) {
  return clGetDeviceInfoIntelFPGA(device, param_name, param_value_size,
                                  param_value, param_value_size_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clCreateSubDevicesIntelFPGA(
    cl_device_id in_device,
    const cl_device_partition_property *partition_properties,
    cl_uint num_entries, cl_device_id *out_devices, cl_uint *num_devices) {
  // Spec says:
  // clCreateSubDevices returns CL_SUCCESS if the partition is created
  // successfully. Otherwise, it returns a NULL value with the following error
  // values returned in errcode_ret:
  // - CL_INVALID_DEVICE if in_device is not valid.
  // - CL_INVALID_VALUE if values specified in properties are not valid or if
  // values specified in properties are valid but not supported by the device.
  //  etc.

  // Since we don't support creating sub devices, we should follow the first
  // case if in_device is not valid, and the second case if it is.

  acl_lock();
  // Suppress compiler warnings.
  partition_properties = partition_properties;
  num_entries = num_entries;
  out_devices = out_devices;
  num_devices = num_devices;

  if (!acl_device_is_valid(in_device)) {
    UNLOCK_RETURN(CL_INVALID_DEVICE);
  }

  UNLOCK_RETURN(CL_INVALID_VALUE);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clCreateSubDevices(
    cl_device_id in_device,
    const cl_device_partition_property *partition_properties,
    cl_uint num_entries, cl_device_id *out_devices, cl_uint *num_devices) {
  return clCreateSubDevicesIntelFPGA(in_device, partition_properties,
                                     num_entries, out_devices, num_devices);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainDeviceIntelFPGA(cl_device_id device) {
  acl_lock();

  // Spec says:
  // "increments the device reference count if device is a valid sub-device
  // created by a call to
  //  clCreateSubDevices. If device is a root level device i.e. a cl_device_id
  //  returned by clGetDeviceIDs, the device reference count remains unchanged.
  //  clRetainDevice returns CL_SUCCESS if the function is executed successfully
  //  or the device is a root-level device. Otherwise, it returns one of the
  //  following errors:
  //  - CL_INVALID_DEVICE if device is not a valid sub-device created by a call
  //  to
  //    clCreateSubDevices."
  //
  // It appears to contradict itself (?):
  // "clRetainDevice returns CL_SUCCESS if ... the device is a root-level
  // device."
  //   and
  // "CL_INVALID_DEVICE if device is not a valid sub-device"
  //
  // But we'll go with the first statement for root-level devices (return
  // CL_SUCCESS and do nothing)

  // Since we don't (currently) support sub-devices, valid devices must be
  // root-level:
  if (acl_device_is_valid(device)) {
    UNLOCK_RETURN(CL_SUCCESS);
  } else {
    UNLOCK_RETURN(CL_INVALID_DEVICE);
  }
}
ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainDevice(cl_device_id device) {
  return clRetainDeviceIntelFPGA(device);
}

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL
clReleaseDeviceIntelFPGA(cl_device_id device) {
  acl_lock();

  // Spec says:
  // "decrements the device reference count if device is a valid sub-device
  // created by a call to
  //  clCreateSubDevices. If device is a root level device i.e. a cl_device_id
  //  returned by clGetDeviceIDs, the device reference count remains unchanged.
  //  clReleaseDevice returns CL_SUCCESS if the function is executed
  //  successfully. Otherwise, it returns one of the following errors:
  //  - CL_INVALID_DEVICE if device is not a valid sub-device created by a call
  //  to
  //    clCreateSubDevices."
  //
  // As with clRetainDevice, we'll go with the interpretation that for
  // root-level devices we return CL_SUCCESS and do nothing

  // Since we don't (currently) support sub-devices, valid devices must be
  // root-level:
  if (acl_device_is_valid(device)) {
    UNLOCK_RETURN(CL_SUCCESS);
  } else {
    UNLOCK_RETURN(CL_INVALID_DEVICE);
  }
}

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL
clReleaseDevice(cl_device_id device) {
  return clReleaseDeviceIntelFPGA(device);
}

ACL_EXPORT CL_API_ENTRY cl_int
clReconfigurePLLIntelFPGA(cl_device_id device, const char *pll_settings_str) {
  // To get the format of the second string argument please refer to the code
  // comments specified for struct pll_setting_t in include/acl_pll.
  const acl_hal_t *hal;
  cl_int configure_status;
  acl_lock();

  if (!acl_device_is_valid(device)) {
    UNLOCK_RETURN(CL_INVALID_DEVICE);
  }
  if (!pll_settings_str) {
    UNLOCK_RETURN(CL_INVALID_VALUE);
  }

  hal = acl_get_hal();
  configure_status =
      hal->pll_reconfigure(device->def.physical_device_id, pll_settings_str);
  if (configure_status == 0)
    UNLOCK_RETURN(CL_SUCCESS);
  else
    UNLOCK_RETURN(CL_INVALID_OPERATION);
}

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL clSetDeviceExceptionCallback(
    cl_uint num_devices, const cl_device_id *devices,
    CL_EXCEPTION_TYPE_INTEL listen_mask,
    acl_exception_notify_fn_t pfn_exception_notify, void *user_data) {
  return clSetDeviceExceptionCallbackIntelFPGA(
      num_devices, devices, listen_mask, pfn_exception_notify, user_data);
}

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL
clSetDeviceExceptionCallbackIntelFPGA(
    cl_uint num_devices, const cl_device_id *devices,
    CL_EXCEPTION_TYPE_INTEL listen_mask,
    acl_exception_notify_fn_t pfn_exception_notify, void *user_data) {
  unsigned i;

  acl_lock();

  if (!pfn_exception_notify)
    UNLOCK_RETURN(CL_INVALID_VALUE);
  if (!listen_mask)
    UNLOCK_RETURN(CL_INVALID_VALUE);
  if (!devices && num_devices > 0)
    UNLOCK_RETURN(CL_INVALID_VALUE);
  if (devices && num_devices == 0)
    UNLOCK_RETURN(CL_INVALID_VALUE);

  for (i = 0; i < num_devices; ++i) {
    devices[i]->exception_notify_fn = pfn_exception_notify;
    devices[i]->exception_notify_user_data = user_data;
    devices[i]->listen_mask = listen_mask;
  }

  UNLOCK_RETURN(CL_SUCCESS);
}

//////////////////////////////
// Internals

int acl_device_is_valid(cl_device_id device) {
  acl_assert_locked();

  if (!acl_device_is_valid_ptr(device)) {
    return 0;
  }

  return 1;
}

size_t l_get_device_official_name(unsigned int physical_device_id, char *name,
                                  size_t size, const char *raw_name) {
  const acl_hal_t *hal;
  size_t raw_name_strlen;
  acl_assert_locked();

  hal = acl_get_hal();
  assert(hal);

  if (size == 0)
    return 0;

  raw_name_strlen = strnlen(raw_name, MAX_NAME_SIZE);
  strncpy(name, raw_name, raw_name_strlen);
  name[raw_name_strlen] = '\0';

  if (hal->get_device_official_name == NULL)
    return raw_name_strlen;

  if (size > raw_name_strlen + 4) {
    strncat(name, " : ", size - raw_name_strlen - 1);
    raw_name_strlen += 3;
  }
  if (size > raw_name_strlen)
    hal->get_device_official_name(physical_device_id, &name[raw_name_strlen],
                                  size - raw_name_strlen);
  return strnlen(name, MAX_NAME_SIZE);
}

size_t l_get_device_vendor_name(unsigned int physical_device_id, char *name,
                                size_t size) {
  const acl_hal_t *hal;
  acl_assert_locked();

  hal = acl_get_hal();
  assert(hal);

  if (size == 0)
    return 0;

  if (hal->get_device_vendor_name == NULL) {
    strncpy(name, "Unknown", size);
    name[size - 1] = '\0';
  } else
    hal->get_device_vendor_name(physical_device_id, name, size);
  return strnlen(name, MAX_NAME_SIZE);
}

// ACL utility to get on-chip temperature.  Only the PCIE HAL does anything
// during a call, and not all ifaces have the hardware.  Currently only SV PCIe
// returns an actual temperature (the rest return failure).
// This function is exposed to users through clGetDeviceInfo with an IntelFPGA
// extension query.  See example use in p4/tools/query_temperature
static cl_int l_get_device_core_temperature(unsigned int physical_device_id) {
  cl_bool return_val;
  cl_int temp; // Degrees C
  const acl_hal_t *hal;
  acl_assert_locked();

  hal = acl_get_hal();
  assert(hal);

  return_val = hal->query_temperature(physical_device_id, &temp);

  if (return_val) {
    return temp;
  } else {
    return 0; // 0 degrees on unsupported read
  }
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif
