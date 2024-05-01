// Copyright (C) 2020-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>

// External library headers.
#include <CL/opencl.h>

// Internal headers.
#include <acl.h>
#include <acl_context.h>
#include <acl_device_op.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_hal.h>
#include <acl_mem.h>
#include <acl_platform.h>
#include <acl_support.h>
#include <acl_svm.h>
#include <acl_types.h>
#include <acl_usm.h>
#include <acl_util.h>

#include <MMD/aocl_mmd.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// Forward declaration
static void l_add_usm_alloc_to_context(cl_context context,
                                       acl_usm_allocation_t *usm_alloc);
static void l_remove_usm_alloc_from_context(cl_context context,
                                            acl_usm_allocation_t *usm_alloc);
static cl_bool l_ptr_in_usm_alloc_range(acl_usm_allocation_t *usm_alloc,
                                        const void *dst_ptr, size_t size);
static void *l_set_dev_alloc_bit(const void *ptr);
static void l_cl_mem_blocking_free(cl_context context, void *ptr);

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL clHostMemAllocINTEL(
    cl_context context, const cl_mem_properties_intel *properties, size_t size,
    cl_uint alignment, cl_int *errcode_ret) {
  std::scoped_lock lock{acl_mutex_wrapper};
  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  if (!acl_context_is_valid(context)) {
    BAIL(CL_INVALID_CONTEXT);
  }

  if (size == 0) {
    BAIL_INFO(CL_INVALID_BUFFER_SIZE, context,
              "Memory buffer cannot be of size zero");
  }

  // Spec only allows for power of 2 allignment.
  // Alignment of '0' means use the default
  if (alignment & (alignment - 1)) {
    BAIL_INFO(CL_INVALID_VALUE, context, "alignment must be power of 2");
  }

  // Spec specifies that alignment is no bigger than the largest supported data
  // type
  if (alignment > sizeof(cl_long16)) {
    BAIL_INFO(CL_INVALID_VALUE, context,
              "Requested alignment greater than largest data type "
              "supported by device (long16)");
  }

  std::vector<cl_device_id> devices = std::vector<cl_device_id>(
      context->device, context->device + context->num_devices);
  if (alignment == 0) {
    bool same_device = true;
    // In a system of different devices use the biggest min_alignment out of the
    // lot, this guarantees that the alignment will work for all devices. If the
    // min alignment of one device is greater that the max alignment of another,
    // an error will be generated during allocation.
    for (const auto &dev : devices) {
      alignment = std::max<cl_uint>(alignment,
                                    (cl_uint)dev->def.min_host_mem_alignment);
      same_device &= dev->def.autodiscovery_def.name ==
                     devices[0]->def.autodiscovery_def.name;
    }

    if (same_device) {
      // if the context contains only the same device, let the MMD decide what
      // the default alignment should be
      alignment = 0;
    }
  }

  // Iterate over properties.
  // The end of the properties list is specified with a zero.
  cl_mem_alloc_flags_intel alloc_flags = 0;
  std::optional<cl_uint> mem_id;
  while (properties != NULL && *properties != 0) {
    switch (*properties) {
    case CL_MEM_ALLOC_FLAGS_INTEL: {
      alloc_flags = *(properties + 1);
    } break;
    case CL_MEM_ALLOC_BUFFER_LOCATION_INTEL: {
      mem_id = (cl_uint) * (properties + 1);
    } break;
    default: {
      BAIL_INFO(CL_INVALID_PROPERTY, context, "Invalid properties");
    }
    }
    properties += 2;
  }

  for (const auto &dev : devices) {
    if (!acl_usm_has_access_capability(dev,
                                       CL_DEVICE_HOST_MEM_CAPABILITIES_INTEL)) {
      BAIL_INFO(
          CL_INVALID_OPERATION, context,
          "Device does not support host Unified Shared Memory allocations: " +
              dev->def.autodiscovery_def.name);
    }
    // Ensure requested size is valid and supported by the specified device
    cl_ulong max_alloc = 0;
    cl_int ret = clGetDeviceInfo(dev, CL_DEVICE_MAX_MEM_ALLOC_SIZE,
                                 sizeof(max_alloc), &max_alloc, 0);
    if (ret) {
      BAIL_INFO(ret, context,
                "Failed to query CL_DEVICE_MAX_MEM_ALLOC_SIZE for device: " +
                    dev->def.autodiscovery_def.name);
    }
    if (size > max_alloc) {
      BAIL_INFO(CL_INVALID_BUFFER_SIZE, context,
                "Size larger than allocation size supported by device: " +
                    dev->def.autodiscovery_def.name);
    }
  }

  bool track_mem_id = false;
  if (acl_get_hal()->host_alloc) {
    std::array<mem_properties_t, 3> mmd_properties;
    {
      auto mmd_properties_it = mmd_properties.begin();
      if (mem_id) {
        if (acl_get_hal()->support_buffer_location(devices)) {
          track_mem_id = true;
          *mmd_properties_it++ = AOCL_MMD_MEM_PROPERTIES_BUFFER_LOCATION;
          *mmd_properties_it++ = *mem_id;
        }
      }
      *mmd_properties_it++ = 0;
    }

    acl_usm_allocation_t *usm_alloc =
        (acl_usm_allocation_t *)acl_malloc(sizeof(acl_usm_allocation_t));

    if (!usm_alloc) {
      BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context, "Out of host memory");
    }

    int error = 0;
    void *mem = acl_get_hal()->host_alloc(
        devices, size, alignment,
        mmd_properties[0] ? mmd_properties.data() : nullptr, &error);
    if (error) {
      acl_free(usm_alloc);
      switch (error) {
      case CL_OUT_OF_HOST_MEMORY:
        BAIL_INFO(error, context,
                  "Error: Unable to allocate " + std::to_string(size) +
                      " bytes");
        break;
      case CL_INVALID_VALUE:
        BAIL_INFO(error, context,
                  "Error: Unsupported alignment of " +
                      std::to_string(alignment));
        break;
      case CL_INVALID_PROPERTY:
        BAIL_INFO(error, context, "Error: Unsuported properties");
        break;
      default:
        BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
                  "Error: Unable to allocate memory");
        break;
      }
    }

    usm_alloc->mem = NULL;
    usm_alloc->device = NULL; // All device in context
    usm_alloc->range.begin = mem;
    usm_alloc->range.next = (void *)((size_t)mem + size);
    usm_alloc->alloc_flags = alloc_flags;
    usm_alloc->type = CL_MEM_TYPE_HOST_INTEL;
    usm_alloc->alignment = alignment;
    usm_alloc->host_shared_mem_id = 0; // Initialize to 0
    if (track_mem_id) {
      usm_alloc->host_shared_mem_id = *mem_id;
    }

    l_add_usm_alloc_to_context(context, usm_alloc);
    return mem;
  }

  BAIL_INFO(CL_INVALID_VALUE, context,
            "Host allocation is not supported for devices in this context");
}

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL
clDeviceMemAllocINTEL(cl_context context, cl_device_id device,
                      const cl_mem_properties_intel *properties, size_t size,
                      cl_uint alignment, cl_int *errcode_ret) {
  std::scoped_lock lock{acl_mutex_wrapper};

  // Valid argument check
  if (!acl_context_is_valid(context)) {
    BAIL(CL_INVALID_CONTEXT);
  }
  if (!acl_device_is_valid(device)) {
    BAIL_INFO(CL_INVALID_DEVICE, context, "Invalid device");
  }
  if (!acl_context_uses_device(context, device)) {
    BAIL_INFO(CL_INVALID_DEVICE, context,
              "Device is not associated with the context");
  }
  if (size == 0) {
    BAIL_INFO(CL_INVALID_BUFFER_SIZE, context,
              "Memory buffer cannot be of size zero");
  }

  cl_ulong max_alloc = 0;
  clGetDeviceInfo(device, CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(max_alloc),
                  &max_alloc, 0);

  if (size > max_alloc) {
    BAIL_INFO(CL_INVALID_BUFFER_SIZE, context,
              "Memory buffer size is larger than max size supported by device");
  }

  if (!acl_usm_has_access_capability(device,
                                     CL_DEVICE_DEVICE_MEM_CAPABILITIES_INTEL)) {
    BAIL_INFO(
        CL_INVALID_OPERATION, context,
        "Device does not support device Unified Shared Memory allocations: " +
            device->def.autodiscovery_def.name);
  }

  // Spec allows for power of 2 allignment.
  // For now, we only allow alignment to ACL_MEM_ALIGN.
  // Over align all allocations to ACL_MEM_ALIGN.
  // Alignment of '0' means use the default
  if (alignment == 0) {
    alignment = ACL_MEM_ALIGN;
  }
  if (alignment & (alignment - 1)) {
    BAIL_INFO(CL_INVALID_VALUE, context, "alignment must be power of 2");
  }
  if (alignment > ACL_MEM_ALIGN) {
    BAIL_INFO(CL_INVALID_VALUE, context, "Alignment value is not supported");
  }
  alignment = ACL_MEM_ALIGN;

  // Iterate over properties.
  // The end of the properties list is specified with a zero.
  cl_mem_alloc_flags_intel alloc_flags = 0;
  cl_uint mem_id = acl_get_default_memory(device->def);
  while (properties != NULL && *properties != 0) {
    switch (*properties) {
    case CL_MEM_ALLOC_FLAGS_INTEL: {
      alloc_flags = *(properties + 1);
    } break;
    case CL_MEM_ALLOC_BUFFER_LOCATION_INTEL: {
      mem_id = (cl_uint) * (properties + 1);
    } break;
    default: {
      BAIL_INFO(CL_INVALID_DEVICE, context, "Invalid properties");
    }
    }
    properties += 2;
  }

  cl_int status;

  // Use cl_mem for convenience
  cl_mem_properties_intel props[] = {CL_MEM_ALLOC_BUFFER_LOCATION_INTEL, mem_id,
                                     0};
  cl_mem usm_device_buffer = clCreateBufferWithPropertiesINTEL(
      context, props, CL_MEM_READ_WRITE, size, NULL, &status);
  if (status != CL_SUCCESS) {
    BAIL_INFO(status, context, "Failed to allocate device memory");
  }
  // Runtime will do device allocation on bind to device
  if (!acl_bind_buffer_to_device(device, usm_device_buffer)) {
    clReleaseMemObjectIntelFPGA(usm_device_buffer);
    BAIL_INFO(CL_OUT_OF_RESOURCES, context, "Failed to allocate device memory");
  }
  acl_usm_allocation_t *usm_alloc =
      (acl_usm_allocation_t *)acl_malloc(sizeof(acl_usm_allocation_t));

  if (!usm_alloc) {
    clReleaseMemObjectIntelFPGA(usm_device_buffer);
    BAIL_INFO(CL_OUT_OF_RESOURCES, context, "Out of host memory");
  }

  void *ptr = acl_get_physical_address(usm_device_buffer, device);
  ptr = l_set_dev_alloc_bit(ptr);

  usm_alloc->mem = usm_device_buffer;
  usm_alloc->device = device;
  usm_alloc->range.begin = ptr;
  usm_alloc->range.next = (void *)((size_t)ptr + size);
  usm_alloc->alloc_flags = alloc_flags;
  usm_alloc->type = CL_MEM_TYPE_DEVICE_INTEL;
  usm_alloc->alignment = alignment;

  l_add_usm_alloc_to_context(context, usm_alloc);

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  return ptr;
}

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL
clSharedMemAllocINTEL(cl_context context, cl_device_id device,
                      const cl_mem_properties_intel *properties, size_t size,
                      cl_uint alignment, cl_int *errcode_ret) {
  std::scoped_lock lock{acl_mutex_wrapper};
  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  if (!acl_context_is_valid(context)) {
    BAIL(CL_INVALID_CONTEXT);
  }
  if (device != nullptr && !acl_device_is_valid(device)) {
    BAIL_INFO(CL_INVALID_DEVICE, context, "Invalid device");
  }
  if (device != nullptr && !acl_context_uses_device(context, device)) {
    BAIL_INFO(CL_INVALID_DEVICE, context,
              "Device is not associated with the context");
  }
  if (size == 0) {
    BAIL_INFO(CL_INVALID_BUFFER_SIZE, context,
              "Allocation cannot be of size zero");
  }
  // USM spec allows only power-of-2 alignment, or 0 (default alignment)
  if (alignment & (alignment - 1)) {
    BAIL_INFO(CL_INVALID_VALUE, context, "alignment must be power of 2");
  }

  // Ensure the specified device, or at least one of the devices in the context
  // supports shared allocations.
  std::vector<cl_device_id> devices;
  if (device != nullptr) {
    devices.push_back(device);
  } else {
    devices = std::vector<cl_device_id>(context->device,
                                        context->device + context->num_devices);
  }
  for (const auto &dev : devices) {
    if (!acl_usm_has_access_capability(
            dev, CL_DEVICE_SINGLE_DEVICE_SHARED_MEM_CAPABILITIES_INTEL)) {
      BAIL_INFO(
          CL_INVALID_OPERATION, context,
          "Device does not support shared Unified Shared Memory allocations");
    }
  }

  // Spec specifies that alignment is no bigger than the largest supported data
  // type
  if (alignment > sizeof(cl_long16)) {
    BAIL_INFO(CL_INVALID_VALUE, context,
              "Requested alignment greater than largest data type "
              "supported by device (long16)");
  }

  // Ensure requested size is valid and supported by the specified device, or at
  // least one of the devices in the context.
  if (device != nullptr) {
    cl_ulong dev_alloc = 0;
    cl_int ret = clGetDeviceInfo(device, CL_DEVICE_MAX_MEM_ALLOC_SIZE,
                                 sizeof(dev_alloc), &dev_alloc, 0);
    if (ret) {
      BAIL_INFO(ret, context,
                "Failed to query CL_DEVICE_MAX_MEM_ALLOC_SIZE for device");
    }
    if (size > dev_alloc) {
      BAIL_INFO(CL_INVALID_BUFFER_SIZE, context,
                "Size larger than allocation size supported by device");
    }
  }
  if (device == nullptr && (size > context->max_mem_alloc_size)) {
    BAIL_INFO(
        CL_INVALID_BUFFER_SIZE, context,
        "Size larger than allocation size supported by any device in context");
  }

  // Iterate over properties.
  // The end of the properties list is specified with a zero.
  cl_mem_alloc_flags_intel alloc_flags = 0;
  std::unordered_set<unsigned long long> seen_flags;
  std::optional<cl_uint> mem_id;
  while (properties != NULL && *properties != 0) {
    switch (*properties) {
    case CL_MEM_ALLOC_FLAGS_INTEL: {
      if (seen_flags.insert(CL_MEM_ALLOC_FLAGS_INTEL).second == false) {
        BAIL_INFO(CL_INVALID_PROPERTY, context,
                  "Property specified multiple times");
      }
      switch (*(properties + 1)) {
      case CL_MEM_ALLOC_WRITE_COMBINED_INTEL:
        break;
      default:
        BAIL_INFO(CL_INVALID_PROPERTY, context, "Invalid value for property");
      }
      alloc_flags = *(properties + 1);
    } break;
    case CL_MEM_ALLOC_BUFFER_LOCATION_INTEL: {
      if (seen_flags.insert(CL_MEM_ALLOC_BUFFER_LOCATION_INTEL).second ==
          false) {
        BAIL_INFO(CL_INVALID_PROPERTY, context,
                  "Property specified multiple times");
      }
      mem_id = (cl_uint) * (properties + 1);
    } break;
    default: {
      BAIL_INFO(CL_INVALID_PROPERTY, context, "Invalid properties");
    }
    }
    properties += 2;
  }

  bool track_mem_id = false;
  if (acl_get_hal()->shared_alloc) {
    std::array<mem_properties_t, 3> mmd_properties;
    {
      auto mmd_properties_it = mmd_properties.begin();
      if (mem_id) {
        if (acl_get_hal()->support_buffer_location(
                std::vector<cl_device_id>{device})) {
          track_mem_id = true;
          *mmd_properties_it++ = AOCL_MMD_MEM_PROPERTIES_BUFFER_LOCATION;
          *mmd_properties_it++ = *mem_id;
        }
      }
      *mmd_properties_it++ = 0;
    }

    acl_usm_allocation_t *usm_alloc =
        (acl_usm_allocation_t *)acl_malloc(sizeof(acl_usm_allocation_t));

    if (!usm_alloc) {
      BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context, "Out of host memory");
    }

    int error;
    void *mem = acl_get_hal()->shared_alloc(
        device, size, alignment,
        mmd_properties[0] ? mmd_properties.data() : nullptr, &error);
    if (mem == NULL) {
      acl_free(usm_alloc);
      switch (error) {
      case CL_OUT_OF_HOST_MEMORY:
        BAIL_INFO(error, context,
                  "Error: Unable to allocate " + std::to_string(size) +
                      " bytes");
        break;
      case CL_INVALID_VALUE:
        BAIL_INFO(error, context,
                  "Error: Unsupported alignment of " +
                      std::to_string(alignment));
        break;
      case CL_INVALID_PROPERTY:
        BAIL_INFO(error, context, "Error: Unsuported properties");
        break;
      default:
        BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
                  "Error: Unable to allocate memory");
        break;
      }
    }

    usm_alloc->mem = NULL;
    usm_alloc->device = device; // May be NULL
    usm_alloc->range.begin = mem;
    usm_alloc->range.next = (void *)((size_t)mem + size);
    usm_alloc->alloc_flags = alloc_flags;
    usm_alloc->type = CL_MEM_TYPE_SHARED_INTEL;
    usm_alloc->alignment = alignment;
    usm_alloc->host_shared_mem_id = 0; // Initialize to 0
    if (track_mem_id) {
      usm_alloc->host_shared_mem_id = *mem_id;
    }

    l_add_usm_alloc_to_context(context, usm_alloc);
    return mem;
  }

  // After all the error check, still error out
  // Shared allocation is not supported yet
  BAIL_INFO(CL_INVALID_VALUE, context,
            "Shared allocation is not supported for devices in this context");
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clMemFreeINTEL(cl_context context, void *ptr) {
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_context_is_valid(context)) {
    return CL_INVALID_CONTEXT;
  }

  // NULL is valid input where nothing happens
  if (ptr == NULL) {
    return CL_SUCCESS;
  }

  acl_usm_allocation_t *usm_alloc = acl_get_usm_alloc_from_ptr(context, ptr);
  if (!usm_alloc) {
    ERR_RET(CL_INVALID_VALUE, context,
            "Memory must be USM allocation in context");
  }
  if (usm_alloc->range.begin != ptr) {
    ERR_RET(CL_INVALID_VALUE, context,
            "Pointer must be exact value returned by allocation");
  }

  switch (usm_alloc->type) {
  case CL_MEM_TYPE_HOST_INTEL: {
    if (acl_get_hal()->free) {
      if (acl_get_hal()->free(context, const_cast<void *>(ptr))) {
        ERR_RET(CL_INVALID_VALUE, context, "Failed to free host allocation");
      }
    }
    break;
  }
  case CL_MEM_TYPE_DEVICE_INTEL: {
    cl_int status = clReleaseMemObjectIntelFPGA(usm_alloc->mem);
    if (status != CL_SUCCESS) {
      return status;
    }
    break;
  }
  case CL_MEM_TYPE_SHARED_INTEL: {
    if (acl_get_hal()->free) {
      if (acl_get_hal()->free(context, const_cast<void *>(ptr))) {
        ERR_RET(CL_INVALID_VALUE, context, "Failed to free shared allocation");
      }
    }
    break;
  }
  default: {
    ERR_RET(CL_INVALID_VALUE, context, "Pointer must be from USM allocation");
    break;
  }
  }

  l_remove_usm_alloc_from_context(context, usm_alloc);
  acl_free(usm_alloc);
  usm_alloc = nullptr;

  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clMemBlockingFreeINTEL(cl_context context,
                                                       void *ptr) {
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_context_is_valid(context)) {
    return CL_INVALID_CONTEXT;
  }

  // NULL is valid input where nothing happens
  if (ptr == NULL) {
    return CL_SUCCESS;
  }

  acl_usm_allocation_t *usm_alloc = acl_get_usm_alloc_from_ptr(context, ptr);
  if (!usm_alloc) {
    ERR_RET(CL_INVALID_VALUE, context,
            "Memory must be USM allocation in context");
  }
  if (usm_alloc->range.begin != ptr) {
    ERR_RET(CL_INVALID_VALUE, context,
            "Pointer must be exact value returned by allocation");
  }

  // wait for enqueued commands that uses ptr to finish before free
  l_cl_mem_blocking_free(context, ptr);

  switch (usm_alloc->type) {
  case CL_MEM_TYPE_HOST_INTEL: {
    if (acl_get_hal()->free) {
      if (acl_get_hal()->free(context, const_cast<void *>(ptr))) {
        ERR_RET(CL_INVALID_VALUE, context, "Failed to free host allocation");
      }
    }
    break;
  }
  case CL_MEM_TYPE_DEVICE_INTEL: {
    cl_int status = clReleaseMemObjectIntelFPGA(usm_alloc->mem);
    if (status != CL_SUCCESS) {
      return status;
    }
    break;
  }
  case CL_MEM_TYPE_SHARED_INTEL: {
    if (acl_get_hal()->free) {
      if (acl_get_hal()->free(context, const_cast<void *>(ptr))) {
        ERR_RET(CL_INVALID_VALUE, context, "Failed to free shared allocation");
      }
    }
    break;
  }
  default: {
    ERR_RET(CL_INVALID_VALUE, context, "Pointer must be from USM allocation");
    break;
  }
  }

  l_remove_usm_alloc_from_context(context, usm_alloc);
  acl_free(usm_alloc);
  usm_alloc = nullptr;

  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetMemAllocInfoINTEL(
    cl_context context, const void *ptr, cl_mem_info_intel param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret) {
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_context_is_valid(context)) {
    return CL_INVALID_CONTEXT;
  }
  VALIDATE_ARRAY_OUT_ARGS(param_value_size, param_value, param_value_size_ret,
                          context);

  // Get USM allocation associated with ptr
  acl_usm_allocation_t *usm_alloc = acl_get_usm_alloc_from_ptr(context, ptr);

  acl_result_t result;
  RESULT_INIT;

  switch (param_name) {
  case CL_MEM_ALLOC_TYPE_INTEL: {
    if (usm_alloc) {
      RESULT_UINT(usm_alloc->type);
    } else {
      RESULT_UINT(CL_MEM_TYPE_UNKNOWN_INTEL);
    }
  } break;

  case CL_MEM_ALLOC_BUFFER_LOCATION_INTEL: {
    if (usm_alloc) {
      if (usm_alloc->mem) {
        RESULT_UINT(usm_alloc->mem->mem_id);
      } else {
        RESULT_UINT(usm_alloc->host_shared_mem_id);
      }
    } else {
      RESULT_UINT(0);
    }
  } break;

  case CL_MEM_ALLOC_BASE_PTR_INTEL: {
    void *base_ptr = NULL;
    if (usm_alloc) {
      base_ptr = usm_alloc->range.begin;
    }
    RESULT_PTR(base_ptr);
  } break;

  case CL_MEM_ALLOC_SIZE_INTEL: {
    size_t size = 0;
    if (usm_alloc) {
      size = (size_t)((unsigned long long)usm_alloc->range.next -
                      (unsigned long long)usm_alloc->range.begin);
    }
    RESULT_SIZE_T(size);
  } break;

  case CL_MEM_ALLOC_DEVICE_INTEL: {
    cl_device_id device = NULL;
    if (usm_alloc) {
      device = usm_alloc->device;
    }
    RESULT_PTR(device);
  } break;

  case CL_MEM_ALLOC_FLAGS_INTEL: {
    cl_mem_alloc_flags_intel alloc_flags = 0;
    if (usm_alloc) {
      alloc_flags = usm_alloc->alloc_flags;
    }
    RESULT_BITFIELD(alloc_flags);
  } break;

  default: {
    ERR_RET(CL_INVALID_VALUE, context, "Param name is not a valid query");
  } break;
  }

  if (param_value) {
    // Try to return the param value.
    if (param_value_size < result.size) {
      // Buffer is too small to hold the return value.
      ERR_RET(CL_INVALID_VALUE, context,
              "Param value size is smaller than query return type");
    }
    RESULT_COPY(param_value, param_value_size);
  }

  if (param_value_size_ret) {
    *param_value_size_ret = result.size;
  }
  return CL_SUCCESS;
}

// clEnqueueMemsetINTEL has been removed in the latest OpenCL spec, but SYCl
// runtime hasn't been updated yet.
// Keep it around until it is not used by SYCL.
ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clEnqueueMemsetINTEL(cl_command_queue command_queue, void *dst_ptr,
                     cl_int value, size_t size, cl_uint num_events_in_wait_list,
                     const cl_event *event_wait_list, cl_event *event) {
  return clEnqueueMemFillINTEL(command_queue, dst_ptr, &value, sizeof(char),
                               size, num_events_in_wait_list, event_wait_list,
                               event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueMemFillINTEL(
    cl_command_queue command_queue, void *dst_ptr, const void *pattern,
    size_t pattern_size, size_t size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  char *ptr;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (dst_ptr == NULL) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer argument cannot be NULL");
  }
  if (((uintptr_t)dst_ptr) % (pattern_size) != 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer not aligned with pattern size");
  }
  if (pattern == NULL) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pattern argument cannot be NULL");
  }
  if (pattern_size == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pattern size argument cannot be 0");
  }
  // Pattern size must be less than largest supported int/float vec type
  if (pattern_size > sizeof(double) * 16) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Patern size must be less than double16");
  }
  // Pattern size can only be power of 2
  if (pattern_size & (pattern_size - 1)) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Patern size must be power of 2");
  }
  if (size == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context, "Size cannot be 0");
  }
  if (size % pattern_size != 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Size must be multiple of pattern size");
  }

  // This array is passed to clSetEventCallback for releasing the
  // allocated memory and releasing the event, if *event is null.
  void **callback_data = (void **)acl_malloc(sizeof(void *) * 2);
  if (!callback_data) {
    ERR_RET(CL_OUT_OF_HOST_MEMORY, command_queue->context,
            "Out of host memory");
  }

  acl_aligned_ptr_t *aligned_ptr =
      (acl_aligned_ptr_t *)acl_malloc(sizeof(acl_aligned_ptr_t));
  if (!aligned_ptr) {
    acl_free(callback_data);
    ERR_RET(CL_OUT_OF_HOST_MEMORY, command_queue->context,
            "Out of host memory");
  }

  // Replicating the value, size times.
  *aligned_ptr = acl_mem_aligned_malloc(size);
  ptr = (char *)(aligned_ptr->aligned_ptr);
  if (!ptr) {
    acl_free(aligned_ptr);
    acl_free(callback_data);
    ERR_RET(CL_OUT_OF_HOST_MEMORY, command_queue->context,
            "Out of host memory");
  }

  for (cl_uint i = 0; i < size / pattern_size; i++) {
    safe_memcpy(&(ptr[i * pattern_size]), pattern, pattern_size, pattern_size,
                pattern_size);
  }

  acl_usm_allocation_t *usm_alloc =
      acl_get_usm_alloc_from_ptr(command_queue->context, dst_ptr);

  bool dst_on_host = true;
  cl_device_id dst_device = NULL;
  if (usm_alloc) {
    if (l_ptr_in_usm_alloc_range(usm_alloc, dst_ptr, size) != CL_TRUE) {
      acl_mem_aligned_free(command_queue->context, aligned_ptr);
      acl_free(aligned_ptr);
      acl_free(callback_data);
      ERR_RET(CL_INVALID_VALUE, command_queue->context,
              "Size accesses outside of USM allocation dst_ptr range");
    }
    if (usm_alloc->type == CL_MEM_TYPE_DEVICE_INTEL) {
      dst_device = usm_alloc->device;
      dst_on_host = false;
    }
  }

  // Even if dst_ptr is not USM allocation, continue assuming it is system mem.
  // If it is USM allocation though, it needs to be on same dev as queue.
  if (dst_device && dst_device->id != command_queue->device->id) {
    // Cleaning up before failing.
    acl_mem_aligned_free(command_queue->context, aligned_ptr);
    acl_free(aligned_ptr);
    acl_free(callback_data);
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Memory allocation needs to be on command queue device");
  }

  cl_event tmp_event = NULL;

  // Create an event to fill the data after events in wait list complete
  cl_int status =
      acl_create_event(command_queue, num_events_in_wait_list, event_wait_list,
                       CL_COMMAND_MEMFILL_INTEL, &tmp_event);
  if (status != CL_SUCCESS) {
    // Cleaning up before failing.
    acl_mem_aligned_free(command_queue->context, aligned_ptr);
    acl_free(aligned_ptr);
    acl_free(callback_data);
    return status; // already signalled callback
  }
  tmp_event->cmd.info.usm_xfer.src_ptr = ptr;
  tmp_event->cmd.info.usm_xfer.dst_ptr = dst_ptr;
  tmp_event->cmd.info.usm_xfer.size = size;
  tmp_event->cmd.info.usm_xfer.src_on_host = true;
  tmp_event->cmd.info.usm_xfer.dst_on_host = dst_on_host;

  // If nothing's blocking, then complete right away
  acl_idle_update(command_queue->context);

  callback_data[0] = (void *)(aligned_ptr);
  if (event) {
    *event = tmp_event;
    // User needs the event, so we shouldn't release it after the event
    // completion.
    callback_data[1] = NULL;
  } else {
    // Passing the event to release it when the event is done.
    callback_data[1] = tmp_event;
  }
  clSetEventCallback(tmp_event, CL_COMPLETE,
                     acl_free_allocation_after_event_completion,
                     (void *)callback_data);

  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueMemcpyINTEL(
    cl_command_queue command_queue, cl_bool blocking, void *dst_ptr,
    const void *src_ptr, size_t size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (dst_ptr == NULL) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer argument cannot be NULL");
  }
  if (src_ptr == NULL) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer argument cannot be NULL");
  }
  if (size == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer size cannot be 0");
  }

  if (((char *)src_ptr < (char *)dst_ptr &&
       (char *)src_ptr + size > (char *)dst_ptr) ||
      ((char *)dst_ptr < (char *)src_ptr &&
       (char *)dst_ptr + size > (char *)src_ptr)) {
    ERR_RET(CL_MEM_COPY_OVERLAP, command_queue->context,
            "Source and destination memory overlaps");
  }

  acl_usm_allocation_t *dst_usm_alloc =
      acl_get_usm_alloc_from_ptr(command_queue->context, dst_ptr);

  bool dst_on_host = true;
  cl_device_id dst_device = NULL;
  if (dst_usm_alloc) {
    if (l_ptr_in_usm_alloc_range(dst_usm_alloc, dst_ptr, size) != CL_TRUE) {
      ERR_RET(CL_INVALID_VALUE, command_queue->context,
              "Size accesses outside of USM allocation dst_ptr range");
    }
    if (dst_usm_alloc->type == CL_MEM_TYPE_DEVICE_INTEL) {
      dst_device = dst_usm_alloc->device;
      dst_on_host = false;
    }
  }

  acl_usm_allocation_t *src_usm_alloc =
      acl_get_usm_alloc_from_ptr(command_queue->context, src_ptr);

  bool src_on_host = true;
  cl_device_id src_device = NULL;
  // Even if src_ptr is not USM pointer, continue assuming it's system mem
  if (src_usm_alloc) {
    if (l_ptr_in_usm_alloc_range(src_usm_alloc, src_ptr, size) != CL_TRUE) {
      ERR_RET(CL_INVALID_VALUE, command_queue->context,
              "Size accesses outside of USM allocation src_ptr range");
    }
    if (src_usm_alloc->type == CL_MEM_TYPE_DEVICE_INTEL) {
      src_device = src_usm_alloc->device;
      src_on_host = false;
    }
  }

  if ((dst_device && dst_device->id != command_queue->device->id) ||
      (src_device && src_device->id != command_queue->device->id)) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Memory allocation needs to be on command queue's device");
  }

  cl_event tmp_event = NULL;

  // Create an event to fill the data after events in wait list complete
  cl_int status =
      acl_create_event(command_queue, num_events_in_wait_list, event_wait_list,
                       CL_COMMAND_MEMCPY_INTEL, &tmp_event);
  if (status != CL_SUCCESS) {
    return status; // already signalled callback
  }
  tmp_event->cmd.info.usm_xfer.src_ptr = src_ptr;
  tmp_event->cmd.info.usm_xfer.dst_ptr = dst_ptr;
  tmp_event->cmd.info.usm_xfer.size = size;
  tmp_event->cmd.info.usm_xfer.src_on_host = src_on_host;
  tmp_event->cmd.info.usm_xfer.dst_on_host = dst_on_host;

  // If nothing's blocking, then complete right away
  acl_idle_update(command_queue->context);

  if (blocking) {
    status = clWaitForEvents(1, &tmp_event);
  }

  if (event) {
    *event = tmp_event;
  } else {
    // User didn't care, so forget about the event.
    clReleaseEvent(tmp_event);
    acl_idle_update(command_queue->context); // Clean up early
  }

  if (blocking && status == CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST) {
    return status;
  }

  return CL_SUCCESS;
}

// Unused argument names are commented out to avoid Windows compile warning:
// unreferenced formal parameter
// Uncomment when the API is fully implemented
ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueMigrateMemINTEL(
    cl_command_queue command_queue, const void *ptr, size_t /* size */,
    cl_mem_migration_flags /* flags */, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (ptr == NULL) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer argument can not be NULL");
  }

  // Migrate currently doesn't do anything
  // Treat it like clEnqueueMarkerWithWaitList, but we won't wait for all events
  // in command queue to finish for OOO queues.
  cl_event local_event = NULL;
  cl_int result =
      acl_create_event(command_queue, num_events_in_wait_list, event_wait_list,
                       CL_COMMAND_MIGRATEMEM_INTEL, &local_event);

  if (result != CL_SUCCESS) {
    return result;
  }

  if (event) {
    *event = local_event;
  } else {
    clReleaseEvent(local_event);
  }
  return CL_SUCCESS;
}

// Unused argument names are commented out to avoid Windows compile warning:
// unreferenced formal parameter
// Uncomment when the API is implemented
ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueMemAdviseINTEL(
    cl_command_queue command_queue, const void *ptr, size_t /* size */,
    cl_mem_advice_intel /* advice */, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (ptr == NULL) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer argument can not be NULL");
  }

  // MemAdvise currently doesn't do anything
  // Treat it like clEnqueueMarkerWithWaitList, but we won't wait for all events
  // in command queue to finish for OOO queues.
  cl_event local_event = NULL;
  cl_int result =
      acl_create_event(command_queue, num_events_in_wait_list, event_wait_list,
                       CL_COMMAND_MEMADVISE_INTEL, &local_event);

  if (result != CL_SUCCESS) {
    return result;
  }

  if (event) {
    *event = local_event;
  } else {
    clReleaseEvent(local_event);
  }
  return CL_SUCCESS;
}

void acl_usm_memcpy(void *, acl_device_op_t *op) {
  cl_event event = op->info.event;
  acl_assert_locked();

  if (!acl_event_is_valid(event) ||
      !acl_command_queue_is_valid(event->command_queue)) {
    acl_set_device_op_execution_status(op, -1);
    return;
  }

  acl_set_device_op_execution_status(op, CL_SUBMITTED);
  acl_set_device_op_execution_status(op, CL_RUNNING);

  const acl_hal_t *const hal = acl_get_hal();
  if (event->cmd.info.usm_xfer.src_on_host) {
    if (event->cmd.info.usm_xfer.dst_on_host) {
      hal->copy_hostmem_to_hostmem(event, event->cmd.info.usm_xfer.src_ptr,
                                   event->cmd.info.usm_xfer.dst_ptr,
                                   event->cmd.info.usm_xfer.size);
    } else {
      hal->copy_hostmem_to_globalmem(event, event->cmd.info.usm_xfer.src_ptr,
                                     event->cmd.info.usm_xfer.dst_ptr,
                                     event->cmd.info.usm_xfer.size);
    }
  } else {
    if (event->cmd.info.usm_xfer.dst_on_host) {
      hal->copy_globalmem_to_hostmem(event, event->cmd.info.usm_xfer.src_ptr,
                                     event->cmd.info.usm_xfer.dst_ptr,
                                     event->cmd.info.usm_xfer.size);
    } else {
      hal->copy_globalmem_to_globalmem(event, event->cmd.info.usm_xfer.src_ptr,
                                       event->cmd.info.usm_xfer.dst_ptr,
                                       event->cmd.info.usm_xfer.size);
    }
  }
}

// Called by acl_submit_command in acl_command.cpp once all events in
// event_wait_list of clEnqueueMemFill/clEnqueueMemcpy are complete.
// If transfer is from host-to-host, host-to-shared or shared-to-host,
// do system memcpy. Otherwise, queue device op to do copy.
int acl_submit_usm_memcpy(cl_event event) {
  int result = 0;
  acl_assert_locked();

  // No user-level scheduling blocks this memory transfer.
  // So submit it to the device op queue.
  // But only if it isn't already enqueued there.
  if (!acl_event_is_valid(event)) {
    return result;
  }
  if (event->last_device_op) {
    return result;
  }

  acl_device_op_queue_t *doq = &(acl_platform.device_op_queue);
  acl_device_op_t *last_op = 0;

  // Precautionary, but it also nudges the device scheduler to try
  // to free up old operation slots.
  acl_forget_proposed_device_ops(doq);

  // Check if both src and dst on are device and do system memcpy instead.
  // clEnqueueMemcpy and clEnqueueMemFill only considers device allocation
  // to be not "on_host". It may be more optimal for BSP to do copy for all
  // USM allocations. However, that will mean copy from host to host USM
  // allocation has acl_device_op_conflict_type_t of ACL_CONFLICT_MEM.
  if (event->cmd.info.usm_xfer.src_on_host) {
    if (event->cmd.info.usm_xfer.dst_on_host) {
      // Do memcpy and set event to complete
      safe_memcpy(event->cmd.info.usm_xfer.dst_ptr,
                  event->cmd.info.usm_xfer.src_ptr,
                  event->cmd.info.usm_xfer.size, event->cmd.info.usm_xfer.size,
                  event->cmd.info.usm_xfer.size);
      acl_set_execution_status(event, CL_SUBMITTED);
      acl_set_execution_status(event, CL_RUNNING);
      acl_set_execution_status(event, CL_COMPLETE);
      return 1;
    }
  }

  last_op = acl_propose_device_op(doq, ACL_DEVICE_OP_USM_MEMCPY, event);

  if (last_op) {
    // We managed to enqueue everything.
    event->last_device_op = last_op;
    acl_commit_proposed_device_ops(doq);
    result = 1;
  } else {
    // Back off, and wait until later when we have more space in the
    // device op queue.
    acl_forget_proposed_device_ops(doq);
  }
  return result;
}

bool acl_usm_ptr_belongs_to_context(cl_context context, const void *ptr) {
  if (!acl_get_usm_alloc_from_ptr(context, ptr)) {
    return false;
  }

  return true;
}

acl_usm_allocation_t *acl_get_usm_alloc_from_ptr(cl_context context,
                                                 const void *ptr) {
  for (auto it : context->usm_allocation) {
    if (l_ptr_in_usm_alloc_range(it, ptr, 1) == CL_TRUE) {
      return it;
    }
  }
  return NULL;
}

cl_bool l_ptr_in_usm_alloc_range(acl_usm_allocation_t *usm_alloc,
                                 const void *ptr, size_t size) {
  unsigned long long start = (unsigned long long)usm_alloc->range.begin;
  unsigned long long end = (unsigned long long)usm_alloc->range.next;
  if ((unsigned long long)ptr >= start &&
      (unsigned long long)ptr + size <= end) {
    return CL_TRUE;
  } else {
    return CL_FALSE;
  }
}

void l_add_usm_alloc_to_context(cl_context context,
                                acl_usm_allocation_t *usm_alloc) {
  for (auto it : context->usm_allocation) {
    if (it == usm_alloc) {
      return;
    }
  }
  context->usm_allocation.insert(context->usm_allocation.begin(), usm_alloc);
}

void l_remove_usm_alloc_from_context(cl_context context,
                                     acl_usm_allocation_t *usm_alloc) {
  context->usm_allocation.remove(usm_alloc);
}

void *l_set_dev_alloc_bit(const void *ptr) {
  void *ptr_val =
      (void *)((unsigned long long)ptr | ((unsigned long long)1 << 61));
  return ptr_val;
}

void l_cl_mem_blocking_free(cl_context context, void *ptr) {
  int num_command_queues = context->num_command_queues;
  acl_usm_allocation_t *src_usm_alloc = NULL;
  acl_usm_allocation_t *dst_usm_alloc = NULL;

  for (int i = 0; i < num_command_queues; i++) {
    cl_command_queue current_cq = context->command_queue[i];
    // Set a flag to indicate the command set of this command queue is being
    // traversed, and any event deletion should be deferred
    current_cq->waiting_for_events = true;

    if (current_cq->properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) {
      // Current queue is ooo queue, check events in commands
      for (auto it = current_cq->commands.begin();
           it != current_cq->commands.end();) {
        auto event = *it;
        if (event->execution_status != CL_COMPLETE) {
          // check if ptr is used by kernels when we submit to queue
          if (event->ptr_hashtable.find(ptr) != event->ptr_hashtable.end()) {
            clWaitForEvents(1, &event);
          }
          // check if ptr is used in queues
          if ((event->cmd.type == CL_COMMAND_MEMCPY_INTEL) ||
              (event->cmd.type == CL_COMMAND_MEMFILL_INTEL)) {
            src_usm_alloc = acl_get_usm_alloc_from_ptr(
                context, event->cmd.info.usm_xfer.src_ptr);
            dst_usm_alloc = acl_get_usm_alloc_from_ptr(
                context, event->cmd.info.usm_xfer.dst_ptr);
            if ((src_usm_alloc && (src_usm_alloc->range.begin == ptr)) ||
                (dst_usm_alloc && (dst_usm_alloc->range.begin == ptr))) {
              clWaitForEvents(1, &event);
            }
          }
        }
        if (event->defer_removal) {
          it = current_cq->commands.erase(it);
          event->defer_removal = false; // Reset as this event might get reused
        } else {
          ++it;
        }
      }
    } else {
      // Current queue is inorder queue, check events in inorder commands
      for (auto it = current_cq->inorder_commands.begin();
           it != current_cq->inorder_commands.end();) {
        auto event = *it;
        if (event->execution_status != CL_COMPLETE) {
          // check if ptr is used by kernels when we submit to queue
          if (event->ptr_hashtable.find(ptr) != event->ptr_hashtable.end()) {
            clWaitForEvents(1, &event);
          }
          // check if ptr is used in queues
          if ((event->cmd.type == CL_COMMAND_MEMCPY_INTEL) ||
              (event->cmd.type == CL_COMMAND_MEMFILL_INTEL)) {
            src_usm_alloc = acl_get_usm_alloc_from_ptr(
                context, event->cmd.info.usm_xfer.src_ptr);
            dst_usm_alloc = acl_get_usm_alloc_from_ptr(
                context, event->cmd.info.usm_xfer.dst_ptr);
            if ((src_usm_alloc && (src_usm_alloc->range.begin == ptr)) ||
                (dst_usm_alloc && (dst_usm_alloc->range.begin == ptr))) {
              clWaitForEvents(1, &event);
            }
          }
        }
        if (event->defer_removal) {
          it = current_cq->inorder_commands.erase(it);
          event->defer_removal = false; // Reset as this event might get reused
        } else {
          ++it;
        }
      }
    }
    current_cq->waiting_for_events = false;
  }
}

bool acl_usm_has_access_capability(cl_device_id device, cl_device_info query) {
  cl_bitfield capabilities = 0;
  cl_int ret =
      clGetDeviceInfo(device, query, sizeof(capabilities), &capabilities, 0);
  if (ret)
    return false;

  return capabilities & CL_UNIFIED_SHARED_MEMORY_ACCESS_INTEL;
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif
