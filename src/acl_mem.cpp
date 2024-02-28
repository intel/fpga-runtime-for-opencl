// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>

// External library headers.
#include <CL/opencl.h>

// Internal headers.
#include <acl.h>
#include <acl_context.h>
#include <acl_device_op.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_hal.h>
#include <acl_icd_dispatch.h>
#include <acl_mem.h>
#include <acl_platform.h>
#include <acl_support.h>
#include <acl_svm.h>
#include <acl_util.h>
#include <check_copy_overlap.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// The OpenCL runtime must manage memory allocations for:
//    - MemObjects pointing to user-provided buffers in host memory
//    - MemObjects pointing to buffers in host memory provided by OpenCL
//    - MemObjects pointing to device global memory
//
// In our implementation the memory allocator resides on the host
// processor.  So the allocator bookkeeping must exist outside of the
// memory it is managing.
//
// Our allocator is optimized for simplicity, and for the case where there
// are only a few largish blocks in the system at a time.
//
// The scheme is as follows:
//   A Region is either for user-provided memory or not.
//
//   In regions of system-provided memory span a contiguous set of addresses.
//   We assume the beginning address has the most general or stringent
//   alignment, e.g. 4 or 8 byte.
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

static void *l_get_address_of_writable_copy(cl_mem mem,
                                            unsigned int physical_device_id,
                                            int *on_host_ptr,
                                            cl_bool is_dest_unmap);

static cl_int l_enqueue_mem_transfer(cl_command_queue command_queue,
                                     cl_bool blocking, cl_mem src_buffer,
                                     size_t src_offset[3], size_t src_row_pitch,
                                     size_t src_slice_pitch, cl_mem dst_buffer,
                                     size_t dst_offset[3], size_t dst_row_pitch,
                                     size_t dst_slice_pitch, size_t cb[3],
                                     cl_uint num_events, const cl_event *events,
                                     cl_event *event, cl_command_type type,
                                     cl_map_flags map_flags);

static void auto_unmap_mem(cl_context context, unsigned int physical_id,
                           cl_mem src_mem, acl_device_op_t *op);
static void l_mem_transfer_buffer_explicitly(cl_context context,
                                             acl_device_op_t *op,
                                             unsigned int physical_device_id,
                                             const acl_command_info_t &cmd);
static void
acl_forcibly_release_all_memory_for_context_in_region(cl_context context,
                                                      acl_mem_region_t *region);
static void acl_dump_mem_internal(cl_mem mem);

static void l_get_working_range(const acl_block_allocation_t *block_allocation,
                                unsigned physical_device_id,
                                unsigned target_mem_id, unsigned bank_id,
                                acl_addr_range_t *working_range,
                                void **initial_try);
static int acl_allocate_block(acl_block_allocation_t *block_allocation,
                              const cl_mem mem, unsigned physical_device_id,
                              unsigned target_mem_id);
static int copy_image_metadata(cl_mem mem);
static void remove_mem_block_linked_list(acl_block_allocation_t *block);
static cl_bool is_image(cl_mem mem);
static void l_free_image_members(cl_mem mem);

cl_int acl_convert_image_format(const void *input_element, void *output_element,
                                cl_image_format format_from,
                                cl_image_format format_to);

static size_t get_offset_for_image_param(cl_context context,
                                         cl_mem_object_type mem_object_type,
                                         const char *name);

// This callback is used to free the allocated host memory needed for memory
// transfers, currently used in clEnqueueFillBuffer, clEnqueueFillImage and
// clEnqueueMemsetINTEL
void CL_CALLBACK acl_free_allocation_after_event_completion(
    cl_event event, cl_int event_command_exec_status, void *callback_data) {
  void **callback_ptrs = (void **)
      callback_data; // callback_ptrs[0] is the allocated memory in host and
                     // callback_ptrs[1] is either NULL or pointer to event.
  event_command_exec_status =
      event_command_exec_status; // Avoiding Windows warning.
  event = event;
  std::scoped_lock lock{acl_mutex_wrapper};
  if (callback_ptrs[0]) {
    acl_mem_aligned_free(event->context, (acl_aligned_ptr_t *)callback_ptrs[0]);
    acl_free(callback_ptrs[0]);
  }
  if (callback_ptrs[1])
    clReleaseEvent(((cl_event)callback_ptrs[1]));
  acl_free(callback_data);
}

ACL_DEFINE_CL_OBJECT_ALLOC_FUNCTIONS(cl_mem);

//////////////////////////////
// OpenCL API

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainMemObjectIntelFPGA(cl_mem mem) {
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_mem_is_valid(mem)) {
    return CL_INVALID_MEM_OBJECT;
  }

  acl_retain(mem);

  acl_print_debug_msg("Retain  mem[%p] now %u\n", mem, acl_ref_count(mem));

  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clRetainMemObject(cl_mem mem) {
  return clRetainMemObjectIntelFPGA(mem);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseMemObjectIntelFPGA(cl_mem mem) {
  std::scoped_lock lock{acl_mutex_wrapper};

  // In the double-free case, we'll error out here, for two reasons:
  // 1) the reference count will be 0.
  // 1) mem->region == 0
  if (!acl_mem_is_valid(mem)) {
    return CL_INVALID_MEM_OBJECT;
  }

  acl_release(mem);

  acl_print_debug_msg("Release mem[%p] now %u\n", mem, acl_ref_count(mem));

  if (!acl_ref_count(mem)) {
    cl_context context = mem->context;
    // Free the memory object.

    // Calling the user registered destructor callbacks, before freeing the
    // resources.
    acl_mem_destructor_callback(mem);

    // For sub-buffers, we don't free the host buffer. Instead, we free the
    // device memory, remove the sub-buffer from the list of active memory
    // objects and release the parent buffer
    if (mem->mem_object_type == CL_MEM_OBJECT_BUFFER &&
        mem->fields.buffer_objs.is_subbuffer) {
      if (mem->block_allocation != NULL &&
          mem->block_allocation->region != NULL &&
          !mem->block_allocation->region->is_user_provided) {
        remove_mem_block_linked_list(mem->block_allocation);
      }

      if (mem->fields.buffer_objs.next_sub != NULL) {
        mem->fields.buffer_objs.next_sub->fields.buffer_objs.prev_sub =
            mem->fields.buffer_objs.prev_sub;
      }
      if (mem->fields.buffer_objs.prev_sub != NULL) {
        mem->fields.buffer_objs.prev_sub->fields.buffer_objs.next_sub =
            mem->fields.buffer_objs.next_sub;
      }
      --mem->fields.buffer_objs.parent->fields.buffer_objs.num_subbuffers;
      clReleaseMemObject(mem->fields.buffer_objs.parent);
    } else {
      if (is_image(mem)) {
        l_free_image_members(mem);
      }
      // The only case wehre mem->region->is_user_provided && mem->host_mem.raw
      // != NULL is when user creates a buffer with CL_MEM_USE_HOST_PTR set and
      // the pointer is allocated with clSVMAlloc.
      if (mem->block_allocation != NULL &&
          mem->block_allocation->region != NULL &&
          !mem->block_allocation->region->is_user_provided &&
          mem->host_mem.raw) {
        // We've used system malloc somewhere along the way.
        // Release that block of memory.
        acl_mem_aligned_free(mem->context, &mem->host_mem);
      }

      // We don't currently allocate anything for pipes (except host pipes),
      // so there is nothing to free
      if (mem->host_pipe_info != NULL) {
        if (mem->host_pipe_info->m_channel_handle > 0) {
          acl_get_hal()->hostchannel_destroy(
              mem->host_pipe_info->m_physical_device_id,
              mem->host_pipe_info->m_channel_handle);
          mem->host_pipe_info->m_channel_handle = -1;
        }
        for (auto &host_op : mem->host_pipe_info->m_host_op_queue) {
          acl_context_callback(
              mem->context,
              "Warning: The pipe being destroyed has pending operations!");
          if (host_op.m_host_buffer != NULL) {
            free(host_op.m_host_buffer);
          }
        }
        acl_mutex_destroy(&(mem->host_pipe_info->m_lock));
        acl_delete(mem->host_pipe_info);
        context->pipe_vec.erase(std::remove(context->pipe_vec.begin(),
                                            context->pipe_vec.end(), mem));
      }
      if (mem->block_allocation != NULL &&
          mem->block_allocation->region != NULL &&
          !mem->block_allocation->region->is_user_provided) {
        remove_mem_block_linked_list(mem->block_allocation);
      }
    }

    acl_untrack_object(mem);
    acl_delete(mem->block_allocation);
    acl_free_cl_mem(mem);
    clReleaseContext(context);
  }

  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clReleaseMemObject(cl_mem mem) {
  return clReleaseMemObjectIntelFPGA(mem);
}

static int acl_do_physical_buffer_allocation(unsigned physical_device_id,
                                             cl_mem mem) {
#ifdef MEM_DEBUG_MSG
  printf("acl_do_physical_buffer_allocation\n");
#endif

  acl_assert_locked();

  // When mem_id == 0 it indicates the mem_id is not finalized yet so need to
  // set it to a real value here.
  bool glob_mem = mem->block_allocation->region == &(acl_platform.global_mem);
  if (glob_mem && mem->mem_id == 0) {
    // Memory migration between SVM and device global memory is not supported.
    // If device supports both, do physical buffer allocation on device global
    // memory only.
    if (acl_svm_device_supports_physical_memory(physical_device_id) &&
        acl_svm_device_supports_any_svm(physical_device_id)) {
      assert(
          acl_platform.device[physical_device_id]
                  .def.autodiscovery_def.num_global_mem_systems > 0 &&
          "Device is not configured to support SVM and device global memory.");
      int tmp_mem_id = acl_get_default_device_global_memory(
          acl_platform.device[physical_device_id].def);
      assert(tmp_mem_id >= 0 &&
             "Device does not have any device global memory.");
      mem->mem_id = (unsigned int)tmp_mem_id;
    }
  }

  int result = 0;
  if (mem->reserved_allocations[physical_device_id].size() == 0) {
    acl_resize_reserved_allocations_for_device(
        mem, acl_platform.device[physical_device_id].def);
  }
  if (glob_mem &&
      mem->reserved_allocations[physical_device_id][mem->mem_id] != NULL) {
    result = 1;
    // We can only get here if the allocation was deferred,
    // which means the original block_allocation isn't in the reserved list,
    // which means we have to delete it.
    acl_delete(mem->block_allocation);
    mem->block_allocation =
        mem->reserved_allocations[physical_device_id][mem->mem_id];
  } else {
    result = acl_allocate_block(mem->block_allocation, mem, physical_device_id,
                                mem->mem_id);
  }

  if (glob_mem && result) {
    mem->reserved_allocations[physical_device_id][mem->mem_id] =
        mem->block_allocation;
  }

  // For images, copy the additional meta data (width, height, etc) after
  // allocating
  if (result && is_image(mem)) {
    result = copy_image_metadata(mem);
  }

  return result;
}

ACL_EXPORT
CL_API_ENTRY cl_int clSetMemObjectDestructorCallbackIntelFPGA(
    cl_mem memobj,
    void(CL_CALLBACK *pfn_notify)(cl_mem memobj, void *user_data),
    void *user_data) {
  acl_mem_destructor_user_callback *cb;
  std::scoped_lock lock{acl_mutex_wrapper};
  if (!acl_mem_is_valid(memobj)) {
    return CL_INVALID_MEM_OBJECT;
  }

  if (pfn_notify == NULL) {
    return CL_INVALID_VALUE;
  }

  cb = (acl_mem_destructor_user_callback *)acl_malloc(
      sizeof(acl_mem_destructor_user_callback));
  if (!cb)
    return CL_OUT_OF_HOST_MEMORY;

  // Push to the front of the list.
  cb->notify_user_data = user_data;
  cb->mem_destructor_notify_fn = pfn_notify;
  cb->next = memobj->destructor_callback_list;
  memobj->destructor_callback_list = cb;

  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int clSetMemObjectDestructorCallback(
    cl_mem memobj,
    void(CL_CALLBACK *pfn_notify)(cl_mem memobj, void *user_data),
    void *user_data) {
  return clSetMemObjectDestructorCallbackIntelFPGA(memobj, pfn_notify,
                                                   user_data);
}

// Given a device, finalize the buffer allocation
int acl_bind_buffer_to_device(cl_device_id device, cl_mem mem) {
  acl_assert_locked();
  assert(mem);
  assert(device);
  if (mem->allocation_deferred) {
    unsigned int physical_device_id = device->def.physical_device_id;
    if (acl_do_physical_buffer_allocation(physical_device_id, mem)) {
      mem->allocation_deferred = 0;
      // If we need to copy from the host pointer, copy now.
      // If this is a sub buffer or a buffer with sub buffers, mark it as
      // auto-mapped, keep the data on the host for now and copy when it is
      // actually used. This prevents us from copying over data that may be
      // modified by an overlapping sub-buffer before this location is used.
      if ((mem->mem_object_type == CL_MEM_OBJECT_BUFFER &&
           acl_is_sub_or_parent_buffer(mem))) {
        mem->auto_mapped = 1;
        mem->writable_copy_on_host = 1;
      } else if ((mem->flags & CL_MEM_COPY_HOST_PTR) &&
                 mem->writable_copy_on_host == 1) {
        // We've allocated the memory -- now do a blocking copy of the host mem
        // buffer. This code should only trigger if the buffer is accessed
        // without enqueuing a kernel that uses it. This is generally not a
        // useful sequence of operations in the host program since the contents
        // of the buffer hasn't changed since it was created if no kernel
        // operates on it. However, this is still valid OpenCL code so we have
        // to ensure it works.

        cl_event unmap_event;
        cl_int status = clEnqueueUnmapMemObject(mem->context->auto_queue, mem,
                                                mem->host_mem.aligned_ptr, 0,
                                                NULL, // wait list
                                                &unmap_event);
        if (status != CL_SUCCESS) {
          return 0;
        }

        status = clWaitForEvents(1, &unmap_event);
        status |= clReleaseEvent(unmap_event);
        if (status != CL_SUCCESS) {
          return 0;
        }

        mem->mem_cpy_host_ptr_pending = 0;
      }
    } else {
      return 0;
    }
  }
  return 1;
}

ACL_EXPORT
CL_API_ENTRY cl_mem clCreateBufferWithPropertiesINTEL(
    cl_context context, const cl_mem_properties_intel *properties,
    cl_mem_flags flags, size_t size, void *host_ptr, cl_int *errcode_ret) {

  cl_mem result = 0;
  cl_mem mem;
  cl_bool context_has_device_with_only_svm;
  cl_bool context_has_device_with_physical_mem;
  unsigned int idevice;
  cl_uint bank_id = 0;
  cl_uint tmp_mem_id = 0;
  std::scoped_lock lock{acl_mutex_wrapper};

#ifdef MEM_DEBUG_MSG
  printf("CreateBuffer\n");
#endif

  while (properties != NULL && *properties != 0) {
    switch (*properties) {
    case CL_MEM_CHANNEL_INTEL: {
      if (flags & CL_CHANNEL_7_INTELFPGA) {
        BAIL_INFO(CL_INVALID_DEVICE, context,
                  "Both channel flag and channel property are set");
      }
      bank_id = (cl_uint) * (properties + 1);
    } break;
    case CL_MEM_ALLOC_BUFFER_LOCATION_INTEL: {
      tmp_mem_id = (cl_uint) * (properties + 1);

      // In FullSystem flow, buffer location is always the index of the global
      // memories. Therefore, there is no additional handling needed for FS.

      // However, in SYCL_HLS(IPA) flow, the user passed buffer_location<id>
      // maps to the global memory with the same id. Runtime needs to find the
      // correct index of the global memory with that id. The id filed is
      // introduced in 2024.2 in auto-discovery string. This change is for
      // accessor only and the USM buffer location change is done in the
      // simulator.

      // Runtime needs to determine whether it's FS or SYCL_HLS first by
      // checking if the global memory id exist or not. If exists, then it's
      // SYCL_HLS flow.
      bool is_SYCL_HLS = false;

      // We document the limitation here:
      // https://www.intel.com/content/www/us/en/docs/oneapi-fpga-add-on/
      // developer-guide/2024-0/targeting-multiple-homogeneous-fpga-devices.html
      // All FPGA devices used must be of the same FPGA card (same -Xstarget
      // target). There might be some workaround supporting multi-device with
      // different boards but they are for FS, not for SYCL_HLS.

      // Therefore, we can safely assume all devices have the same
      // global_mem_defs in SYCL_HLS flow as of 2024.2. So we can just check
      // acl_platform.device[0].

      auto global_mem_defs =
          acl_platform.device[0].def.autodiscovery_def.global_mem_defs;

      for (const auto &global_mem_def : global_mem_defs) {
        if (global_mem_def.id != "-") {
          is_SYCL_HLS = true;
          break;
        }
      }

      if (is_SYCL_HLS) {
        // find the correct index in the global memory
        long index = -1;
        for (auto it = begin(global_mem_defs); it != end(global_mem_defs);
             ++it) {
          if (stoul(it->id) == tmp_mem_id) {
            index = it - global_mem_defs.begin();
            break;
          }
        }

        if (index == -1) {
          BAIL_INFO(CL_INVALID_VALUE, context,
                    "Invalid Buffer Location id provided");
        }

        // Update the tmp_mem_id to the corect index in the global memories
        // vector.
        tmp_mem_id = static_cast<cl_uint>(index);
      }

    } break;
    default: {
      BAIL_INFO(CL_INVALID_DEVICE, context, "Invalid properties");
    }
    }
    properties += 2;
  }

#ifndef REMOVE_VALID_CHECKS
  if (!acl_context_is_valid(context))
    BAIL(CL_INVALID_CONTEXT);

  if (bank_id > 7) {
    BAIL_INFO(CL_INVALID_VALUE, context, "Invalid channel property value");
  }

  // Check flags
  {
    // Check for invalid enum bits
    if (flags & ~(CL_MEM_READ_WRITE | CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY |
                  CL_MEM_USE_HOST_PTR | CL_MEM_ALLOC_HOST_PTR |
                  CL_MEM_COPY_HOST_PTR | CL_MEM_HOST_WRITE_ONLY |
                  CL_MEM_HOST_READ_ONLY | CL_MEM_HOST_NO_ACCESS |
                  CL_CHANNEL_7_INTELFPGA | CL_MEM_HETEROGENEOUS_INTELFPGA)) {
      BAIL_INFO(CL_INVALID_VALUE, context, "Invalid or unsupported flags");
    }

    {
      // Check for exactly one read/write spec
      int num_rw_specs = 0;
      if (flags & CL_MEM_READ_WRITE)
        num_rw_specs++;
      if (flags & CL_MEM_READ_ONLY)
        num_rw_specs++;
      if (flags & CL_MEM_WRITE_ONLY)
        num_rw_specs++;
      // Default to CL_MEM_READ_WRITE.
      if (num_rw_specs > 1) {
        BAIL_INFO(CL_INVALID_VALUE, context,
                  "More than one read/write flag is specified");
      }
      if (num_rw_specs == 0)
        flags |= CL_MEM_READ_WRITE;

      // Check for exactly one host read/write/no_access spec
      num_rw_specs = 0;
      if (flags & CL_MEM_HOST_READ_ONLY)
        num_rw_specs++;
      if (flags & CL_MEM_HOST_WRITE_ONLY)
        num_rw_specs++;
      if (flags & CL_MEM_HOST_NO_ACCESS)
        num_rw_specs++;
      if (num_rw_specs > 1) {
        BAIL_INFO(
            CL_INVALID_VALUE, context,
            "More than one host read/write/no_access flags are specified");
      }
    }

    // Check exclusion between use-host-ptr and others
    if ((flags & CL_MEM_USE_HOST_PTR) && (flags & CL_MEM_ALLOC_HOST_PTR)) {
      BAIL_INFO(CL_INVALID_VALUE, context,
                "Flags CL_MEM_USE_HOST_PTR and CL_MEM_ALLOC_HOST_PTR are both "
                "specified but are mutually exclusive");
    }
    if ((flags & CL_MEM_USE_HOST_PTR) && (flags & CL_MEM_COPY_HOST_PTR)) {
      BAIL_INFO(CL_INVALID_VALUE, context,
                "Flags CL_MEM_USE_HOST_PTR and CL_MEM_COPY_HOST_PTR are both "
                "specified but are mutually exclusive");
    }
  }

  // Check host_ptr
  if (host_ptr == 0 && (flags & CL_MEM_USE_HOST_PTR)) {
    BAIL_INFO(CL_INVALID_HOST_PTR, context,
              "Flag CL_MEM_USE_HOST_PTR is specified, but no host pointer is "
              "provided");
  }
  if (host_ptr == 0 && (flags & CL_MEM_COPY_HOST_PTR)) {
    BAIL_INFO(CL_INVALID_HOST_PTR, context,
              "Flag CL_MEM_COPY_HOST_PTR is specified, but no host pointer is "
              "provided");
  }
  if (host_ptr != 0 &&
      !(flags & (CL_MEM_USE_HOST_PTR | CL_MEM_COPY_HOST_PTR))) {
    BAIL_INFO(CL_INVALID_HOST_PTR, context,
              "A host pointer is provided without also specifying one of "
              "CL_MEM_USE_HOST_PTR or CL_MEM_COPY_HOST_PTR");
  }

  // Check size
  if (size == 0) {
    BAIL_INFO(CL_INVALID_BUFFER_SIZE, context,
              "Memory buffer cannot be of size zero");
  }
  // If using host memory, then just accept any size.
  if (!(flags & CL_MEM_USE_HOST_PTR) && (size > context->max_mem_alloc_size)) {
    BAIL_INFO(CL_INVALID_BUFFER_SIZE, context,
              "Requested memory object size exceeds device limits");
  }

#endif

  auto *new_block = acl_new<acl_block_allocation_t>();
  if (!new_block) {
    BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
              "Could not allocate a cl_mem object");
  }

  // Now actually allocate the mem object.
  mem = acl_alloc_cl_mem();
  if (!mem) {
    acl_delete(new_block);
    BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
              "Could not allocate a cl_mem object");
  }
  mem->mem_id = tmp_mem_id;

  mem->block_allocation = new_block;
  mem->block_allocation->mem_obj = mem;

  acl_reset_ref_count(mem);

  mem->mem_object_type = CL_MEM_OBJECT_BUFFER;
  mem->dispatch = &acl_icd_dispatch;
  mem->allocation_deferred = 0;
  mem->host_mem.aligned_ptr = NULL;
  mem->host_mem.raw = NULL;
  mem->mem_cpy_host_ptr_pending = 0;
  mem->destructor_callback_list = NULL;
  mem->fields.buffer_objs.is_subbuffer = CL_FALSE;
  mem->fields.buffer_objs.parent = NULL;
  mem->fields.buffer_objs.next_sub = NULL;
  mem->fields.buffer_objs.sub_origin = 0;
  mem->fields.buffer_objs.parent = 0;
  mem->fields.buffer_objs.num_subbuffers = 0;
  // Determine what region it goes into.
  if (flags & CL_MEM_USE_HOST_PTR) {
    mem->block_allocation->region = &(acl_platform.host_user_mem);
    mem->block_allocation->range.begin = host_ptr;
    mem->block_allocation->range.next = (void *)((char *)host_ptr + size);
#ifdef ACL_HOST_MEMORY_SHARED
  } else if (flags & CL_MEM_ALLOC_HOST_PTR) {
    mem->block_allocation->region = &(acl_platform.host_auto_mem);
#endif
  } else {
    // CL_MEM_ALLOC_HOST_PTR is handled by this section.
    // The caller wants device global memory.
    // Special case: If CL_MEM_COPY_HOST_PTR then the buffer is in *global*
    // memory and we have to schedule a copy now.

    mem->block_allocation->region = context->global_mem;
    if (mem->block_allocation->region == &(context->emulated_global_mem)) {
      // Will use host malloc, so no working_range or banking required.
      acl_print_debug_msg("emulating memory %p %p\n",
                          mem->block_allocation->region,
                          &(context->emulated_global_mem));
    }
  }

  // Device buffers that are not host accessible always start
  // as not being mapped to the host.
  // Set the home location for this buffer.
  mem->writable_copy_on_host =
      mem->block_allocation->region->is_host_accessible;

  // Determine if this is SVM memory and, if so, allocate the memory for it
  context_has_device_with_physical_mem = CL_FALSE;
  context_has_device_with_only_svm = CL_FALSE;
  for (idevice = 0; idevice < context->num_devices; ++idevice) {
    if (acl_svm_device_supports_physical_memory(
            context->device[idevice]->def.physical_device_id)) {
      context_has_device_with_physical_mem = CL_TRUE;
    } else if (acl_svm_device_supports_any_svm(
                   context->device[idevice]->def.physical_device_id)) {
      context_has_device_with_only_svm = CL_TRUE;
    }
  }

  if ((flags & CL_MEM_USE_HOST_PTR &&
       acl_ptr_is_contained_in_context_svm(context, host_ptr)) ||
      (context_has_device_with_only_svm &&
       !context_has_device_with_physical_mem)) {
    mem->is_svm = CL_TRUE;
  } else if (context_has_device_with_only_svm &&
             context_has_device_with_physical_mem) {
    acl_delete(mem->block_allocation);
    acl_free_cl_mem(mem);
    BAIL_INFO(CL_MEM_OBJECT_ALLOCATION_FAILURE, context,
              "Detected devices with only SVM and on-board memory in the same "
              "context. Altera does not currently support this combination and "
              "cannot allocate requested memory object.");
  } else {
    mem->is_svm = CL_FALSE;
  }

  if (mem->block_allocation->region->is_user_provided) {
    if (mem->is_svm &&
        host_ptr != (void *)ACL_MEM_ALIGN) { // Special case: if host_ptr =
                                             // ACL_MEM_ALIGN, then this is the
                                             // context->unwrapped_host_mem,
                                             // which can never be svmAlloc-ed.
      if (acl_ptr_is_contained_in_context_svm(context, host_ptr)) {
        acl_aligned_ptr_t ptr;
        ptr.device_addr = 0;
        ptr.raw = host_ptr;
        ptr.aligned_ptr = (void *)(((size_t)ptr.raw + (ACL_MEM_ALIGN - 1)) &
                                   ~((size_t)(ACL_MEM_ALIGN - 1)));
        ptr.alignment =
            ACL_MEM_ALIGN; // data will be aligned to at least one page.
        ptr.size = size;
        mem->host_mem = ptr;
      } else {
        BAIL_INFO(
            CL_INVALID_HOST_PTR, context,
            "On a system that only supports SVM and does not support "
            "fine-grained system SVM, "
            " provided host pointers must be allocated using clSVMAlloc.");
      }
    }
    result = mem;
  } else if (mem->block_allocation->region->uses_host_system_malloc ||
             mem->is_svm) {
// ARM SoC with 1 bank or BSP with only SVM
// Allocate something slightly larger to ensure alignment.
#ifdef ACL_HOST_MEMORY_SHARED
    if (acl_get_hal() == NULL || acl_get_hal()->legacy_shared_alloc == NULL) {
      // No shared_alloc function in kernel capture mode for conformance tests
      // (CL_CONTEXT_COMPILER_MODE_INTELFPGA=1 flow)
      // Just alloc the memory normally.
      mem->host_mem = acl_mem_aligned_malloc(size);
    } else {

      // Ask the driver for physically-contiguous memory.
      acl_aligned_ptr_t ptr;
      ptr.raw =
          acl_get_hal()->legacy_shared_alloc(context, size, &ptr.device_addr);

      // using size_t. Will break when have 64-bit pointers with 32-bit ARM cpu.
      // assuming shared memory for CV SoC is bank #1 if there are
      // two banks and bank #0 if there is only one bank.

      // Assume ARM is always going to have one device !
      acl_addr_range_t cur_working_range =
          acl_platform.device[0].def.autodiscovery_def.global_mem_defs[0].range;
      size_t cur_num_banks = acl_platform.device[0]
                                 .def.autodiscovery_def.global_mem_defs[0]
                                 .num_global_banks;
      size_t cur_bank_size;
      size_t start_of_bank_one = 0;
      if (cur_num_banks == 1) {
        // shared memory is bank #0
        start_of_bank_one = (size_t)cur_working_range.begin;
      } else if (cur_num_banks == 2) {
        // shared memory is bank #1
        cur_bank_size =
            ((size_t)cur_working_range.next - (size_t)cur_working_range.begin) /
            cur_num_banks;
        start_of_bank_one = (size_t)cur_working_range.begin + cur_bank_size;
      }

      ptr.device_addr += start_of_bank_one;
      ptr.aligned_ptr = ptr.raw;
      ptr.alignment =
          ACL_MEM_ALIGN; // data will be aligned to at least one page.
      ptr.size = size;
      mem->host_mem = ptr;
    }
#else
#ifdef SYSTEM_SVM
    // can pass in any pointer to FPGA, alloc using normal systemn call
    mem->host_mem = acl_mem_aligned_malloc(size);
#else
    // do the same thing as ARM (without special cases for non-shared bank)
    // alloc with legacy_shared_alloc if available and device only has SVM
    if (!context_has_device_with_only_svm || acl_get_hal() == NULL ||
        acl_get_hal()->legacy_shared_alloc == NULL) {
      // No shared_alloc function in kernel capture mode for conformance tests
      // (CL_CONTEXT_COMPILER_MODE_INTELFPGA=1 flow)
      // Just alloc the memory normally.
      mem->host_mem = acl_mem_aligned_malloc(size);
    } else {
      // Ask the mmd for memory.
      acl_aligned_ptr_t ptr;
      ptr.raw =
          acl_get_hal()->legacy_shared_alloc(context, size, &ptr.device_addr);
      ptr.aligned_ptr = ptr.raw;
      ptr.alignment =
          ACL_MEM_ALIGN; // data will be aligned to at least one page.
      ptr.size = size;
      mem->host_mem = ptr;
    }
#endif // SYSTEM_SVM
#endif // ACL_HOST_MEMORY_SHARED

    if (mem->host_mem.raw == 0) {
      acl_free_cl_mem(mem);
      BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
                "Could not allocate a buffer in host memory");
    }
    mem->block_allocation->range.begin = mem->host_mem.aligned_ptr;
    mem->block_allocation->range.next =
        (void *)(((char *)mem->host_mem.aligned_ptr) + size);

    // Put it on the allocation list
    mem->block_allocation->next_block_in_region =
        mem->block_allocation->region->first_block;
    mem->block_allocation->region->first_block = mem->block_allocation;

    // Use the first device available in the context
    int device_id = context->device[0]->id;
    unsigned int physical_id =
        acl_platform.device[device_id].def.physical_device_id;
    if (mem->reserved_allocations[physical_id].size() == 0) {
      acl_resize_reserved_allocations_for_device(
          mem, acl_platform.device[device_id].def);
    }
    mem->reserved_allocations[physical_id][mem->mem_id] = mem->block_allocation;

    result = mem;
  } else {
    // Perform our own first-fit allocation algorithm in the region.
    // Assign to result only if we succeeded.

    // Try getting host backing store first, if required.
    // Do this first before modifying any of our other bookkeeping.

    if ((context->device_buffers_have_backing_store ||
         (((context->num_devices > 1) ||
           (flags & CL_MEM_HETEROGENEOUS_INTELFPGA)) &&
          (flags & CL_MEM_COPY_HOST_PTR))) &&
        !mem->block_allocation->region
             ->is_host_accessible // needs backing store
    ) {
      mem->host_mem = acl_mem_aligned_malloc(size);
      if (mem->host_mem.raw == 0) {
        acl_delete(mem->block_allocation);
        acl_free_cl_mem(mem);
        BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
                  "Could not allocate backing store for a device buffer");
      }
    }

    mem->context = context;
    mem->flags = flags;
    mem->size = size;

    mem->bank_id = 0;
    if (is_SOC_device()) {
      // HPS DDR is system managed for SoC.
      // Only allocate on device DDR.
      acl_print_debug_msg("Ignoring CHANNEL setting on ARM\n");
    } else {
      // Using channel flag for setting bank id
      if ((flags & CL_CHANNEL_7_INTELFPGA) != CL_CHANNEL_AUTO_INTELFPGA) {
        bank_id =
            ((cl_uint)flags & CL_CHANNEL_7_INTELFPGA) / CL_CHANNEL_1_INTELFPGA;
      }
      mem->bank_id = bank_id;
    }

    // For device global memory, we will defer the allocation until we know
    // where it should go
    if (mem->block_allocation->region == &acl_platform.global_mem) {
      mem->allocation_deferred = 1;
      result = mem;
    } else {
      // physical_device_id is set to 0 because it's not used
      // when mem->block_allocation->region != &acl_platform.global_mem
      if (acl_do_physical_buffer_allocation(0, mem))
        result = mem;
      else
        result = 0;
    }

    // Out of memory, or fragmentation is too bad.
    if (result == 0) {
      cl_int code = mem->block_allocation->region->is_host_accessible
                        ? CL_OUT_OF_HOST_MEMORY
                        : CL_OUT_OF_RESOURCES;
      acl_delete(mem->block_allocation);
      acl_free_cl_mem(mem);
      BAIL_INFO(code, context,
                "Could not allocate a buffer of the specified size due to "
                "fragmentation or exhaustion");
    }
  }

  result->context = context;
  result->flags = flags;
  result->size = size;
  result->fields.buffer_objs.host_ptr = host_ptr;
  result->mapping_count = 0;

  result->bank_id = 0;
  if (is_SOC_device()) {
    // HPS DDR is system managed for SoC.
    // Only allocate on device DDR.
    acl_print_debug_msg("Ignoring CHANNEL setting on ARM\n");
  } else {
    // Using channel flag for setting bank id
    if ((flags & CL_CHANNEL_7_INTELFPGA) != CL_CHANNEL_AUTO_INTELFPGA) {
      bank_id =
          ((cl_uint)flags & CL_CHANNEL_7_INTELFPGA) / CL_CHANNEL_1_INTELFPGA;
    }
    result->bank_id = bank_id;
  }

  acl_retain(result);
  acl_retain(context);

  // Handling the special case when clCreateBuffer is called with the
  // 'CL_MEM_COPY_HOST_PTR' flag. What do we have to do in this case? The user
  // is telling us to make a copy of this pointer NOW (i.e. before returning).
  // So, we have two options right now:
  //      1) Defer the copy to the device(s) to a later time by making a local
  //         copy of the data in host memory.
  //      2) Pre-emptively write the data to ALL devices in the context.
  //
  // (1) is the safer option but, for the trivial case of a single-device
  // context, it results in a performance hit by doing an extra
  // host->host memcpy.
  // (2) could save us the extra memcpy from (1) for the common single-device
  // context case, but this preemptive broadcast write to all devices could
  // result in a TON of extra host->device copies in multi-device contexts
  // and oversubscription of memory!
  //
  // A middle ground between (1) and (2) would be to do (2) if there is a
  // single device in the context, and do (1) if there are multiple devices
  // in the context (we only know the context of the buffer, we don't know the
  // specific device, yet).
  // However, this middle ground could confuse users; if their OpenCL program
  // only uses one device (e.g. they always use device[0]), then their
  // performance will change depending on the number of devices in their
  // context!
  if (flags & CL_MEM_COPY_HOST_PTR) {
    if (mem->allocation_deferred) {
// The case where we will defer the copy to when we know which device
// the buffer is bound to. Based on the earlier code in this function
// that sets 'mem->allocation_deferred', we will enter this block
// whenever the target memory on the device is global memory. We defer
// to a later time, but we'd better make a copy somewhere. We will
// make a copy in host memory for now with a blocking memcpy.
// This is an 'extra' memcpy and can hurt performance.
#ifdef MEM_DEBUG_MSG
      printf("clCreateBuffer called with CL_MEM_COPY_HOST_PTR, making a "
             "temporary copy before returning!\n");
#endif
      safe_memcpy(mem->host_mem.aligned_ptr, host_ptr, size, size, size);

      // This flag indicates that we haven't actually performed the copy of
      // the memory; it is just in the host_mem storage.
      mem->mem_cpy_host_ptr_pending = 1;
      mem->writable_copy_on_host = 1;

      // Don't need to skip the check since this
      // code branch doesn't internally call clEnqueueWriteBuffer.
      mem->copy_host_ptr_skip_check = CL_FALSE;
    } else {
      // The case where we will directly copy the data to device. Based on
      // the code earlier in this function, which sets
      // 'mem->allocation_deferred', the only time we will enter this
      // else-block is if the target memory on the device is NOT global
      // memory. I think the rational here is device memory that is
      // host-accessible but NOT global memory will be "specific". For us,
      // maybe some specific on-chip FPGA memory?

      // We will do a direct blocking copy now, so we will do our own error
      // checking once the copy is done.
      mem->copy_host_ptr_skip_check = CL_TRUE;

      // Enqueue the buffer write.
      // Must be blocking since the call has to finish before returning
      // from this function. We choose 'auto_queue' queue here which is tied
      // to the default device (device[0], see acl_context.cpp).
      cl_int status = clEnqueueWriteBuffer(
          context->auto_queue, result,
          1, // blocking write
          0, // offset
          result->size, result->fields.buffer_objs.host_ptr, 0, 0, // wait list
          0);

      // Check the status of the write once it returns
      if (status != CL_SUCCESS) {
        acl_release(result);
        acl_release(context);
        acl_delete(mem->block_allocation);
        acl_free_cl_mem(mem);
        // Need an error status valid to return from this function
        BAIL_INFO(CL_OUT_OF_RESOURCES, context,
                  "Could not copy data into the allocated buffer");
      }
    }
  }

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  acl_track_object(ACL_OBJ_MEM_OBJECT, result);

#ifdef MEM_DEBUG_MSG
  printf("CreateBuffer Finished:  %zx\n", (size_t)result);
#endif

  return result;
}

// Create a buffer.
// In some cases, we copy memory from host to global memory.
//
// If the user specified USE_HOST_MEMORY, then when a kernel is invoked
// with that mem object, we will have to schedule two copy operations:
// Before the kernel starts, copy the buffer from host to global mem;
// after the kernel ends, copy the buffer from global mem to host mem.
ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL clCreateBufferIntelFPGA(cl_context context,
                                                        cl_mem_flags flags,
                                                        size_t size,
                                                        void *host_ptr,
                                                        cl_int *errcode_ret) {
  return clCreateBufferWithPropertiesINTEL(context, NULL, flags, size, host_ptr,
                                           errcode_ret);
}

// Create a buffer with additional properties
ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL clCreateBufferWithProperties(
    cl_context context, const cl_mem_properties *properties, cl_mem_flags flags,
    size_t size, void *host_ptr, cl_int *errcode_ret) {
  return clCreateBufferWithPropertiesINTEL(context, properties, flags, size,
                                           host_ptr, errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL clCreateBuffer(cl_context context,
                                               cl_mem_flags flags, size_t size,
                                               void *host_ptr,
                                               cl_int *errcode_ret) {
  return clCreateBufferIntelFPGA(context, flags, size, host_ptr, errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL clCreateSubBufferIntelFPGA(
    cl_mem buffer, cl_mem_flags flags, cl_buffer_create_type buffer_create_type,
    const void *buffer_create_info, cl_int *errcode_ret) {
  cl_mem result = 0;
  cl_bool aligns_with_any_device = CL_FALSE;
  cl_mem_flags sub_flags;
  unsigned int idevice;
  cl_context context;

  cl_mem mem;
  int num_rw_specs = 0;

  std::scoped_lock lock{acl_mutex_wrapper};

#ifdef MEM_DEBUG_MSG
  printf("CreateSubBuffer");
#endif

  if (!acl_mem_is_valid(buffer)) {
    BAIL(CL_INVALID_MEM_OBJECT);
  }
  if (buffer->mem_object_type != CL_MEM_OBJECT_BUFFER ||
      buffer->fields.buffer_objs.is_subbuffer) {
    BAIL(CL_INVALID_MEM_OBJECT);
  }
  context = buffer->context;

  // Check for invalid enum bits
  // CL_MEM_USE_HOST_PTR, CL_MEM_ALLOC_HOST_PTR and CL_MEM_COPY_HOST_PTR are
  // not valid flags for sub-buffers so are removed from this list
  if (flags &
      ~(CL_MEM_READ_WRITE | CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY |
        CL_MEM_HOST_WRITE_ONLY | CL_MEM_HOST_READ_ONLY | CL_MEM_HOST_NO_ACCESS |
        CL_CHANNEL_7_INTELFPGA | CL_MEM_HETEROGENEOUS_INTELFPGA)) {
    BAIL_INFO(CL_INVALID_VALUE, context, "Invalid or unsupported flags");
  }

  // Check for exactly one read/write spec
  if (flags & CL_MEM_READ_WRITE)
    num_rw_specs++;
  if (flags & CL_MEM_READ_ONLY)
    num_rw_specs++;
  if (flags & CL_MEM_WRITE_ONLY)
    num_rw_specs++;
  if (num_rw_specs > 1) {
    BAIL_INFO(CL_INVALID_VALUE, context,
              "More than one read/write flag is specified");
  }

  // Check for exactly one host read/write/no_access spec
  num_rw_specs = 0;
  if (flags & CL_MEM_HOST_READ_ONLY)
    num_rw_specs++;
  if (flags & CL_MEM_HOST_WRITE_ONLY)
    num_rw_specs++;
  if (flags & CL_MEM_HOST_NO_ACCESS)
    num_rw_specs++;
  if (num_rw_specs > 1) {
    BAIL_INFO(CL_INVALID_VALUE, context,
              "More than one host read/write/no_access flags are specified");
  }

  // If the parent buffer is write only then the sub-buffer cannot read.
  // If the parent buffer is read only then the sub-buffer cannot write.
  if (((buffer->flags & CL_MEM_WRITE_ONLY) &&
       ((flags & CL_MEM_READ_WRITE) || (flags & CL_MEM_READ_ONLY))) ||
      ((buffer->flags & CL_MEM_READ_ONLY) &&
       ((flags & CL_MEM_READ_WRITE) || (flags & CL_MEM_WRITE_ONLY))) ||
      ((buffer->flags & CL_MEM_HOST_WRITE_ONLY) &&
       (flags & CL_MEM_HOST_READ_ONLY)) ||
      ((buffer->flags & CL_MEM_HOST_READ_ONLY) &&
       (flags & CL_MEM_HOST_WRITE_ONLY)) ||
      ((buffer->flags & CL_MEM_HOST_NO_ACCESS) &&
       ((flags & CL_MEM_HOST_READ_ONLY) || (flags & CL_MEM_HOST_WRITE_ONLY)))) {
    BAIL_INFO(CL_INVALID_VALUE, context,
              "Read/write flags are incompatible with the parent buffer");
  }

  if (buffer_create_type != CL_BUFFER_CREATE_TYPE_REGION) {
    BAIL_INFO(CL_INVALID_VALUE, context, "Invalid buffer_create_type value");
  }

  if (buffer_create_info == NULL) {
    BAIL_INFO(CL_INVALID_VALUE, context, "Empty buffer_create_info");
  }

  if (((cl_buffer_region *)buffer_create_info)->origin +
          ((cl_buffer_region *)buffer_create_info)->size >
      buffer->size) {
    BAIL_INFO(CL_INVALID_VALUE, context,
              "Origin plus size is out of bounds of parent buffer");
  }

  if (((cl_buffer_region *)buffer_create_info)->size == 0) {
    BAIL_INFO(CL_INVALID_BUFFER_SIZE, context, "Sub-buffer size is zero");
  }

  for (idevice = 0; idevice < context->num_devices; ++idevice) {
    int device_mem_base_addr_align = 0;
    int status_code;

    status_code = clGetDeviceInfoIntelFPGA(
        context->device[idevice], CL_DEVICE_MEM_BASE_ADDR_ALIGN, sizeof(int),
        &device_mem_base_addr_align, NULL);
    if (status_code != CL_SUCCESS) {
      BAIL(CL_OUT_OF_HOST_MEMORY);
    }

    if (!((((cl_buffer_region *)buffer_create_info)->origin * 8) &
          (device_mem_base_addr_align - 1))) {
      aligns_with_any_device = CL_TRUE;
      break;
    }
  }

  if (!aligns_with_any_device) {
    BAIL_INFO(CL_MISALIGNED_SUB_BUFFER_OFFSET, context,
              "Sub-buffer offset does not align with any device in context");
  }

  acl_block_allocation_t *new_block = acl_new<acl_block_allocation_t>();
  if (!new_block) {
    BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
              "Could not allocate a cl_mem object");
  }
  // Now actually allocate the mem object.
  mem = acl_alloc_cl_mem();
  if (!mem) {
    acl_delete(new_block);
    BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
              "Could not allocate a cl_mem object");
  }

  mem->block_allocation = new_block;
  mem->block_allocation->mem_obj = mem;

  acl_reset_ref_count(mem);

  sub_flags = flags;

  // If read/write flags are not specified, they are inherited from the parent
  // buffer
  if (!(flags & (CL_MEM_READ_WRITE | CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY))) {
    sub_flags |= (buffer->flags &
                  (CL_MEM_READ_WRITE | CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY));
  }
  // If host read/write flags are not specified, they are inherited from the
  // parent buffer
  if (!(flags & (CL_MEM_HOST_WRITE_ONLY | CL_MEM_HOST_READ_ONLY |
                 CL_MEM_HOST_NO_ACCESS))) {
    sub_flags |=
        (buffer->flags & (CL_MEM_HOST_WRITE_ONLY | CL_MEM_HOST_READ_ONLY |
                          CL_MEM_HOST_NO_ACCESS));
  }

  sub_flags |= (buffer->flags & (CL_MEM_USE_HOST_PTR | CL_MEM_ALLOC_HOST_PTR |
                                 CL_MEM_COPY_HOST_PTR));

  mem->mem_object_type = CL_MEM_OBJECT_BUFFER;
  mem->dispatch = &acl_icd_dispatch;
  mem->allocation_deferred = 0;

  // If we have sub buffers, we need to be able to mirror the data to the host
  if (!buffer->host_mem.aligned_ptr) {
    // Initialize the host_mem entries for the parent buffer
    if (buffer->block_allocation->region
            ->is_user_provided) { // CL_MEM_USE_HOST_PTR
      // When a buffer is created using CL_MEM_USE_HOST_PTR, there is no need to
      // allocate additional memory on the host for this buffer. Simply
      // initialize host_ptr_mem to point into the inputted host memory region.
      acl_aligned_ptr_t host_ptr_mem;
      host_ptr_mem.aligned_ptr = buffer->block_allocation->range.begin;
      host_ptr_mem.raw = buffer->block_allocation->range.begin;
      host_ptr_mem.alignment = 0;
      host_ptr_mem.size = ((cl_buffer_region *)buffer_create_info)->size;
      buffer->host_mem = host_ptr_mem;
    } else {
      buffer->host_mem = acl_mem_aligned_malloc(buffer->size);
      if (!buffer->host_mem.raw) {
        acl_free_cl_mem(mem);
        BAIL_INFO(
            CL_OUT_OF_HOST_MEMORY, context,
            "Could not allocate backing store for a device buffer with sub "
            "buffers");
      }
    }
  }
  // Uses the same host location as the main buffer, but offsets
  // into it
  mem->host_mem.aligned_ptr = (char *)buffer->host_mem.aligned_ptr +
                              ((cl_buffer_region *)buffer_create_info)->origin;

  if (buffer->host_mem.device_addr != 0) {
    mem->host_mem.device_addr =
        buffer->host_mem.device_addr +
        ((cl_buffer_region *)buffer_create_info)->origin;
  } else {
    mem->host_mem.device_addr = 0;
  }
  // Not used for sub buffers
  mem->host_mem.raw = NULL;
  mem->host_mem.size = 0;
  mem->host_mem.alignment = 0;

  mem->mem_cpy_host_ptr_pending = 0;
  mem->destructor_callback_list = NULL;
  mem->fields.buffer_objs.is_subbuffer = CL_TRUE;
  mem->fields.buffer_objs.parent = buffer;
  mem->fields.buffer_objs.num_subbuffers = 0;
  // Insert at the head of the list of sub buffers for the parent object
  mem->fields.buffer_objs.next_sub = buffer->fields.buffer_objs.next_sub;
  buffer->fields.buffer_objs.next_sub = mem;
  if (mem->fields.buffer_objs.next_sub != NULL) {
    mem->fields.buffer_objs.next_sub->fields.buffer_objs.prev_sub = mem;
  }
  mem->fields.buffer_objs.prev_sub = buffer;
  ++buffer->fields.buffer_objs.num_subbuffers;

  mem->fields.buffer_objs.sub_origin =
      ((cl_buffer_region *)buffer_create_info)->origin;
  mem->size = ((cl_buffer_region *)buffer_create_info)->size;

  mem->block_allocation->region = buffer->block_allocation->region;
  // Always start on the host.
  // If parent buffer or overlapping subbuffer data is on a device,
  // it will be copied back to the host before the sub buffer is used.
  mem->writable_copy_on_host = 1;
  mem->auto_mapped = 1;

  mem->is_svm = buffer->is_svm;

  if (mem->block_allocation->region->is_user_provided) { // CL_MEM_USE_HOST_PTR
    // We'll set the block allocation to point into the block allocation of the
    // parent buffer. We won't put it in the linked list. This is probably
    // "bad", but you can't release the parent buffer before the child buffer so
    // it shouldn't be possible for anyone else to attempt to allocate the same
    // region, and when memory is released in
    // acl_forcibly_release_all_memory_for_context, the release of the parent
    // buffer should release this memory as well.

    mem->block_allocation->range.begin = mem->host_mem.aligned_ptr;
    mem->block_allocation->range.next =
        (void *)(((char *)mem->host_mem.aligned_ptr) + mem->size);
    // Don't put it in the allocation list
    mem->block_allocation->next_block_in_region = NULL;
    mem->block_allocation->region->first_block = NULL;

    result = mem;
  } else if (mem->block_allocation->region->uses_host_system_malloc ||
             mem->is_svm) {
    mem->block_allocation->range.begin = mem->host_mem.aligned_ptr;
    mem->block_allocation->range.next =
        (void *)(((char *)mem->host_mem.aligned_ptr) + mem->size);

    // Put it on the allocation list
    mem->block_allocation->next_block_in_region =
        mem->block_allocation->region->first_block;
    mem->block_allocation->region->first_block = mem->block_allocation;

    result = mem;
  } else {
    // Perform our own first-fit allocation algorithm in the region.
    // Assign to result only if we succeeded.

    mem->context = context;
    mem->flags = sub_flags;
    mem->mem_id = buffer->mem_id;

    if (is_SOC_device()) {
      // HPS DDR is system managed for SoC.
      // Only allocate on device DDR.
      acl_print_debug_msg("Ignoring CHANNEL setting on ARM\n");
    } else {
      // Ensure that if a bank id is specified, it is the same as the parent
      // buffer
      if ((sub_flags & CL_CHANNEL_7_INTELFPGA) != CL_CHANNEL_AUTO_INTELFPGA) {
        cl_uint sub_bank_id = ((cl_uint)sub_flags & CL_CHANNEL_7_INTELFPGA) /
                              CL_CHANNEL_1_INTELFPGA;
        if (sub_bank_id != buffer->bank_id) {
          BAIL_INFO(CL_INVALID_VALUE, context,
                    "Sub-buffer bank id does not match parent buffer bank id");
        }
      }
    }
    mem->bank_id = buffer->bank_id;

    // For device global memory, we will defer the allocation until we know
    // where it should go
    if (mem->block_allocation->region == &acl_platform.global_mem) {
      mem->allocation_deferred = 1;
      result = mem;
    } else {
      // physical_device_id is set to 0 because it's not used
      // when mem->block_allocation->region != &acl_platform.global_mem
      if (acl_do_physical_buffer_allocation(0, mem))
        result = mem;
      else
        result = 0;
    }

    // Out of memory, or fragmentation is too bad.
    // It's ok to bail here because we haven't damaged the .link on the
    // candidate cl_mem object, nor updated the free list link.
    if (result == 0) {
      cl_int code = mem->block_allocation->region->is_host_accessible
                        ? CL_OUT_OF_HOST_MEMORY
                        : CL_OUT_OF_RESOURCES;
      acl_free_cl_mem(mem);
      BAIL_INFO(code, context,
                "Could not allocate a buffer of the specified size due to "
                "fragmentation or exhaustion");
    }
  }

  result->context = context;
  result->flags = sub_flags;
  result->fields.buffer_objs.host_ptr =
      (char *)buffer->fields.buffer_objs.host_ptr +
      ((cl_buffer_region *)buffer_create_info)->origin;
  result->mapping_count = 0;

  if (is_SOC_device()) {
    // HPS DDR is system managed for SoC.
    // Only allocate on device DDR.
    acl_print_debug_msg("Ignoring CHANNEL setting on ARM\n");
  } else {
    // Ensure that if a bank id is specified, it is the same as the parent
    // buffer
    if ((sub_flags & CL_CHANNEL_7_INTELFPGA) != CL_CHANNEL_AUTO_INTELFPGA) {
      cl_uint sub_bank_id = ((cl_uint)sub_flags & CL_CHANNEL_7_INTELFPGA) /
                            CL_CHANNEL_1_INTELFPGA;
      if (sub_bank_id != buffer->bank_id) {
        BAIL_INFO(CL_INVALID_VALUE, context,
                  "Sub-buffer bank id does not match parent buffer bank id");
      }
    }
  }
  result->bank_id = buffer->bank_id;

  acl_retain(result);
  acl_retain(result->fields.buffer_objs.parent);
  acl_retain(result->context);

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  acl_track_object(ACL_OBJ_MEM_OBJECT, result);

#ifdef MEM_DEBUG_MSG
  printf(" %zx\n", (size_t)result);
#endif

  return result;
}

ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL clCreateSubBuffer(
    cl_mem buffer, cl_mem_flags flags, cl_buffer_create_type buffer_create_type,
    const void *buffer_create_info, cl_int *errcode_ret) {
  return clCreateSubBufferIntelFPGA(buffer, flags, buffer_create_type,
                                    buffer_create_info, errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetMemObjectInfoIntelFPGA(
    cl_mem mem, cl_mem_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret) {
  acl_result_t result;
  cl_context context;
  std::scoped_lock lock{acl_mutex_wrapper};

  RESULT_INIT;

  if (!acl_mem_is_valid(mem)) {
    return CL_INVALID_MEM_OBJECT;
  }

  context = mem->context;

  switch (param_name) {
  case CL_MEM_ALLOC_BUFFER_LOCATION_INTEL:
    RESULT_UINT(mem->mem_id);
    break;
  case CL_MEM_TYPE:
    RESULT_ENUM(mem->mem_object_type);
    break;
  case CL_MEM_FLAGS:
    RESULT_BITFIELD(mem->flags);
    break;
  case CL_MEM_SIZE:
    RESULT_SIZE_T(mem->size);
    break; // ignores alignment adjustment
  case CL_MEM_HOST_PTR:
    if (mem->mem_object_type == CL_MEM_OBJECT_BUFFER)
      RESULT_PTR(mem->fields.buffer_objs.host_ptr);
    if (is_image(mem))
      RESULT_PTR(mem->fields.image_objs.host_ptr);
    if (mem->mem_object_type == CL_MEM_OBJECT_PIPE)
      RESULT_PTR(NULL);
    break;
  case CL_MEM_MAP_COUNT:
    RESULT_UINT(mem->mapping_count);
    break;
  case CL_MEM_REFERENCE_COUNT:
    if (mem->mem_object_type == CL_MEM_OBJECT_BUFFER) {
      RESULT_UINT(acl_ref_count(mem) -
                  (cl_uint)mem->fields.buffer_objs.num_subbuffers);
    } else {
      RESULT_UINT(acl_ref_count(mem));
    }
    break;
  case CL_MEM_CONTEXT:
    RESULT_PTR(mem->context);
    break;
  case CL_MEM_USES_SVM_POINTER:
    if (mem->mem_object_type == CL_MEM_OBJECT_BUFFER) {
      RESULT_BOOL(
          (cl_bool)(mem->fields.buffer_objs.host_ptr != NULL &&
                    acl_ptr_is_contained_in_context_svm(
                        mem->context, mem->fields.buffer_objs.host_ptr)));
    } else if (is_image(mem)) {
      RESULT_BOOL(
          (cl_bool)(mem->fields.image_objs.host_ptr != NULL &&
                    acl_ptr_is_contained_in_context_svm(
                        mem->context, mem->fields.image_objs.host_ptr)));
    } else {
      RESULT_BOOL(CL_FALSE);
    }
    break;
  case CL_MEM_ASSOCIATED_MEMOBJECT:
    if (mem->mem_object_type == CL_MEM_OBJECT_BUFFER &&
        mem->fields.buffer_objs.is_subbuffer) {
      RESULT_PTR(mem->fields.buffer_objs.parent);
      break;
    } else if (is_image(mem)) {
      RESULT_PTR(mem->fields.image_objs.image_desc->mem_object);
      break;
    } else {
      RESULT_PTR(NULL);
      break;
    }
  case CL_MEM_OFFSET:
    if (mem->mem_object_type == CL_MEM_OBJECT_BUFFER &&
        mem->fields.buffer_objs.is_subbuffer) {
      RESULT_SIZE_T(mem->fields.buffer_objs.sub_origin);
      break;
    } else {
      RESULT_SIZE_T(0);
    }
  default:
    break;
  }

  if (result.size == 0) {
    ERR_RET(CL_INVALID_VALUE, context,
            "Invalid or unsupported memory object query");
  }

  if (param_value) {
    if (param_value_size < result.size) {
      ERR_RET(CL_INVALID_VALUE, context,
              "Parameter return buffer is too small");
    }
    RESULT_COPY(param_value, param_value_size);
  }

  if (param_value_size_ret) {
    *param_value_size_ret = result.size;
  }
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL
clGetMemObjectInfo(cl_mem mem, cl_mem_info param_name, size_t param_value_size,
                   void *param_value, size_t *param_value_size_ret) {
  return clGetMemObjectInfoIntelFPGA(mem, param_name, param_value_size,
                                     param_value, param_value_size_ret);
}

ACL_EXPORT CL_API_ENTRY cl_mem CL_API_CALL clCreateImageIntelFPGA(
    cl_context context, cl_mem_flags flags, const cl_image_format *image_format,
    const cl_image_desc *image_desc, void *host_ptr, cl_int *errcode_ret) {
  size_t image_size;
  size_t element_size;
  cl_mem return_buffer;
  cl_int local_errcode_ret = CL_SUCCESS;
  size_t max_2d_image_width = 0;
  size_t max_3d_image_width = 0;
  size_t max_2d_image_height = 0;
  size_t max_3d_image_height = 0;
  size_t max_3d_image_depth = 0;
  cl_uint num_image_formats;
  cl_image_format *supported_image_formats;
  unsigned iformat;
  cl_bool found_image_format;
  unsigned int idevice;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_context_is_valid(context)) {
    BAIL(CL_INVALID_CONTEXT);
  }

  // Check the maximum image sizes for all available devices in the context
  // The image cannot be larger than the smallest image size within this context
  for (idevice = 0; idevice < context->num_devices; ++idevice) {
    size_t size;
    clGetDeviceInfo(context->device[idevice], CL_DEVICE_IMAGE2D_MAX_WIDTH,
                    sizeof(size_t), &size, NULL);
    if (size > max_2d_image_width)
      max_2d_image_width = size;
    clGetDeviceInfo(context->device[idevice], CL_DEVICE_IMAGE3D_MAX_WIDTH,
                    sizeof(size_t), &size, NULL);
    if (size > max_3d_image_width)
      max_3d_image_width = size;
    clGetDeviceInfo(context->device[idevice], CL_DEVICE_IMAGE2D_MAX_HEIGHT,
                    sizeof(size_t), &size, NULL);
    if (size > max_2d_image_height)
      max_2d_image_height = size;
    clGetDeviceInfo(context->device[idevice], CL_DEVICE_IMAGE3D_MAX_HEIGHT,
                    sizeof(size_t), &size, NULL);
    if (size > max_3d_image_height)
      max_3d_image_height = size;
    clGetDeviceInfo(context->device[idevice], CL_DEVICE_IMAGE3D_MAX_DEPTH,
                    sizeof(size_t), &size, NULL);
    if (size > max_3d_image_depth)
      max_3d_image_depth = size;
  }

  if (image_format == NULL) {
    BAIL_INFO(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR, context,
              "image_format is NULL");
  }

  element_size =
      acl_get_image_element_size(context, image_format, &local_errcode_ret);
  if (local_errcode_ret != CL_SUCCESS) {
    BAIL(local_errcode_ret);
  }

  if (image_desc == NULL) {
    BAIL_INFO(CL_INVALID_IMAGE_DESCRIPTOR, context, "image_desc is NULL");
  }

  local_errcode_ret = clGetSupportedImageFormats(
      context, flags, image_desc->image_type, 0, NULL, &num_image_formats);
  if (local_errcode_ret != CL_SUCCESS) {
    BAIL(local_errcode_ret);
  }
  supported_image_formats = (cl_image_format *)acl_malloc(
      sizeof(cl_image_format) * num_image_formats);
  local_errcode_ret = clGetSupportedImageFormats(
      context, flags, image_desc->image_type, num_image_formats,
      supported_image_formats, NULL);
  found_image_format = CL_FALSE;
  if (local_errcode_ret == CL_SUCCESS) {
    for (iformat = 0; iformat < num_image_formats; ++iformat) {
      if (supported_image_formats[iformat].image_channel_order ==
              image_format->image_channel_order &&
          supported_image_formats[iformat].image_channel_data_type ==
              image_format->image_channel_data_type) {
        found_image_format = CL_TRUE;
        break;
      }
    }
  }
  acl_free(supported_image_formats);

  if (local_errcode_ret != CL_SUCCESS) {
    BAIL(local_errcode_ret);
  }
  if (!found_image_format) {
    BAIL_INFO(CL_IMAGE_FORMAT_NOT_SUPPORTED, context,
              "Unsupported image format");
  }

  // Allocate the memory for the image. This size (and sometimes the method)
  // is different for each image type.
  switch (image_desc->image_type) {
  case CL_MEM_OBJECT_IMAGE1D:
    image_size =
        element_size * image_desc->image_width +
        get_offset_for_image_param(context, image_desc->image_type, "data");
    if (image_desc->image_width <= 0) {
      BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                "image width cannot be zero for a 1D object");
    }
    if (image_size > context->max_mem_alloc_size) {
      BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                "image size exceeds maximum alloc size");
    }
    return_buffer =
        clCreateBuffer(context, flags, image_size, host_ptr, errcode_ret);
    if (return_buffer == NULL)
      return NULL;
    return_buffer->fields.image_objs.image_format =
        (cl_image_format *)acl_malloc(sizeof(cl_image_format));
    return_buffer->fields.image_objs.image_desc =
        (cl_image_desc *)acl_malloc(sizeof(cl_image_desc));
    break;
  case CL_MEM_OBJECT_IMAGE1D_BUFFER:
    // Need to actually allocate/assign the buffer data here
    BAIL_INFO(CL_IMAGE_FORMAT_NOT_SUPPORTED, context,
              "Do not support images created from buffers");
    // Need to actually allocate/assign the buffer data here
    break;
  case CL_MEM_OBJECT_IMAGE1D_ARRAY:
    image_size =
        element_size * image_desc->image_width * image_desc->image_array_size +
        get_offset_for_image_param(context, image_desc->image_type, "data");
    if (image_desc->image_width <= 0) {
      BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                "image width cannot be zero for a 1D object");
    }
    if (image_size > context->max_mem_alloc_size) {
      BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                "image size exceeds maximum alloc size");
    }
    return_buffer =
        clCreateBuffer(context, flags, image_size, host_ptr, errcode_ret);
    if (return_buffer == NULL)
      return NULL;
    return_buffer->fields.image_objs.image_format =
        (cl_image_format *)acl_malloc(sizeof(cl_image_format));
    return_buffer->fields.image_objs.image_desc =
        (cl_image_desc *)acl_malloc(sizeof(cl_image_desc));
    break;
  case CL_MEM_OBJECT_IMAGE2D:
    // If we change this, need to actually allocate/assign the buffer data here
    if (image_desc->mem_object != NULL &&
        image_desc->mem_object->mem_object_type == CL_MEM_OBJECT_BUFFER) {
      BAIL_INFO(CL_IMAGE_FORMAT_NOT_SUPPORTED, context,
                "Do not support images created from buffers");
      // Copy information from the other image object
    } else if (image_desc->mem_object != NULL &&
               image_desc->mem_object->mem_object_type ==
                   CL_MEM_OBJECT_BUFFER) {
      BAIL_INFO(CL_IMAGE_FORMAT_NOT_SUPPORTED, context,
                "Do not support images created from other images");
      // Allocate a new image object
    } else {
      image_size =
          element_size * image_desc->image_width * image_desc->image_height +
          get_offset_for_image_param(context, image_desc->image_type, "data");
      if (image_desc->image_width <= 0) {
        BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                  "image width cannot be zero for a 2D object");
      }
      if (image_desc->image_width > max_2d_image_width) {
        BAIL_INFO(
            CL_INVALID_IMAGE_SIZE, context,
            "image width exceeds maximum width for all devices in context");
      }
      if (image_desc->image_height <= 0) {
        BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                  "image height cannot be zero for a 2D object");
      }
      if (image_desc->image_height > max_2d_image_height) {
        BAIL_INFO(
            CL_INVALID_IMAGE_SIZE, context,
            "1 image height exceeds maximum height for all devices in context");
      }
      if (image_size > context->max_mem_alloc_size) {
        BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                  "image size exceeds maximum alloc size");
      }
      return_buffer =
          clCreateBuffer(context, flags, image_size, host_ptr, errcode_ret);
      if (return_buffer == NULL)
        return NULL;
      return_buffer->fields.image_objs.image_format =
          (cl_image_format *)acl_malloc(sizeof(cl_image_format));
      return_buffer->fields.image_objs.image_desc =
          (cl_image_desc *)acl_malloc(sizeof(cl_image_desc));
    }
    break;
  case CL_MEM_OBJECT_IMAGE2D_ARRAY:
    image_size =
        element_size * image_desc->image_width * image_desc->image_height *
            image_desc->image_array_size +
        get_offset_for_image_param(context, image_desc->image_type, "data");
    if (image_desc->image_width <= 0)
      BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                "image width cannot be zero for a 2D object");
    if (image_desc->image_width > max_2d_image_width)
      BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                "image width exceeds maximum width for all devices in context");
    if (image_desc->image_height <= 0)
      BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                "image height cannot be zero for a 2D object");
    if (image_desc->image_height > max_2d_image_height)
      BAIL_INFO(
          CL_INVALID_IMAGE_SIZE, context,
          "2 image height exceeds maximum height for all devices in context");
    if (image_size > context->max_mem_alloc_size)
      BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                "image size exceeds maximum alloc size");
    return_buffer =
        clCreateBuffer(context, flags, image_size, host_ptr, errcode_ret);
    if (return_buffer == NULL)
      return NULL;
    return_buffer->fields.image_objs.image_format =
        (cl_image_format *)acl_malloc(sizeof(cl_image_format));
    return_buffer->fields.image_objs.image_desc =
        (cl_image_desc *)acl_malloc(sizeof(cl_image_desc));
    break;
  case CL_MEM_OBJECT_IMAGE3D:
    image_size =
        element_size * image_desc->image_width * image_desc->image_height *
            image_desc->image_depth +
        get_offset_for_image_param(context, image_desc->image_type, "data");
    if (image_desc->image_width <= 0)
      BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                "image width cannot be zero for a 3D object");
    if (image_desc->image_width > max_3d_image_width)
      BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                "image width exceeds maximum width for all devices in context");
    if (image_desc->image_height <= 0)
      BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                "image height cannot be zero for a 3D object");
    if (image_desc->image_height > max_3d_image_height)
      BAIL_INFO(
          CL_INVALID_IMAGE_SIZE, context,
          "image height exceeds maximum height for all devices in context");
    if (image_desc->image_depth <= 0)
      BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                "image depth cannot be zero for a 3D object");
    if (image_desc->image_depth > max_3d_image_depth)
      BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                "image depth exceeds maximum depth for all devices in context");
    if (image_size > context->max_mem_alloc_size)
      BAIL_INFO(CL_INVALID_IMAGE_SIZE, context,
                "image size exceeds maximum alloc size");
    return_buffer =
        clCreateBuffer(context, flags, image_size, host_ptr, errcode_ret);
    if (return_buffer == NULL)
      return NULL;
    return_buffer->fields.image_objs.image_format =
        (cl_image_format *)acl_malloc(sizeof(cl_image_format));
    return_buffer->fields.image_objs.image_desc =
        (cl_image_desc *)acl_malloc(sizeof(cl_image_desc));
    break;
  default:
    BAIL_INFO(CL_INVALID_IMAGE_DESCRIPTOR, context, "invalid image type");
    break;
  }

  // Set up image specific fields in cl_mem object
  return_buffer->mem_object_type = image_desc->image_type;
  return_buffer->fields.image_objs.host_ptr = host_ptr;
  safe_memcpy(return_buffer->fields.image_objs.image_format, image_format,
              sizeof(cl_image_format), sizeof(cl_image_format),
              sizeof(cl_image_format));
  safe_memcpy(return_buffer->fields.image_objs.image_desc, image_desc,
              sizeof(cl_image_desc), sizeof(cl_image_desc),
              sizeof(cl_image_desc));

  // Images always need a backing store for map/unmap
  if (!return_buffer->host_mem.aligned_ptr) {
    return_buffer->host_mem = acl_mem_aligned_malloc(image_size);
    if (return_buffer->host_mem.raw == 0) {
      BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
                "Could not allocate backing store for a device image");
    }
  }

  // If the memory was allocated, copy over the image meta data (width, height,
  // etc)
  if (!return_buffer->allocation_deferred &&
      !(return_buffer->block_allocation->region ==
        &(acl_platform.host_user_mem))) {
    copy_image_metadata(return_buffer);
  }
  return return_buffer;
}

ACL_EXPORT CL_API_ENTRY cl_mem CL_API_CALL clCreateImage(
    cl_context context, cl_mem_flags flags, const cl_image_format *image_format,
    const cl_image_desc *image_desc, void *host_ptr, cl_int *errcode_ret) {
  return clCreateImageIntelFPGA(context, flags, image_format, image_desc,
                                host_ptr, errcode_ret);
}
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100)
#endif
// Image non-support
ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL clCreateImage2DIntelFPGA(
    cl_context context, cl_mem_flags flags, const cl_image_format *image_format,
    size_t image_width, size_t image_height, size_t image_row_pitch,
    void *host_ptr, cl_int *errcode_ret) {
  cl_image_desc image_desc;
  image_desc.image_type = CL_MEM_OBJECT_IMAGE2D;
  image_desc.image_width = image_width;
  image_desc.image_height = image_height;
  image_desc.image_depth = 1;
  image_desc.image_array_size = 1;
  image_desc.image_row_pitch = image_row_pitch;
  image_desc.image_slice_pitch = 1;
  image_desc.num_mip_levels = 1;
  image_desc.num_samples = 1;
  image_desc.buffer = NULL;

  return clCreateImageIntelFPGA(context, flags, image_format, &image_desc,
                                host_ptr, errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL clCreateImage2D(
    cl_context context, cl_mem_flags flags, const cl_image_format *image_format,
    size_t image_width, size_t image_height, size_t image_row_pitch,
    void *host_ptr, cl_int *errcode_ret) {
  return clCreateImage2DIntelFPGA(context, flags, image_format, image_width,
                                  image_height, image_row_pitch, host_ptr,
                                  errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL clCreateImage3DIntelFPGA(
    cl_context context, cl_mem_flags flags, const cl_image_format *image_format,
    size_t image_width, size_t image_height, size_t image_depth,
    size_t image_row_pitch, size_t image_slice_pitch, void *host_ptr,
    cl_int *errcode_ret) {
  cl_image_desc image_desc;
  image_desc.image_type = CL_MEM_OBJECT_IMAGE3D;
  image_desc.image_width = image_width;
  image_desc.image_height = image_height;
  image_desc.image_depth = image_depth;
  image_desc.image_array_size = 1;
  image_desc.image_row_pitch = image_row_pitch;
  image_desc.image_slice_pitch = image_slice_pitch;
  image_desc.num_mip_levels = 1;
  image_desc.num_samples = 1;
  image_desc.buffer = NULL;

  return clCreateImageIntelFPGA(context, flags, image_format, &image_desc,
                                host_ptr, errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL
clCreateImage3D(cl_context context, cl_mem_flags flags,
                const cl_image_format *image_format, size_t image_width,
                size_t image_height, size_t image_depth, size_t image_row_pitch,
                size_t image_slice_pitch, void *host_ptr, cl_int *errcode_ret) {
  return clCreateImage3DIntelFPGA(context, flags, image_format, image_width,
                                  image_height, image_depth, image_row_pitch,
                                  image_slice_pitch, host_ptr, errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetSupportedImageFormatsIntelFPGA(
    cl_context context, cl_mem_flags flags, cl_mem_object_type image_type,
    cl_uint num_entries, cl_image_format *image_formats,
    cl_uint *num_image_formats) {
#define NUM_SUPPORTED_IMAGE_FORMATS 34
  cl_image_format supported_image_formats[NUM_SUPPORTED_IMAGE_FORMATS] = {
      {CL_R, CL_UNORM_INT8},
      {CL_R, CL_UNORM_INT16},
      {CL_R, CL_SNORM_INT8},
      {CL_R, CL_SNORM_INT16},
      {CL_R, CL_SIGNED_INT8},
      {CL_R, CL_SIGNED_INT16},
      {CL_R, CL_SIGNED_INT32},
      {CL_R, CL_UNSIGNED_INT8},
      {CL_R, CL_UNSIGNED_INT16},
      {CL_R, CL_UNSIGNED_INT32},
      {CL_R, CL_FLOAT},
      {CL_RG, CL_UNORM_INT8},
      {CL_RG, CL_UNORM_INT16},
      {CL_RG, CL_SNORM_INT8},
      {CL_RG, CL_SNORM_INT16},
      {CL_RG, CL_SIGNED_INT8},
      {CL_RG, CL_SIGNED_INT16},
      {CL_RG, CL_SIGNED_INT32},
      {CL_RG, CL_UNSIGNED_INT8},
      {CL_RG, CL_UNSIGNED_INT16},
      {CL_RG, CL_UNSIGNED_INT32},
      {CL_RG, CL_FLOAT},
      {CL_RGBA, CL_UNORM_INT8},
      {CL_RGBA, CL_UNORM_INT16},
      {CL_RGBA, CL_SNORM_INT8},
      {CL_RGBA, CL_SNORM_INT16},
      {CL_RGBA, CL_SIGNED_INT8},
      {CL_RGBA, CL_SIGNED_INT16},
      {CL_RGBA, CL_SIGNED_INT32},
      {CL_RGBA, CL_UNSIGNED_INT8},
      {CL_RGBA, CL_UNSIGNED_INT16},
      {CL_RGBA, CL_UNSIGNED_INT32},
      {CL_RGBA, CL_FLOAT},
      {CL_BGRA, CL_UNORM_INT8},
  };

  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_context_is_valid(context)) {
    return CL_INVALID_CONTEXT;
  }
  if (num_entries == 0 && image_formats) {
    ERR_RET(CL_INVALID_VALUE, context,
            "num_entries is zero but image formats array is specified");
  }
  if (num_entries > 0 && image_formats == 0) {
    ERR_RET(CL_INVALID_VALUE, context,
            "num_entries is non-zero but image_formats array is NULL");
  }
  switch (image_type) {
  case CL_MEM_OBJECT_IMAGE2D:
  case CL_MEM_OBJECT_IMAGE3D:
  case CL_MEM_OBJECT_IMAGE2D_ARRAY:
  case CL_MEM_OBJECT_IMAGE1D:
  case CL_MEM_OBJECT_IMAGE1D_ARRAY:
  case CL_MEM_OBJECT_IMAGE1D_BUFFER:
    break;
  default:
    ERR_RET(CL_INVALID_VALUE, context, "Invalid or unsupported image type");
  }
  if (flags &
      ~(CL_MEM_READ_WRITE | CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY |
        CL_MEM_USE_HOST_PTR | CL_MEM_ALLOC_HOST_PTR | CL_MEM_COPY_HOST_PTR)) {
    ERR_RET(CL_INVALID_VALUE, context, "Invalid flags");
  }

  if (num_image_formats) {
    *num_image_formats = NUM_SUPPORTED_IMAGE_FORMATS;
  }

  if (num_entries >= NUM_SUPPORTED_IMAGE_FORMATS && image_formats) {
    int i;
    for (i = 0; i < NUM_SUPPORTED_IMAGE_FORMATS; ++i) {
      image_formats[i].image_channel_order =
          supported_image_formats[i].image_channel_order;
      image_formats[i].image_channel_data_type =
          supported_image_formats[i].image_channel_data_type;
    }
  }
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetSupportedImageFormats(
    cl_context context, cl_mem_flags flags, cl_mem_object_type image_type,
    cl_uint num_entries, cl_image_format *image_formats,
    cl_uint *num_image_formats) {
  return clGetSupportedImageFormatsIntelFPGA(context, flags, image_type,
                                             num_entries, image_formats,
                                             num_image_formats);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetImageInfoIntelFPGA(
    cl_mem image, cl_mem_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret) {
  acl_result_t result;
  cl_context context;
  std::scoped_lock lock{acl_mutex_wrapper};

  RESULT_INIT;

  if (!acl_mem_is_valid(image)) {
    return CL_INVALID_MEM_OBJECT;
  }

  context = image->context;

  if (!is_image(image)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, context, "Memory object is not an image");
  }

  switch (param_name) {
  case CL_IMAGE_FORMAT:
    RESULT_IMAGE_FORMAT(*(image->fields.image_objs.image_format));
    break;
  case CL_IMAGE_ELEMENT_SIZE:
    RESULT_SIZE_T(acl_get_image_element_size(
        image->context, image->fields.image_objs.image_format, NULL));
    break;
  case CL_IMAGE_ROW_PITCH:
    RESULT_SIZE_T(image->fields.image_objs.image_desc->image_row_pitch);
    break;
  case CL_IMAGE_SLICE_PITCH:
    RESULT_SIZE_T(image->fields.image_objs.image_desc->image_slice_pitch);
    break;
  case CL_IMAGE_WIDTH:
    RESULT_SIZE_T(image->fields.image_objs.image_desc->image_width);
    break;
  case CL_IMAGE_HEIGHT:
    RESULT_SIZE_T(image->fields.image_objs.image_desc->image_height);
    break;
  case CL_IMAGE_DEPTH:
    RESULT_SIZE_T(image->fields.image_objs.image_desc->image_depth);
    break;
  default:
    break;
  }

  if (result.size == 0) {
    ERR_RET(CL_INVALID_VALUE, context,
            "Invalid or unsupported memory object query");
  }

  if (param_value) {
    if (param_value_size < result.size) {
      ERR_RET(CL_INVALID_VALUE, context,
              "Parameter return buffer is too small");
    }
    RESULT_COPY(param_value, param_value_size);
  }

  if (param_value_size_ret) {
    *param_value_size_ret = result.size;
  }
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clGetImageInfo(cl_mem image,
                                               cl_mem_info param_name,
                                               size_t param_value_size,
                                               void *param_value,
                                               size_t *param_value_size_ret) {
  return clGetImageInfoIntelFPGA(image, param_name, param_value_size,
                                 param_value, param_value_size_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadImageIntelFPGA(
    cl_command_queue command_queue, cl_mem image, cl_bool blocking_read,
    const size_t *origin, const size_t *region, size_t row_pitch,
    size_t slice_pitch, void *ptr, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  size_t tmp_src_offset[3];
  size_t tmp_dst_offset[3];
  size_t tmp_cb[3];
  size_t tmp_row_pitch, tmp_slice_pitch;
  cl_int errcode_ret;
  size_t src_element_size;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  if (ptr == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer argument cannot be NULL");
  }

  if (image == NULL) {
    return CL_INVALID_MEM_OBJECT;
  }

  src_element_size = acl_get_image_element_size(
      image->context, image->fields.image_objs.image_format, &errcode_ret);
  if (errcode_ret != CL_SUCCESS) {
    return errcode_ret;
  }

  tmp_src_offset[0] = origin[0];
  tmp_src_offset[1] = origin[1];
  tmp_src_offset[2] = origin[2];
  tmp_dst_offset[0] = (size_t)((char *)ptr - (char *)ACL_MEM_ALIGN);
  tmp_dst_offset[1] = 0;
  tmp_dst_offset[2] = 0;
  // For images, have to multiply the first region by the size of each element
  // because each element can be more than one byte wide.
  tmp_cb[0] = region[0] * src_element_size;
  tmp_cb[1] = region[1];
  tmp_cb[2] = region[2];

  if (row_pitch != 0) {
    if (row_pitch <
        image->fields.image_objs.image_desc->image_width * src_element_size) {
      ERR_RET(CL_INVALID_VALUE, command_queue->context,
              "Invalid row pitch provided");
    }
    tmp_row_pitch = row_pitch;
  } else {
    tmp_row_pitch =
        image->fields.image_objs.image_desc->image_width * src_element_size;
  }

  if (image->mem_object_type == CL_MEM_OBJECT_IMAGE1D ||
      image->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY ||
      image->mem_object_type == CL_MEM_OBJECT_IMAGE1D_BUFFER) {
    tmp_slice_pitch = tmp_row_pitch;
  } else {
    tmp_slice_pitch =
        image->fields.image_objs.image_desc->image_height * tmp_row_pitch;
  }
  // Allow the user to override the default slice pitch
  if (slice_pitch != 0) {
    if (slice_pitch < tmp_slice_pitch) {
      ERR_RET(CL_INVALID_VALUE, command_queue->context,
              "Invalid row pitch provided");
    }
    tmp_slice_pitch = slice_pitch;
  }

  if (!is_image(image)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Memory object is not an image");
  }

  if (!acl_bind_buffer_to_device(command_queue->device, image)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }

  {
    cl_int ret = l_enqueue_mem_transfer(
        command_queue, blocking_read, image, tmp_src_offset, 0, 0,
        command_queue->context->unwrapped_host_mem,
        tmp_dst_offset, // see creation of the unwrapped_host_mem
        tmp_row_pitch, tmp_slice_pitch, tmp_cb, num_events_in_wait_list,
        event_wait_list, event, CL_COMMAND_READ_BUFFER, 0);
    return ret;
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadImage(
    cl_command_queue command_queue, cl_mem image, cl_bool blocking_read,
    const size_t *origin, const size_t *region, size_t row_pitch,
    size_t slice_pitch, void *ptr, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  return clEnqueueReadImageIntelFPGA(
      command_queue, image, blocking_read, origin, region, row_pitch,
      slice_pitch, ptr, num_events_in_wait_list, event_wait_list, event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteImageIntelFPGA(
    cl_command_queue command_queue, cl_mem image, cl_bool blocking_write,
    const size_t *origin, const size_t *region, size_t input_row_pitch,
    size_t input_slice_pitch, const void *ptr, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  size_t tmp_src_offset[3];
  size_t tmp_dst_offset[3];
  size_t tmp_cb[3];
  size_t tmp_row_pitch, tmp_slice_pitch;
  cl_int errcode_ret;
  size_t dst_element_size;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (image == NULL) {
    return CL_INVALID_MEM_OBJECT;
  }

  dst_element_size = acl_get_image_element_size(
      image->context, image->fields.image_objs.image_format, &errcode_ret);
  if (errcode_ret != CL_SUCCESS) {
    return errcode_ret;
  }

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  tmp_src_offset[0] = (size_t)((char *)ptr - (const char *)ACL_MEM_ALIGN);
  tmp_src_offset[1] = 0;
  tmp_src_offset[2] = 0;
  tmp_dst_offset[0] = origin[0];
  tmp_dst_offset[1] = origin[1];
  tmp_dst_offset[2] = origin[2];
  // For images, have to multiply the first region by the size of each element
  // because each element can be more than one byte wide.
  tmp_cb[0] = region[0] * dst_element_size;
  tmp_cb[1] = region[1];
  tmp_cb[2] = region[2];

  if (input_row_pitch != 0) {
    if (input_row_pitch <
        image->fields.image_objs.image_desc->image_width * dst_element_size) {
      ERR_RET(CL_INVALID_VALUE, command_queue->context,
              "Invalid row pitch provided");
    }
    tmp_row_pitch = input_row_pitch;
  } else {
    tmp_row_pitch =
        image->fields.image_objs.image_desc->image_width * dst_element_size;
  }

  if (image->mem_object_type == CL_MEM_OBJECT_IMAGE1D ||
      image->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY ||
      image->mem_object_type == CL_MEM_OBJECT_IMAGE1D_BUFFER) {
    tmp_slice_pitch = tmp_row_pitch;
  } else {
    tmp_slice_pitch =
        image->fields.image_objs.image_desc->image_height * tmp_row_pitch;
  }
  // Allow the user to override the default slice pitch
  if (input_slice_pitch != 0) {
    if (input_slice_pitch < tmp_slice_pitch) {
      ERR_RET(CL_INVALID_VALUE, command_queue->context,
              "Invalid row pitch provided");
    }
    tmp_slice_pitch = input_slice_pitch;
  }

  if (!is_image(image)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Memory object is not an image");
  }

  if (!acl_bind_buffer_to_device(command_queue->device, image)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }

  {
    cl_int ret = l_enqueue_mem_transfer(
        command_queue, blocking_write,
        command_queue->context->unwrapped_host_mem, tmp_src_offset,
        tmp_row_pitch, tmp_slice_pitch, image, tmp_dst_offset, 0, 0, tmp_cb,
        num_events_in_wait_list, event_wait_list, event,
        CL_COMMAND_WRITE_BUFFER, 0);
    return ret;
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteImage(
    cl_command_queue command_queue, cl_mem image, cl_bool blocking_write,
    const size_t *origin, const size_t *region, size_t input_row_pitch,
    size_t input_slice_pitch, const void *ptr, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  return clEnqueueWriteImageIntelFPGA(
      command_queue, image, blocking_write, origin, region, input_row_pitch,
      input_slice_pitch, ptr, num_events_in_wait_list, event_wait_list, event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueFillImageIntelFPGA(
    cl_command_queue command_queue, cl_mem image, const void *fill_color,
    const size_t *origin, const size_t *region, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  size_t tmp_src_offset[3];
  size_t tmp_dst_offset[3];
  size_t tmp_cb[3];
  size_t tmp_row_pitch, tmp_slice_pitch;
  cl_int errcode_ret;
  size_t dst_element_size;
  char *ptr = NULL;
  char converted_fill_color[16]; // Maximum number of bytes needed to keep a
                                 // pixel.
  cl_event tmp_event;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (image == NULL) {
    return CL_INVALID_MEM_OBJECT;
  }

  dst_element_size = acl_get_image_element_size(
      image->context, image->fields.image_objs.image_format, &errcode_ret);
  if (errcode_ret != CL_SUCCESS) {
    return errcode_ret;
  }

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  if (!is_image(image)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Memory object is not an image");
  }

  // Replicating the color in the region allocated in host mem.
  cl_image_format color_format;
  color_format.image_channel_order = CL_RGBA;

  if (fill_color == NULL)
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "fill_color cannot be NULL");

  size_t host_mem_size = region[0] * region[1] * region[2] * dst_element_size;

  // Converting the given color to proper format.
  // Note: The fill color is a single floating point value if the channel order
  // is CL_DEPTH. This case is not implemented since we don't support CL_DEPTH
  // yet
  switch (image->fields.image_objs.image_format->image_channel_data_type) {
  case CL_SNORM_INT8:
  case CL_SNORM_INT16:
  case CL_UNORM_INT8:
  case CL_UNORM_INT16:
  case CL_UNORM_SHORT_565:
  case CL_UNORM_SHORT_555:
  case CL_UNORM_INT_101010:
  case CL_HALF_FLOAT:
  case CL_FLOAT:
    color_format.image_channel_data_type = CL_FLOAT;
    errcode_ret =
        acl_convert_image_format(fill_color, converted_fill_color, color_format,
                                 *(image->fields.image_objs.image_format));
    break;
  case CL_SIGNED_INT8:
  case CL_SIGNED_INT16:
  case CL_SIGNED_INT32:
    // Specs are vague about this, the correct image_channel_data_type of
    // fill_color is either always int32, or int 8, 16 or 32 based on the image
    // channel data type. Going for always int32.
    color_format.image_channel_data_type = CL_SIGNED_INT32;
    errcode_ret =
        acl_convert_image_format(fill_color, converted_fill_color, color_format,
                                 *(image->fields.image_objs.image_format));
    break;
  case CL_UNSIGNED_INT8:
  case CL_UNSIGNED_INT16:
  case CL_UNSIGNED_INT32:
    // Specs are vague about this, the correct image_channel_data_type of
    // fill_color is either always int32, or int 8, 16 or 32 based on the image
    // channel data type. Going for always int32.
    color_format.image_channel_data_type = CL_UNSIGNED_INT32;
    errcode_ret =
        acl_convert_image_format(fill_color, converted_fill_color, color_format,
                                 *(image->fields.image_objs.image_format));
    break;
  default:
    errcode_ret = -1;
  }
  if (errcode_ret != CL_SUCCESS)
    ERR_RET(CL_IMAGE_FORMAT_NOT_SUPPORTED, command_queue->context,
            "Failed to convert fill_color to the appropriate image "
            "channel format and order");

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

  *aligned_ptr = acl_mem_aligned_malloc(host_mem_size);
  ptr = (char *)(aligned_ptr->aligned_ptr);
  if (!ptr) {
    acl_free(aligned_ptr);
    acl_free(callback_data);
    ERR_RET(CL_OUT_OF_HOST_MEMORY, command_queue->context,
            "Out of host memory");
  }

  for (cl_uint i = 0; i < region[0] * region[1] * region[2]; i++) {
    safe_memcpy(ptr + i * dst_element_size, converted_fill_color,
                dst_element_size, dst_element_size, 16);
  }

  tmp_src_offset[0] = (size_t)ptr - ACL_MEM_ALIGN;
  tmp_src_offset[1] = 0;
  tmp_src_offset[2] = 0;
  tmp_dst_offset[0] = origin[0];
  tmp_dst_offset[1] = origin[1];
  tmp_dst_offset[2] = origin[2];
  // For images, have to multiply the first region by the size of each element
  // because each element can be more than one byte wide.
  tmp_cb[0] = region[0] * dst_element_size;
  tmp_cb[1] = region[1];
  tmp_cb[2] = region[2];

  tmp_row_pitch =
      region[0] *
      dst_element_size; // Width of each row of the memory that ptr points to.

  if (image->mem_object_type == CL_MEM_OBJECT_IMAGE1D ||
      image->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY ||
      image->mem_object_type == CL_MEM_OBJECT_IMAGE1D_BUFFER) {
    tmp_slice_pitch = tmp_row_pitch;
  } else {
    tmp_slice_pitch = region[1] * tmp_row_pitch;
  }

  if (!acl_bind_buffer_to_device(command_queue->device, image)) {
    if (ptr) {
      acl_mem_aligned_free(command_queue->context,
                           aligned_ptr); // Cleaning up before failing.
      acl_free(aligned_ptr);
      acl_free(callback_data);
    }
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }

  {
    cl_int ret = l_enqueue_mem_transfer(
        command_queue, 0, command_queue->context->unwrapped_host_mem,
        tmp_src_offset, tmp_row_pitch, tmp_slice_pitch, image, tmp_dst_offset,
        0, 0, tmp_cb, num_events_in_wait_list, event_wait_list, &tmp_event,
        CL_COMMAND_WRITE_BUFFER, 0);

    if (ret != CL_SUCCESS) {
      acl_mem_aligned_free(command_queue->context,
                           aligned_ptr); // Cleaning up before failing.
      acl_free(aligned_ptr);
      acl_free(callback_data);
      return ret;
    }

    callback_data[0] = (void *)(aligned_ptr);
    if (event) {
      *event = tmp_event;
      callback_data[1] = NULL; // User needs the event, so we shouldn't release
                               // it after the event completion.
    } else {
      callback_data[1] =
          tmp_event; // Passing the event to release it when the event is done.
    }
    clSetEventCallback(tmp_event, CL_COMPLETE,
                       acl_free_allocation_after_event_completion,
                       (void *)callback_data);
    return ret;
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueFillImage(
    cl_command_queue command_queue, cl_mem image, const void *fill_color,
    const size_t *origin, const size_t *region, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  return clEnqueueFillImageIntelFPGA(command_queue, image, fill_color, origin,
                                     region, num_events_in_wait_list,
                                     event_wait_list, event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyImageIntelFPGA(
    cl_command_queue command_queue, cl_mem src_image, cl_mem dst_image,
    const size_t *src_origin, const size_t *dst_origin, const size_t *region,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  size_t tmp_src_offset[3];
  size_t tmp_dst_offset[3];
  size_t tmp_cb[3];
  size_t element_size;
  cl_int errcode_ret;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  if (src_image == NULL || !is_image(src_image)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Source memory object is not an image");
  }
  if (dst_image == NULL || !is_image(dst_image)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Source memory object is not an image");
  }

  if ((src_image->fields.image_objs.image_format->image_channel_order !=
       dst_image->fields.image_objs.image_format->image_channel_order) ||
      (src_image->fields.image_objs.image_format->image_channel_data_type !=
       dst_image->fields.image_objs.image_format->image_channel_data_type)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Source memory object is not an image");
  }

  // Doesn't matter if we look at src or dst, already verified that they are the
  // same above.
  element_size = acl_get_image_element_size(
      command_queue->context, src_image->fields.image_objs.image_format,
      &errcode_ret);
  if (errcode_ret != CL_SUCCESS) {
    ERR_RET(errcode_ret, command_queue->context,
            "Source memory object is not an image");
  }

  tmp_src_offset[0] = src_origin[0];
  tmp_src_offset[1] = src_origin[1];
  tmp_src_offset[2] = src_origin[2];
  tmp_dst_offset[0] = dst_origin[0];
  tmp_dst_offset[1] = dst_origin[1];
  tmp_dst_offset[2] = dst_origin[2];
  // For images, have to multiply the first region by the size of each element
  // because each element can be more than one byte wide.
  tmp_cb[0] = region[0] * element_size;
  tmp_cb[1] = region[1];
  tmp_cb[2] = region[2];

  if ((src_image->fields.image_objs.image_format->image_channel_order !=
       dst_image->fields.image_objs.image_format->image_channel_order) ||
      (src_image->fields.image_objs.image_format->image_channel_data_type !=
       dst_image->fields.image_objs.image_format->image_channel_data_type)) {
    ERR_RET(CL_IMAGE_FORMAT_MISMATCH, command_queue->context,
            "Mismatch in image format between source & destination image");
  }
  if (!acl_bind_buffer_to_device(command_queue->device, src_image)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }
  if (!acl_bind_buffer_to_device(command_queue->device, dst_image)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }

  {
    cl_int ret = l_enqueue_mem_transfer(
        command_queue, 0, src_image, tmp_src_offset, 0, 0, dst_image,
        tmp_dst_offset, 0, 0, tmp_cb, num_events_in_wait_list, event_wait_list,
        event, CL_COMMAND_COPY_BUFFER, 0);
    return ret;
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyImage(
    cl_command_queue command_queue, cl_mem src_image, cl_mem dst_image,
    const size_t *src_origin, const size_t *dst_origin, const size_t *region,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  return clEnqueueCopyImageIntelFPGA(
      command_queue, src_image, dst_image, src_origin, dst_origin, region,
      num_events_in_wait_list, event_wait_list, event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyImageToBufferIntelFPGA(
    cl_command_queue command_queue, cl_mem src_image, cl_mem dst_buffer,
    const size_t *src_origin, const size_t *region, size_t dst_offset,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  size_t tmp_src_offset[3];
  size_t tmp_dst_offset[3];
  size_t tmp_cb[3];
  size_t tmp_row_pitch, tmp_slice_pitch;
  cl_int errcode_ret;
  size_t src_element_size;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (!acl_mem_is_valid(src_image)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Source image is invalid");
  }
  if (!acl_mem_is_valid(dst_buffer)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Destination buffer is invalid");
  }

  if (src_image != NULL) {
    src_element_size = acl_get_image_element_size(
        src_image->context, src_image->fields.image_objs.image_format,
        &errcode_ret);
    if (errcode_ret != CL_SUCCESS) {
      return errcode_ret;
    }
  } else {
    src_element_size = 0;
  }

  tmp_src_offset[0] = src_origin[0];
  tmp_src_offset[1] = src_origin[1];
  tmp_src_offset[2] = src_origin[2];
  tmp_dst_offset[0] = dst_offset;
  tmp_dst_offset[1] = 0;
  tmp_dst_offset[2] = 0;
  // For images, have to multiply the first region by the size of each element
  // because each element can be more than one byte wide.
  tmp_cb[0] = region[0] * src_element_size;
  tmp_cb[1] = region[1];
  tmp_cb[2] = region[2];

  tmp_row_pitch =
      src_image->fields.image_objs.image_desc->image_width * src_element_size;
  if (src_image->mem_object_type == CL_MEM_OBJECT_IMAGE1D ||
      src_image->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY ||
      src_image->mem_object_type == CL_MEM_OBJECT_IMAGE1D_BUFFER) {
    tmp_slice_pitch = tmp_row_pitch;
  } else {
    tmp_slice_pitch =
        src_image->fields.image_objs.image_desc->image_height * tmp_row_pitch;
  }

  if (!is_image(src_image)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Memory object is not an image");
  }

  if (!acl_bind_buffer_to_device(command_queue->device, src_image)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }
  if (!acl_bind_buffer_to_device(command_queue->device, dst_buffer)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }

  {
    cl_int ret = l_enqueue_mem_transfer(
        command_queue, CL_FALSE, src_image, tmp_src_offset, 0, 0, dst_buffer,
        tmp_dst_offset, // see creation of the unwrapped_host_mem
        tmp_row_pitch, tmp_slice_pitch, tmp_cb, num_events_in_wait_list,
        event_wait_list, event, CL_COMMAND_READ_BUFFER, 0);
    return ret;
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyImageToBuffer(
    cl_command_queue command_queue, cl_mem src_image, cl_mem dst_buffer,
    const size_t *src_origin, const size_t *region, size_t dst_offset,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  return clEnqueueCopyImageToBufferIntelFPGA(
      command_queue, src_image, dst_buffer, src_origin, region, dst_offset,
      num_events_in_wait_list, event_wait_list, event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyBufferToImageIntelFPGA(
    cl_command_queue command_queue, cl_mem src_buffer, cl_mem dst_image,
    size_t src_offset, const size_t *dst_origin, const size_t *region,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  size_t tmp_src_offset[3];
  size_t tmp_dst_offset[3];
  size_t tmp_cb[3];
  size_t tmp_row_pitch, tmp_slice_pitch;
  cl_int errcode_ret;
  size_t dst_element_size;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (dst_image != NULL) {
    dst_element_size = acl_get_image_element_size(
        dst_image->context, dst_image->fields.image_objs.image_format,
        &errcode_ret);
    if (errcode_ret != CL_SUCCESS) {
      return errcode_ret;
    }
  } else {
    dst_element_size = 0;
  }

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (!acl_mem_is_valid(src_buffer)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Source buffer is invalid");
  }
  if (!acl_mem_is_valid(dst_image)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Destination buffer is invalid");
  }

  tmp_src_offset[0] = src_offset;
  tmp_src_offset[1] = 0;
  tmp_src_offset[2] = 0;
  tmp_dst_offset[0] = dst_origin[0];
  tmp_dst_offset[1] = dst_origin[1];
  tmp_dst_offset[2] = dst_origin[2];
  // For images, have to multiply the first region by the size of each element
  // because each element can be more than one byte wide.
  tmp_cb[0] = region[0] * dst_element_size;
  tmp_cb[1] = region[1];
  tmp_cb[2] = region[2];

  tmp_row_pitch =
      dst_image->fields.image_objs.image_desc->image_width * dst_element_size;
  if (dst_image->mem_object_type == CL_MEM_OBJECT_IMAGE1D ||
      dst_image->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY ||
      dst_image->mem_object_type == CL_MEM_OBJECT_IMAGE1D_BUFFER) {
    tmp_slice_pitch = tmp_row_pitch;
  } else {
    tmp_slice_pitch =
        dst_image->fields.image_objs.image_desc->image_height * tmp_row_pitch;
  }

  if (!is_image(dst_image)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Memory object is not an image");
  }

  if (!acl_bind_buffer_to_device(command_queue->device, src_buffer)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }
  if (!acl_bind_buffer_to_device(command_queue->device, dst_image)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }

  {
    cl_int ret = l_enqueue_mem_transfer(
        command_queue, CL_FALSE, src_buffer, tmp_src_offset, tmp_row_pitch,
        tmp_slice_pitch, dst_image, tmp_dst_offset, 0, 0, tmp_cb,
        num_events_in_wait_list, event_wait_list, event,
        CL_COMMAND_WRITE_BUFFER, 0);
    return ret;
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyBufferToImage(
    cl_command_queue command_queue, cl_mem src_buffer, cl_mem dst_image,
    size_t src_offset, const size_t *dst_origin, const size_t *region,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  return clEnqueueCopyBufferToImageIntelFPGA(
      command_queue, src_buffer, dst_image, src_offset, dst_origin, region,
      num_events_in_wait_list, event_wait_list, event);
}

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL clEnqueueMapImageIntelFPGA(
    cl_command_queue command_queue, cl_mem image, cl_bool blocking_map,
    cl_map_flags map_flags, const size_t *origin, const size_t *region,
    size_t *image_row_pitch, size_t *image_slice_pitch,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event, cl_int *errcode_ret) {
  void *result;
  cl_event local_event = 0; // used for blocking
  cl_context context;
  cl_int status;
  size_t element_size;
  size_t tmp_row_pitch;
  size_t tmp_slice_pitch = 0;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (image != NULL) {
    element_size = acl_get_image_element_size(
        image->context, image->fields.image_objs.image_format, errcode_ret);
    if (*errcode_ret != CL_SUCCESS) {
      return NULL;
    }
  } else {
    element_size = 0;
  }
  if (!acl_command_queue_is_valid(command_queue)) {
    BAIL(CL_INVALID_COMMAND_QUEUE);
  }
  context = command_queue->context;
  if (!acl_mem_is_valid(image)) {
    BAIL_INFO(CL_INVALID_MEM_OBJECT, context, "Invalid memory object");
  }

  if (command_queue->context != image->context) {
    BAIL_INFO(
        CL_INVALID_CONTEXT, context,
        "Command queue and memory object are not associated with the same "
        "context");
  }

  if (!acl_bind_buffer_to_device(command_queue->device, image)) {
    BAIL_INFO(CL_MEM_OBJECT_ALLOCATION_FAILURE, context,
              "Deferred Allocation Failed");
  }

  // Check if we can physically map the data into place.
  // We can map it either if it already resides on the host, or we
  // have backing store for it.
  if (!image->block_allocation->region->is_host_accessible &&
      !image->host_mem.aligned_ptr) {
    BAIL_INFO(CL_MAP_FAILURE, context,
              "Could not map the image into host memory");
  }

  if (!is_image(image)) {
    BAIL_INFO(CL_INVALID_MEM_OBJECT, command_queue->context,
              "Memory object is not an image");
  }

  if (image_row_pitch == NULL) {
    BAIL_INFO(CL_INVALID_VALUE, command_queue->context,
              "Invalid row pitch provided");
  } else {
    tmp_row_pitch =
        image->fields.image_objs.image_desc->image_width * element_size;
    *image_row_pitch = tmp_row_pitch;
  }

  if ((image->mem_object_type == CL_MEM_OBJECT_IMAGE3D ||
       image->mem_object_type == CL_MEM_OBJECT_IMAGE2D_ARRAY ||
       image->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY) &&
      image_slice_pitch == NULL) {
    BAIL_INFO(CL_INVALID_VALUE, command_queue->context,
              "Invalid slice pitch provided");
  }

  if (image->mem_object_type == CL_MEM_OBJECT_IMAGE2D ||
      image->mem_object_type == CL_MEM_OBJECT_IMAGE1D ||
      image->mem_object_type == CL_MEM_OBJECT_IMAGE1D_BUFFER) {
    if (image_slice_pitch != NULL) {
      *image_slice_pitch = 0;
    }
  } else if (image->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
    *image_slice_pitch = tmp_row_pitch;
  } else {
    *image_slice_pitch =
        image->fields.image_objs.image_desc->image_height * tmp_row_pitch;
  }

  if (image_slice_pitch != NULL) {
    tmp_slice_pitch = *image_slice_pitch;
  }

  if (!image->block_allocation->region->is_host_accessible) {
    size_t tmp_src_offset[3];
    size_t tmp_dst_offset[3];
    size_t tmp_cb[3];

    tmp_src_offset[0] = 0;
    tmp_src_offset[1] = 0;
    tmp_src_offset[2] = 0;
    tmp_dst_offset[0] =
        (size_t)((char *)image->host_mem.aligned_ptr - (char *)ACL_MEM_ALIGN);
    tmp_dst_offset[1] = 0;
    tmp_dst_offset[2] = 0;
    // For images, have to multiply the first region by the size of each element
    // because each element can be more than one byte wide.
    tmp_cb[0] = image->fields.image_objs.image_desc->image_width * element_size;
    if (image->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
      tmp_cb[1] = image->fields.image_objs.image_desc->image_array_size;
    } else {
      tmp_cb[1] = image->fields.image_objs.image_desc->image_height;
    }
    if (image->mem_object_type == CL_MEM_OBJECT_IMAGE2D_ARRAY) {
      tmp_cb[2] = image->fields.image_objs.image_desc->image_array_size;
    } else {
      tmp_cb[2] = image->fields.image_objs.image_desc->image_depth;
    }

    // We need to enqueue a memory transfer into the buffer->host_mem.
    //
    // It does not matter where the writable copy of the buffer is *right now*
    // because that can change based on other enqueued commands.  We must
    // always schedule the map, and then can elide the copy if at
    // command run time the buffer is already mapped to the host.
    //
    // Since we only have 1 bit to indicate the logical location
    // of the buffer (on the host or not), we copy the entire buffer.
    //
    // This will only move date if *at DMA time* the buffer is not
    // mapped to the host,
    status = l_enqueue_mem_transfer(command_queue, blocking_map,
                                    // Source is buffer
                                    image, tmp_src_offset, 0, 0,
                                    // dest is host memory pointer
                                    context->unwrapped_host_mem, tmp_dst_offset,
                                    tmp_row_pitch, tmp_slice_pitch, tmp_cb,
                                    num_events_in_wait_list, event_wait_list,
                                    event, CL_COMMAND_MAP_BUFFER, map_flags);
    acl_print_debug_msg(" map: ref count is %u\n", acl_ref_count(image));

    if (status != CL_SUCCESS)
      BAIL(status); // already signalled callback

    // The enqueue of the mem transfer will retain the buffer.
  } else {
    // We don't automatically move the live copy of a host buffer to the device.
    // So there is no reason to copy the data.
    status =
        acl_create_event(command_queue, num_events_in_wait_list,
                         event_wait_list, CL_COMMAND_MAP_BUFFER, &local_event);
    if (status != CL_SUCCESS)
      BAIL(status); // already signalled callback
    // Mark it as the trivial map buffer case.
    local_event->cmd.trivial = 1;
    local_event->cmd.info.trivial_mem_mapping.mem = image;

    // Should retain the memory object so that its metadata will stick around
    // until at least the command is completed.
    clRetainMemObject(image);
    acl_print_debug_msg(" map[%p] ref count is %u trivial case\n", image,
                        acl_ref_count(image));
  }

  // If nothing's blocking, then complete right away
  acl_idle_update(command_queue->context);

  if (blocking_map) {
    status = clWaitForEvents(1, &local_event);
  }

  if (event) {
    *event = local_event;
  } else {
    // User didn't care, so forget about the event.
    clReleaseEvent(local_event);
    acl_idle_update(command_queue->context); // Clean up early
  }
  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }
  // Returns pointer to the mapped location.
  if (image->host_mem.aligned_ptr) {
    // This case occurs for:
    //    - host-malloc'd CL_MEM_ALLOC_HOST_PTR buffers,
    //    - device-side buffer inaccessible from the host, but
    //    which has backing store in host_mem.  For this case
    //    the map event completion callback will actually copy the whole
    //    buffer over.
    result = ((char *)image->host_mem.aligned_ptr) + origin[0] * element_size;
  } else {
    // Can occur in legacy case where we never use host's malloc.
    result =
        ((char *)image->block_allocation->range.begin) +
        get_offset_for_image_param(context, image->mem_object_type, "data") +
        origin[0] * element_size;
  }
  acl_dump_mem_internal(image);

  return result;
}

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL clEnqueueMapImage(
    cl_command_queue command_queue, cl_mem image, cl_bool blocking_map,
    cl_map_flags map_flags, const size_t *origin, const size_t *region,
    size_t *image_row_pitch, size_t *image_slice_pitch,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event, cl_int *errcode_ret) {
  return clEnqueueMapImageIntelFPGA(command_queue, image, blocking_map,
                                    map_flags, origin, region, image_row_pitch,
                                    image_slice_pitch, num_events_in_wait_list,
                                    event_wait_list, event, errcode_ret);
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Map a part of given memory into the host address space.
//
// Can't fail if using host ptr or alloc host ptr.
//
// If the context requires that device side buffers have backing store on
// the host size, then we'll honour those mapping requests too.
// We'll copy the whole buffer back and forth as needed.
//
// Otherwise don't, i.e. assume that the device global memory is
// inaccessible from the host.
ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL clEnqueueMapBufferIntelFPGA(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_map,
    cl_map_flags map_flags, size_t offset, size_t cb, cl_uint num_events,
    const cl_event *events, cl_event *event, cl_int *errcode_ret) {
  void *result;
  cl_event local_event = 0; // used for blocking
  cl_context context;
  cl_int status;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    BAIL(CL_INVALID_COMMAND_QUEUE);
  }
  context = command_queue->context;
  if (!acl_mem_is_valid(buffer)) {
    BAIL_INFO(CL_INVALID_MEM_OBJECT, context, "Invalid memory object");
  }

  if (command_queue->context != buffer->context) {
    BAIL_INFO(
        CL_INVALID_CONTEXT, context,
        "Command queue and memory object are not associated with the same "
        "context");
  }

  if (!acl_bind_buffer_to_device(command_queue->device, buffer)) {
    BAIL_INFO(CL_MEM_OBJECT_ALLOCATION_FAILURE, context,
              "Deferred Allocation Failed");
  }

  // Check flags
  if (map_flags &
      ~(CL_MAP_READ | CL_MAP_WRITE | CL_MAP_WRITE_INVALIDATE_REGION)) {
    BAIL_INFO(CL_INVALID_VALUE, context, "Invalid or unsupported flags");
  }
  if (((map_flags & CL_MAP_READ) &&
       (map_flags & CL_MAP_WRITE_INVALIDATE_REGION)) ||
      ((map_flags & CL_MAP_WRITE) &&
       (map_flags & CL_MAP_WRITE_INVALIDATE_REGION))) {
    BAIL_INFO(
        CL_INVALID_VALUE, context,
        "CL_MAP_READ or CL_MAP_WRITE and CL_MAP_WRITE_INVALIDATE_REGION are "
        "specified but are mutually exclusive");
  }

  // Check if we can physically map the data into place.
  // We can map it either if it already resides on the host, or we
  // have backing store for it.
  cl_mem_flags flags = buffer->flags;
  if (!buffer->block_allocation->region->is_host_accessible &&
      !buffer->host_mem.aligned_ptr && !(flags & CL_MEM_USE_HOST_PTR)) {
    BAIL_INFO(CL_MAP_FAILURE, context,
              "Could not map the buffer into host memory");
  }

  if (offset + cb > buffer->size) {
    BAIL_INFO(CL_INVALID_VALUE, context,
              "Requested offset and byte count exceeds the buffer size");
  }

  if (flags & CL_MEM_USE_HOST_PTR) {
    size_t tmp_src_offset[3];
    size_t tmp_dst_offset[3];
    size_t tmp_cb[3];

    tmp_src_offset[0] = 0;
    tmp_src_offset[1] = 0;
    tmp_src_offset[2] = 0;
    tmp_dst_offset[0] = (size_t)((char *)buffer->fields.buffer_objs.host_ptr -
                                 (char *)ACL_MEM_ALIGN);
    tmp_dst_offset[1] = 0;
    tmp_dst_offset[2] = 0;
    tmp_cb[0] = buffer->size;
    tmp_cb[1] = 1;
    tmp_cb[2] = 1;

    status = l_enqueue_mem_transfer(
        command_queue, blocking_map,
        // Source is buffer
        buffer, tmp_src_offset, 0, 0,
        // dest is host memory pointer
        context->unwrapped_host_mem, tmp_dst_offset, 0, 0, tmp_cb, num_events,
        events, &local_event, CL_COMMAND_MAP_BUFFER, map_flags);
    acl_print_debug_msg(" map: ref count is %u\n", acl_ref_count(buffer));

    if (status != CL_SUCCESS)
      BAIL(status); // already signalled callback

  } else if (!buffer->block_allocation->region->is_host_accessible) {
    size_t tmp_src_offset[3];
    size_t tmp_dst_offset[3];
    size_t tmp_cb[3];

    tmp_src_offset[0] = 0;
    tmp_src_offset[1] = 0;
    tmp_src_offset[2] = 0;
    tmp_dst_offset[0] =
        (size_t)((char *)buffer->host_mem.aligned_ptr - (char *)ACL_MEM_ALIGN);
    tmp_dst_offset[1] = 0;
    tmp_dst_offset[2] = 0;
    tmp_cb[0] = buffer->size;
    tmp_cb[1] = 1;
    tmp_cb[2] = 1;

    // We need to enqueue a memory transfer into the buffer->host_mem.
    //
    // It does not matter where the writable copy of the buffer is *right now*
    // because that can change based on other enqueued commands.  We must
    // always schedule the map, and then can elide the copy if at
    // command run time the buffer is already mapped to the host.
    //
    // Since we only have 1 bit to indicate the logical location
    // of the buffer (on the host or not), we copy the entire buffer.
    //
    // This will only move date if *at DMA time* the buffer is not
    // mapped to the host,
    status = l_enqueue_mem_transfer(
        command_queue, blocking_map,
        // Source is buffer
        buffer, tmp_src_offset, 0, 0,
        // dest is host memory pointer
        context->unwrapped_host_mem, tmp_dst_offset, 0, 0, tmp_cb, num_events,
        events, &local_event, CL_COMMAND_MAP_BUFFER, map_flags);
    acl_print_debug_msg(" map: ref count is %u\n", acl_ref_count(buffer));

    if (status != CL_SUCCESS)
      BAIL(status); // already signalled callback

    // The enqueue of the mem transfer will retain the buffer.
  } else {
    // We don't automatically move the live copy of a host buffer to the device.
    // So there is no reason to copy the data.
    status = acl_create_event(command_queue, num_events, events,
                              CL_COMMAND_MAP_BUFFER, &local_event);
    if (status != CL_SUCCESS)
      BAIL(status); // already signalled callback
    // Mark it as the trivial map buffer case.
    local_event->cmd.trivial = 1;
    local_event->cmd.info.trivial_mem_mapping.mem = buffer;

    // Should retain the memory object so that its metadata will stick around
    // until at least the command is completed.
    clRetainMemObject(buffer);
    acl_print_debug_msg(" map[%p] ref count is %u trivial case\n", buffer,
                        acl_ref_count(buffer));
  }

  // If nothing's blocking, then complete right away
  acl_idle_update(command_queue->context);

  if (blocking_map) {
    status = clWaitForEvents(1, &local_event);
  }

  if (event) {
    *event = local_event;
  } else {
    // User didn't care, so forget about the event.
    clReleaseEvent(local_event);
    acl_idle_update(command_queue->context); // Clean up early
  }
  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }
  // Returns pointer to the mapped location.
  if (buffer->host_mem.aligned_ptr) {
    // This case occurs for:
    //    - host-malloc'd CL_MEM_ALLOC_HOST_PTR buffers,
    //    - device-side buffer inaccessible from the host, but
    //    which has backing store in host_mem.  For this case
    //    the map event completion callback will actually copy the whole
    //    buffer over.
    if (flags & CL_MEM_USE_HOST_PTR) {
      result = (char *)buffer->fields.buffer_objs.host_ptr + offset;
    } else {
      result = ((char *)buffer->host_mem.aligned_ptr) + offset;
    }
  } else {
    if (flags & CL_MEM_USE_HOST_PTR) {
      result = (char *)buffer->fields.buffer_objs.host_ptr + offset;
    } else {
      // Can occur in legacy case where we never use host's malloc
      result = ((char *)buffer->block_allocation->range.begin) + offset;
    }
  }
  acl_dump_mem_internal(buffer);
  return result;
}

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL clEnqueueMapBuffer(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_map,
    cl_map_flags map_flags, size_t offset, size_t cb, cl_uint num_events,
    const cl_event *events, cl_event *event, cl_int *errcode_ret) {
  return clEnqueueMapBufferIntelFPGA(command_queue, buffer, blocking_map,
                                     map_flags, offset, cb, num_events, events,
                                     event, errcode_ret);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueUnmapMemObjectIntelFPGA(
    cl_command_queue command_queue, cl_mem mem, void *mapped_ptr,
    cl_uint num_events, const cl_event *events, cl_event *event) {
  cl_event local_event = 0;
  cl_context context;
  cl_int status;
  char *valid_base_ptr;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  context = command_queue->context;
  if (!acl_mem_is_valid(mem)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, context, "Memory object is invalid");
  }

  if (command_queue->context != mem->context) {
    ERR_RET(CL_INVALID_CONTEXT, context,
            "Command queue and memory object are not associated with the "
            "same context");
  }
  cl_mem_flags flags = mem->flags;
  if ((!mem->block_allocation->region->is_host_accessible &&
       !mem->host_mem.aligned_ptr && !(flags & CL_MEM_USE_HOST_PTR)) ||
      mem->allocation_deferred) {
    ERR_RET(CL_MAP_FAILURE, context,
            "Could not have mapped the buffer into host memory");
  }

  // Necessary sanity check on the pointer.
  // This checks that the *start* of the mapped buffer is in bounds for
  // the mem object.
  if (flags & CL_MEM_USE_HOST_PTR) {
    valid_base_ptr = (char *)mem->fields.buffer_objs.host_ptr;
  } else {
    valid_base_ptr = (char *)(mem->host_mem.aligned_ptr
                                  ? mem->host_mem.aligned_ptr
                                  : mem->block_allocation->range.begin);
  }
  if ((valid_base_ptr - (char *)mapped_ptr) > 0) {
    ERR_RET(CL_INVALID_VALUE, context,
            "Invalid mapped_ptr argument: it lies outside the buffer");
  }
  if (((char *)mapped_ptr - (valid_base_ptr + mem->size)) >= 0) {
    ERR_RET(CL_INVALID_VALUE, context,
            "Invalid mapped_ptr argument: it lies outside the buffer");
  }

  // This is the mirror image of mapping the buffer in the first place.
  if (flags & CL_MEM_USE_HOST_PTR) {
    size_t tmp_src_offset[3];
    size_t tmp_dst_offset[3];
    size_t tmp_cb[3];

    // When unmapping an image, use the built in image sizes
    tmp_src_offset[0] = (size_t)((char *)mem->fields.buffer_objs.host_ptr -
                                 (char *)ACL_MEM_ALIGN);
    tmp_src_offset[1] = 0;
    tmp_src_offset[2] = 0;
    tmp_dst_offset[0] = 0;
    tmp_dst_offset[1] = 0;
    tmp_dst_offset[2] = 0;
    if (is_image(mem)) {

      size_t image_element_size = acl_get_image_element_size(
          mem->context, mem->fields.image_objs.image_format, &status);
      if (status != CL_SUCCESS) {
        return status;
      }

      tmp_cb[0] =
          mem->fields.image_objs.image_desc->image_width * image_element_size;
      tmp_cb[1] = mem->fields.image_objs.image_desc->image_height;
      tmp_cb[2] = mem->fields.image_objs.image_desc->image_depth;
    } else {
      tmp_cb[0] = mem->size;
      tmp_cb[1] = 1;
      tmp_cb[2] = 1;
    }

    acl_print_debug_msg(" unmapping case writable %d\n",
                        mem->writable_copy_on_host);
    // We need to enqueue a memory transfer into the buffer->host_mem.
    // Since we only have 1 bit to indicate the logical location
    // of the buffer (on the host or not), we copy the entire buffer.
    //
    // This will only move date if *at DMA time* the buffer is not
    // mapped to the host *and* the mapping included the write flag.
    status = l_enqueue_mem_transfer(
        command_queue,
        0, // not blocking
        // Src is host memory pointer
        context->unwrapped_host_mem, tmp_src_offset, 0, 0,
        // Dest is buffer
        mem, tmp_dst_offset, 0, 0, tmp_cb, num_events, events, &local_event,
        CL_COMMAND_UNMAP_MEM_OBJECT,
        // We don't need map flags.  We just return
        // the writable copy back to the host.  So we'll only need
        // to inspect cl_mem->writable_copy_on_host at command
        // execution time.
        0);
    if (status != CL_SUCCESS)
      return status; // already signalled callback
    acl_print_debug_msg("mem[%p] enqueue unmap. refcount %u\n", mem,
                        acl_ref_count(mem));

  } else if (!mem->block_allocation->region->is_host_accessible) {
    size_t tmp_src_offset[3];
    size_t tmp_dst_offset[3];
    size_t tmp_cb[3];

    // When unmapping an image, use the built in image sizes
    tmp_src_offset[0] =
        (size_t)((char *)mem->host_mem.aligned_ptr - (char *)ACL_MEM_ALIGN);
    tmp_src_offset[1] = 0;
    tmp_src_offset[2] = 0;
    tmp_dst_offset[0] = 0;
    tmp_dst_offset[1] = 0;
    tmp_dst_offset[2] = 0;
    if (is_image(mem)) {

      size_t image_element_size = acl_get_image_element_size(
          mem->context, mem->fields.image_objs.image_format, &status);
      if (status != CL_SUCCESS) {
        return status;
      }

      tmp_cb[0] =
          mem->fields.image_objs.image_desc->image_width * image_element_size;
      tmp_cb[1] = mem->fields.image_objs.image_desc->image_height;
      tmp_cb[2] = mem->fields.image_objs.image_desc->image_depth;
    } else {
      tmp_cb[0] = mem->size;
      tmp_cb[1] = 1;
      tmp_cb[2] = 1;
    }

    acl_print_debug_msg(" unmapping case writable %d\n",
                        mem->writable_copy_on_host);
    // We need to enqueue a memory transfer into the buffer->host_mem.
    // Since we only have 1 bit to indicate the logical location
    // of the buffer (on the host or not), we copy the entire buffer.
    //
    // This will only move date if *at DMA time* the buffer is not
    // mapped to the host *and* the mapping included the write flag.
    status = l_enqueue_mem_transfer(
        command_queue,
        0, // not blocking
        // Src is host memory pointer
        context->unwrapped_host_mem, tmp_src_offset, 0, 0,
        // Dest is buffer
        mem, tmp_dst_offset, 0, 0, tmp_cb, num_events, events, &local_event,
        CL_COMMAND_UNMAP_MEM_OBJECT,
        // We don't need map flags.  We just return
        // the writable copy back to the host.  So we'll only need
        // to inspect cl_mem->writable_copy_on_host at command
        // execution time.
        0);
    if (status != CL_SUCCESS)
      return status; // already signalled callback
    acl_print_debug_msg("mem[%p] enqueue unmap. refcount %u\n", mem,
                        acl_ref_count(mem));
  } else {
    status = acl_create_event(command_queue, num_events, events,
                              CL_COMMAND_UNMAP_MEM_OBJECT, &local_event);
    if (status != CL_SUCCESS)
      return status; // already signalled callback
    local_event->cmd.trivial = 1;
    local_event->cmd.info.trivial_mem_mapping.mem = mem;
    // Should retain the memory object so that its metadata will stick around
    // until at least the command is completed.
    clRetainMemObject(mem);
    acl_print_debug_msg("mem[%p] enqueue unmap trivial. refcount %u\n", mem,
                        acl_ref_count(mem));
  }

  acl_idle_update(
      command_queue
          ->context); // If nothing's blocking, then complete right away

  if (event) {
    *event = local_event;
  } else {
    // User didn't care, so forget about the event.
    clReleaseEvent(local_event);
    acl_idle_update(command_queue->context); // Clean up early
  }
  return CL_SUCCESS;
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueUnmapMemObject(
    cl_command_queue command_queue, cl_mem mem, void *mapped_ptr,
    cl_uint num_events, const cl_event *events, cl_event *event) {
  return clEnqueueUnmapMemObjectIntelFPGA(command_queue, mem, mapped_ptr,
                                          num_events, events, event);
}

// Schedule a buffer read into host memory.
ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadBufferIntelFPGA(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read,
    size_t offset, size_t cb, void *ptr, cl_uint num_events,
    const cl_event *events, cl_event *event) {
  size_t tmp_src_offset[3];
  size_t tmp_dst_offset[3];
  size_t tmp_cb[3];
  std::scoped_lock lock{acl_mutex_wrapper};

  tmp_src_offset[0] = offset;
  tmp_src_offset[1] = 0;
  tmp_src_offset[2] = 0;
  tmp_dst_offset[0] = (size_t)((char *)ptr - (char *)ACL_MEM_ALIGN);
  tmp_dst_offset[1] = 0;
  tmp_dst_offset[2] = 0;
  tmp_cb[0] = cb;
  tmp_cb[1] = 1;
  tmp_cb[2] = 1;

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (ptr == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer argument cannot be NULL");
  }
  if (!acl_bind_buffer_to_device(command_queue->device, buffer)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }

  {
    cl_int ret = l_enqueue_mem_transfer(
        command_queue, blocking_read, buffer, tmp_src_offset, 0, 0,
        command_queue->context->unwrapped_host_mem,
        tmp_dst_offset, // see creation of the unwrapped_host_mem
        0, 0, tmp_cb, num_events, events, event, CL_COMMAND_READ_BUFFER, 0);
    return ret;
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadBuffer(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read,
    size_t offset, size_t cb, void *ptr, cl_uint num_events,
    const cl_event *events, cl_event *event) {
  return clEnqueueReadBufferIntelFPGA(command_queue, buffer, blocking_read,
                                      offset, cb, ptr, num_events, events,
                                      event);
}

// Schedule a buffer read of a rectangular region into host memory.
ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadBufferRectIntelFPGA(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read,
    const size_t *buffer_offset, const size_t *host_offset,
    const size_t *region, size_t buffer_row_pitch, size_t buffer_slice_pitch,
    size_t host_row_pitch, size_t host_slice_pitch, void *ptr,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  size_t tmp_src_offset[3];
  size_t tmp_dst_offset[3];
  size_t tmp_cb[3];
  std::scoped_lock lock{acl_mutex_wrapper};

  if (buffer_row_pitch == 0) {
    buffer_row_pitch = region[0];
  }
  if (buffer_slice_pitch == 0) {
    buffer_slice_pitch = region[1] * buffer_row_pitch;
  }
  if (host_row_pitch == 0) {
    host_row_pitch = region[0];
  }
  if (host_slice_pitch == 0) {
    host_slice_pitch = region[1] * host_row_pitch;
  }

  tmp_src_offset[0] = buffer_offset[0];
  tmp_src_offset[1] = buffer_offset[1];
  tmp_src_offset[2] = buffer_offset[2];
  tmp_dst_offset[0] = (char *)ptr - (char *)ACL_MEM_ALIGN + host_offset[0];
  tmp_dst_offset[1] = host_offset[1];
  tmp_dst_offset[2] = host_offset[2];
  tmp_cb[0] = region[0];
  tmp_cb[1] = region[1];
  tmp_cb[2] = region[2];

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (!acl_mem_is_valid(buffer)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context, "Buffer is invalid");
  }
  if (ptr == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer argument cannot be NULL");
  }
  if (!acl_bind_buffer_to_device(command_queue->device, buffer)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }
  {
    cl_int ret = l_enqueue_mem_transfer(
        command_queue, blocking_read, buffer, tmp_src_offset, buffer_row_pitch,
        buffer_slice_pitch, command_queue->context->unwrapped_host_mem,
        tmp_dst_offset, // see creation of the unwrapped_host_mem
        host_row_pitch, host_slice_pitch, tmp_cb, num_events_in_wait_list,
        event_wait_list, event, CL_COMMAND_READ_BUFFER, 0);
    return ret;
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadBufferRect(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read,
    const size_t *buffer_origin, const size_t *host_origin,
    const size_t *region, size_t buffer_row_pitch, size_t buffer_slice_pitch,
    size_t host_row_pitch, size_t host_slice_pitch, void *ptr,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  return clEnqueueReadBufferRectIntelFPGA(
      command_queue, buffer, blocking_read, buffer_origin, host_origin, region,
      buffer_row_pitch, buffer_slice_pitch, host_row_pitch, host_slice_pitch,
      ptr, num_events_in_wait_list, event_wait_list, event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteBufferIntelFPGA(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_write,
    size_t offset, size_t cb, const void *ptr, cl_uint num_events,
    const cl_event *events, cl_event *event) {
  size_t tmp_src_offset[3];
  size_t tmp_dst_offset[3];
  size_t tmp_cb[3];
  std::scoped_lock lock{acl_mutex_wrapper};

  tmp_src_offset[0] = (size_t)((char *)ptr - (const char *)ACL_MEM_ALIGN);
  tmp_src_offset[1] = 0;
  tmp_src_offset[2] = 0;
  tmp_dst_offset[0] = offset;
  tmp_dst_offset[1] = 0;
  tmp_dst_offset[2] = 0;
  tmp_cb[0] = cb;
  tmp_cb[1] = 1;
  tmp_cb[2] = 1;

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (!acl_bind_buffer_to_device(command_queue->device, buffer)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }

  {
    cl_int ret = l_enqueue_mem_transfer(
        command_queue, blocking_write,
        command_queue->context->unwrapped_host_mem, tmp_src_offset, 0, 0,
        buffer, tmp_dst_offset, 0, 0, tmp_cb, num_events, events, event,
        CL_COMMAND_WRITE_BUFFER, 0);
    return ret;
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteBuffer(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_write,
    size_t offset, size_t cb, const void *ptr, cl_uint num_events,
    const cl_event *events, cl_event *event) {
  return clEnqueueWriteBufferIntelFPGA(command_queue, buffer, blocking_write,
                                       offset, cb, ptr, num_events, events,
                                       event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteBufferRectIntelFPGA(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_write,
    const size_t *buffer_offset, const size_t *host_offset,
    const size_t *region, size_t buffer_row_pitch, size_t buffer_slice_pitch,
    size_t host_row_pitch, size_t host_slice_pitch, const void *ptr,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  size_t tmp_src_offset[3];
  size_t tmp_dst_offset[3];
  size_t tmp_cb[3];
  std::scoped_lock lock{acl_mutex_wrapper};

  if (buffer_row_pitch == 0) {
    buffer_row_pitch = region[0];
  }
  if (buffer_slice_pitch == 0) {
    buffer_slice_pitch = region[1] * buffer_row_pitch;
  }
  if (host_row_pitch == 0) {
    host_row_pitch = region[0];
  }
  if (host_slice_pitch == 0) {
    host_slice_pitch = region[1] * host_row_pitch;
  }

  tmp_src_offset[0] = (char *)ptr - (char *)ACL_MEM_ALIGN + host_offset[0];
  tmp_src_offset[1] = host_offset[1];
  tmp_src_offset[2] = host_offset[2];
  tmp_dst_offset[0] = buffer_offset[0];
  tmp_dst_offset[1] = buffer_offset[1];
  tmp_dst_offset[2] = buffer_offset[2];
  tmp_cb[0] = region[0];
  tmp_cb[1] = region[1];
  tmp_cb[2] = region[2];

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (!acl_mem_is_valid(buffer)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context, "Buffer is invalid");
  }
  if (ptr == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer argument cannot be NULL");
  }
  if (!acl_bind_buffer_to_device(command_queue->device, buffer)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }
  {
    cl_int ret = l_enqueue_mem_transfer(
        command_queue, blocking_write,
        command_queue->context->unwrapped_host_mem, tmp_src_offset,
        host_row_pitch, host_slice_pitch, buffer,
        tmp_dst_offset, // see creation of the unwrapped_host_mem
        buffer_row_pitch, buffer_slice_pitch, tmp_cb, num_events_in_wait_list,
        event_wait_list, event, CL_COMMAND_WRITE_BUFFER, 0);
    return ret;
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteBufferRect(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_write,
    const size_t *buffer_offset, const size_t *host_offset,
    const size_t *region, size_t buffer_row_pitch, size_t buffer_slice_pitch,
    size_t host_row_pitch, size_t host_slice_pitch, const void *ptr,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  return clEnqueueWriteBufferRectIntelFPGA(
      command_queue, buffer, blocking_write, buffer_offset, host_offset, region,
      buffer_row_pitch, buffer_slice_pitch, host_row_pitch, host_slice_pitch,
      ptr, num_events_in_wait_list, event_wait_list, event);
}

ACL_EXPORT
CL_API_ENTRY cl_int clEnqueueFillBufferIntelFPGA(
    cl_command_queue command_queue, cl_mem buffer, const void *pattern,
    size_t pattern_size, size_t offset, size_t size,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  size_t tmp_src_offset[3];
  size_t tmp_dst_offset[3];
  size_t tmp_cb[3];
  char *ptr;

  cl_event tmp_event;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  if (!acl_mem_is_valid(buffer)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context, "Buffer is invalid");
  }

  // Pattern size can only be {1,2,4,8,...,1024 sizeof(double16)}.
  if (pattern_size == 0 || pattern_size > 1024 ||
      (pattern_size & (pattern_size - 1))) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context, "Invalid pattern size");
  }

  if (offset % pattern_size != 0 || size % pattern_size != 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Offset and size must be a multiple of pattern size");
  }

  if (pattern == NULL) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context, "pattern cannot be NULL");
  }

  if (!acl_bind_buffer_to_device(command_queue->device, buffer)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
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

  // Replicating the pattern, size/pattern_size times.
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

  tmp_src_offset[0] = (size_t)((char *)ptr - (const char *)ACL_MEM_ALIGN);
  tmp_src_offset[1] = 0;
  tmp_src_offset[2] = 0;
  tmp_dst_offset[0] = offset;
  tmp_dst_offset[1] = 0;
  tmp_dst_offset[2] = 0;
  tmp_cb[0] = size;
  tmp_cb[1] = 1;
  tmp_cb[2] = 1;

  {
    cl_int ret = l_enqueue_mem_transfer(
        command_queue, 0, command_queue->context->unwrapped_host_mem,
        tmp_src_offset, 0, 0, buffer, tmp_dst_offset, 0, 0, tmp_cb,
        num_events_in_wait_list, event_wait_list, &tmp_event,
        CL_COMMAND_WRITE_BUFFER, 0);

    if (ret != CL_SUCCESS) {
      acl_mem_aligned_free(command_queue->context, aligned_ptr);
      acl_free(aligned_ptr);
      acl_free(callback_data);
      return ret;
    }
    callback_data[0] = (void *)(aligned_ptr);
    if (event) {
      *event = tmp_event;
      callback_data[1] = NULL; // User needs the event, so we shouldn't release
                               // it after the event completion.
    } else {
      callback_data[1] =
          tmp_event; // Passing the event to release it when the event is done.
    }
    clSetEventCallback(tmp_event, CL_COMPLETE,
                       acl_free_allocation_after_event_completion,
                       (void *)callback_data);
    return ret;
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int clEnqueueFillBuffer(cl_command_queue command_queue,
                                        cl_mem buffer, const void *pattern,
                                        size_t pattern_size, size_t offset,
                                        size_t size,
                                        cl_uint num_events_in_wait_list,
                                        const cl_event *event_wait_list,
                                        cl_event *event) {
  return clEnqueueFillBufferIntelFPGA(
      command_queue, buffer, pattern, pattern_size, offset, size,
      num_events_in_wait_list, event_wait_list, event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyBufferIntelFPGA(
    cl_command_queue command_queue, cl_mem src_buffer, cl_mem dst_buffer,
    size_t src_offset, size_t dst_offset, size_t cb, cl_uint num_events,
    const cl_event *events, cl_event *event) {
  size_t tmp_src_offset[3];
  size_t tmp_dst_offset[3];
  size_t tmp_cb[3];
  std::scoped_lock lock{acl_mutex_wrapper};

  tmp_src_offset[0] = src_offset;
  tmp_src_offset[1] = 0;
  tmp_src_offset[2] = 0;
  tmp_dst_offset[0] = dst_offset;
  tmp_dst_offset[1] = 0;
  tmp_dst_offset[2] = 0;
  tmp_cb[0] = cb;
  tmp_cb[1] = 1;
  tmp_cb[2] = 1;

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (!acl_mem_is_valid(src_buffer)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Source buffer is invalid");
  }
  if (!acl_mem_is_valid(dst_buffer)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Destination buffer is invalid");
  }
  if (!acl_bind_buffer_to_device(command_queue->device, src_buffer)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }
  if (!acl_bind_buffer_to_device(command_queue->device, dst_buffer)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }

  {
    cl_int ret = l_enqueue_mem_transfer(
        command_queue, 0, src_buffer, tmp_src_offset, 0, 0, dst_buffer,
        tmp_dst_offset, 0, 0, tmp_cb, num_events, events, event,
        CL_COMMAND_COPY_BUFFER, 0);
    return ret;
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyBuffer(
    cl_command_queue command_queue, cl_mem src_buffer, cl_mem dst_buffer,
    size_t src_offset, size_t dst_offset, size_t cb, cl_uint num_events,
    const cl_event *events, cl_event *event) {
  return clEnqueueCopyBufferIntelFPGA(command_queue, src_buffer, dst_buffer,
                                      src_offset, dst_offset, cb, num_events,
                                      events, event);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyBufferRectIntelFPGA(
    cl_command_queue command_queue, cl_mem src_buffer, cl_mem dst_buffer,
    const size_t *src_origin, const size_t *dst_origin, const size_t *region,
    size_t src_row_pitch, size_t src_slice_pitch, size_t dst_row_pitch,
    size_t dst_slice_pitch, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  size_t tmp_src_offset[3];
  size_t tmp_dst_offset[3];
  size_t tmp_cb[3];
  std::scoped_lock lock{acl_mutex_wrapper};

  if (src_row_pitch == 0) {
    src_row_pitch = region[0];
  }
  if (src_slice_pitch == 0) {
    src_slice_pitch = region[1] * src_row_pitch;
  }
  if (dst_row_pitch == 0) {
    dst_row_pitch = region[0];
  }
  if (dst_slice_pitch == 0) {
    dst_slice_pitch = region[1] * dst_row_pitch;
  }

  tmp_src_offset[0] = src_origin[0];
  tmp_src_offset[1] = src_origin[1];
  tmp_src_offset[2] = src_origin[2];
  tmp_dst_offset[0] = dst_origin[0];
  tmp_dst_offset[1] = dst_origin[1];
  tmp_dst_offset[2] = dst_origin[2];
  tmp_cb[0] = region[0];
  tmp_cb[1] = region[1];
  tmp_cb[2] = region[2];

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (!acl_mem_is_valid(src_buffer)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Source buffer is invalid");
  }
  if (!acl_mem_is_valid(dst_buffer)) {
    ERR_RET(CL_INVALID_MEM_OBJECT, command_queue->context,
            "Destination buffer is invalid");
  }
  if (!acl_bind_buffer_to_device(command_queue->device, src_buffer)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }
  if (!acl_bind_buffer_to_device(command_queue->device, dst_buffer)) {
    ERR_RET(CL_MEM_OBJECT_ALLOCATION_FAILURE, command_queue->context,
            "Deferred Allocation Failed");
  }

  if (src_buffer == dst_buffer) {
    if (src_row_pitch != dst_row_pitch) {
      ERR_RET(CL_INVALID_VALUE, command_queue->context,
              "Source buffer and destination buffer are the same, but row "
              "pitches do not match");
    }
    if (src_slice_pitch != dst_slice_pitch) {
      ERR_RET(CL_INVALID_VALUE, command_queue->context,
              "Source buffer and destination buffer are the same, but slice "
              "pitches do not match");
    }
    if (check_copy_overlap(tmp_src_offset, tmp_dst_offset, tmp_cb,
                           src_row_pitch, src_slice_pitch)) {
      ERR_RET(CL_MEM_COPY_OVERLAP, command_queue->context,
              "Source buffer and destination buffer are the same and regions "
              "overlaps");
    }
  }
  {
    cl_int ret = l_enqueue_mem_transfer(
        command_queue, 0, src_buffer, tmp_src_offset, src_row_pitch,
        src_slice_pitch, dst_buffer,
        tmp_dst_offset, // see creation of the unwrapped_host_mem
        dst_row_pitch, dst_slice_pitch, tmp_cb, num_events_in_wait_list,
        event_wait_list, event, CL_COMMAND_COPY_BUFFER, 0);
    return ret;
  }
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyBufferRect(
    cl_command_queue command_queue, cl_mem src_buffer, cl_mem dst_buffer,
    const size_t *src_origin, const size_t *dst_origin, const size_t *region,
    size_t src_row_pitch, size_t src_slice_pitch, size_t dst_row_pitch,
    size_t dst_slice_pitch, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  return clEnqueueCopyBufferRectIntelFPGA(
      command_queue, src_buffer, dst_buffer, src_origin, dst_origin, region,
      src_row_pitch, src_slice_pitch, dst_row_pitch, dst_slice_pitch,
      num_events_in_wait_list, event_wait_list, event);
}

ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL clCreatePipeIntelFPGA(
    cl_context context, cl_mem_flags flags, cl_uint pipe_packet_size,
    cl_uint pipe_max_packets, const cl_pipe_properties *properties,
    cl_int *errcode_ret) {
  cl_mem mem;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_context_is_valid(context)) {
    BAIL(CL_INVALID_CONTEXT);
  }

  // Check flags
  {
    // Check for invalid enum bits
    if (flags & ~(CL_MEM_READ_WRITE | CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY |
                  CL_MEM_HOST_READ_ONLY | CL_MEM_HOST_WRITE_ONLY)) {
      BAIL_INFO(CL_INVALID_VALUE, context, "Invalid or unsupported flags");
    }

    {
      int num_rw_specs = 0;
      int num_hostrw_specs = 0;

      if (flags & CL_MEM_READ_WRITE)
        num_rw_specs++;
      if (flags & CL_MEM_READ_ONLY)
        num_rw_specs++;
      if (flags & CL_MEM_WRITE_ONLY)
        num_rw_specs++;
      if (flags & CL_MEM_HOST_READ_ONLY)
        num_hostrw_specs++;
      if (flags & CL_MEM_HOST_WRITE_ONLY)
        num_hostrw_specs++;

      // Check for exactly one read/write spec
      if (num_rw_specs > 1) {
        BAIL_INFO(CL_INVALID_VALUE, context,
                  "More than one read/write flag is specified");
      }
      if (num_hostrw_specs > 1) {
        BAIL_INFO(CL_INVALID_VALUE, context,
                  "More than one host read/write flag is specified");
      }

      // Default to CL_MEM_READ_WRITE.
      if ((num_rw_specs == 0) && (num_hostrw_specs == 0)) {
        flags |= CL_MEM_READ_WRITE;
      }

      if (((flags & CL_MEM_HOST_READ_ONLY) && (flags & CL_MEM_READ_ONLY)) ||
          ((flags & CL_MEM_HOST_WRITE_ONLY) && (flags & CL_MEM_WRITE_ONLY)) ||
          (num_hostrw_specs && (flags & CL_MEM_READ_WRITE))) {
        BAIL_INFO(CL_INVALID_VALUE, context,
                  "Conflicting read/write flags specified");
      }
    }
  }

  if (pipe_packet_size == 0) {
    BAIL_INFO(CL_INVALID_PIPE_SIZE, context, "Pipe packet size is zero");
  }
  if (pipe_packet_size > acl_platform.pipe_max_packet_size) {
    BAIL_INFO(CL_INVALID_PIPE_SIZE, context,
              "Pipe packet size exceeds maximum allowed");
  }

  if (properties != NULL) {
    BAIL_INFO(CL_INVALID_VALUE, context, "Properties must be NULL for pipes");
  }

  mem = acl_alloc_cl_mem();
  if (!mem) {
    BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
              "Could not allocate a cl_mem object");
  }

  acl_reset_ref_count(mem);

  mem->mem_object_type = CL_MEM_OBJECT_PIPE;
  mem->dispatch = &acl_icd_dispatch;
  mem->allocation_deferred = 0;
  mem->host_mem.aligned_ptr = NULL;
  mem->host_mem.raw = NULL;
  mem->mem_cpy_host_ptr_pending = 0;
  mem->destructor_callback_list = NULL;
  mem->block_allocation = NULL;
  mem->writable_copy_on_host = 0;

  mem->fields.pipe_objs.pipe_packet_size = pipe_packet_size;
  mem->fields.pipe_objs.pipe_max_packets = pipe_max_packets;

  mem->context = context;
  mem->flags = flags;
  mem->size = 0;

  mem->host_pipe_info = NULL;

  /* If it is a host pipe, populate the host pipe data structure in the pipe  */
  if (flags & (CL_MEM_HOST_READ_ONLY | CL_MEM_HOST_WRITE_ONLY)) {
    host_pipe_t *host_pipe_info;

    host_pipe_info = acl_new<host_pipe_t>();
    if (!host_pipe_info) {
      acl_free_cl_mem(mem);
      BAIL_INFO(CL_OUT_OF_HOST_MEMORY, context,
                "Could not allocate memory for internal data structure");
    }
    host_pipe_info->m_physical_device_id = 0;
    host_pipe_info->m_channel_handle = -1;
    host_pipe_info->size_buffered = 0;
    host_pipe_info->m_binded_kernel = 0;
    host_pipe_info->binded = false;
    host_pipe_info->host_pipe_channel_id = "";
    acl_mutex_init(&(host_pipe_info->m_lock), NULL);

    mem->host_pipe_info = host_pipe_info;
  }

  // push_back the new cl_pipe
  context->pipe_vec.push_back(mem);

  mem->mapping_count = 0;

  acl_retain(mem);
  acl_retain(context);

  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  acl_track_object(ACL_OBJ_MEM_OBJECT, mem);

  return mem;
}

ACL_EXPORT
CL_API_ENTRY cl_mem CL_API_CALL
clCreatePipe(cl_context context, cl_mem_flags flags, cl_uint pipe_packet_size,
             cl_uint pipe_max_packets, const cl_pipe_properties *properties,
             cl_int *errcode_ret) {
  return clCreatePipeIntelFPGA(context, flags, pipe_packet_size,
                               pipe_max_packets, properties, errcode_ret);
}

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL clGetPipeInfoIntelFPGA(
    cl_mem pipe, cl_pipe_info param_name, size_t param_value_size,
    void *param_value, size_t *param_value_size_ret) {
  acl_result_t result;
  std::scoped_lock lock{acl_mutex_wrapper};

  RESULT_INIT;

  if (!acl_mem_is_valid(pipe)) {
    return CL_INVALID_MEM_OBJECT;
  }

  // Wrong object type
  if (pipe->mem_object_type != CL_MEM_OBJECT_PIPE) {
    return CL_INVALID_MEM_OBJECT;
  }

  switch (param_name) {
  case CL_PIPE_PACKET_SIZE:
    RESULT_UINT(pipe->fields.pipe_objs.pipe_packet_size);
    break;
  case CL_PIPE_MAX_PACKETS:
    RESULT_UINT(pipe->fields.pipe_objs.pipe_max_packets);
    break;
  default:
    break;
  }

  if (result.size == 0) {
    // We didn't implement the enum. Error out semi-gracefully.
    return CL_INVALID_VALUE;
  }

  if (param_value) {
    // Actually try to return the string.
    if (param_value_size < result.size) {
      // Buffer is too small to hold the return value.
      return CL_INVALID_VALUE;
    }
    RESULT_COPY(param_value, param_value_size);
  }

  if (param_value_size_ret) {
    *param_value_size_ret = result.size;
  }
  return CL_SUCCESS;
}

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL
clGetPipeInfo(cl_mem pipe, cl_pipe_info param_name, size_t param_value_size,
              void *param_value, size_t *param_value_size_ret) {
  return clGetPipeInfoIntelFPGA(pipe, param_name, param_value_size, param_value,
                                param_value_size_ret);
}

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL clEnqueueMigrateMemObjectsIntelFPGA(
    cl_command_queue command_queue, cl_uint num_mem_objects,
    const cl_mem *mem_objects, cl_mem_migration_flags flags,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  size_t i;
  cl_int status;
  cl_event local_event = 0;
  unsigned int physical_id;
  unsigned int mem_id;

  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  if (num_mem_objects == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Number of memory objects is zero");
  }
  if (mem_objects == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Array of memory objects is NULL");
  }

  for (i = 0; i < num_mem_objects; ++i) {
    if (!acl_mem_is_valid(mem_objects[i])) {
      return CL_INVALID_MEM_OBJECT;
    }

    if (command_queue->context != mem_objects[i]->context) {
      return CL_INVALID_CONTEXT;
    }
  }

  if (flags != 0 && (flags & ~(CL_MIGRATE_MEM_OBJECT_HOST |
                               CL_MIGRATE_MEM_OBJECT_CONTENT_UNDEFINED))) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context, "Invalid flags provided");
  }

  physical_id = command_queue->device->def.physical_device_id;
  // SVM memory is not associated with any device.
  // Migration should only be moving device global memories.
  int tmp_mem_id =
      acl_get_default_device_global_memory(command_queue->device->def);
  if (tmp_mem_id < 0) {
    ERR_RET(CL_OUT_OF_RESOURCES, command_queue->context,
            "Can not find default global memory system");
  }
  mem_id = (unsigned int)tmp_mem_id;

  // Try to reserve space for all the buffers to be moved. If we fail, we need
  // to know which buffers to deallocate:
  std::vector<bool> needs_release_on_fail(num_mem_objects, false);

  status = CL_SUCCESS;
  for (i = 0; i < num_mem_objects; ++i) {
    if (mem_objects[i]->reserved_allocations[physical_id].size() == 0) {
      acl_resize_reserved_allocations_for_device(mem_objects[i],
                                                 command_queue->device->def);
    }
    if (mem_objects[i]->reserved_allocations[physical_id][mem_id] == NULL) {
      if (!acl_reserve_buffer_block(mem_objects[i],
                                    &(acl_get_platform()->global_mem),
                                    physical_id, mem_id)) {
        status = CL_MEM_OBJECT_ALLOCATION_FAILURE;
        break;
      }
      needs_release_on_fail[i] = true;
    }
    mem_objects[i]->reserved_allocations_count[physical_id][mem_id]++;
  }

  if (status != CL_SUCCESS) {
    // We failed, release memory that was reserved:
    for (i = 0; i < num_mem_objects; ++i) {
      if (needs_release_on_fail[i]) {
        remove_mem_block_linked_list(
            mem_objects[i]->reserved_allocations[physical_id][mem_id]);
        acl_free(mem_objects[i]->reserved_allocations[physical_id][mem_id]);
        mem_objects[i]->reserved_allocations[physical_id][mem_id] = NULL;
      }
      mem_objects[i]->reserved_allocations_count[physical_id][mem_id]--;
    }
    return status;
  }

  // All space is reserved, create an event/command to actually move the data at
  // the appropriate time.
  status =
      acl_create_event(command_queue, num_events_in_wait_list, event_wait_list,
                       CL_COMMAND_MIGRATE_MEM_OBJECTS, &local_event);

  if (status != CL_SUCCESS) {
    return status; // already signalled callback
  }

  local_event->cmd.info.memory_migration.num_mem_objects = num_mem_objects;

  if (local_event->cmd.info.memory_migration.src_mem_list) {
    acl_mem_migrate_wrapper_t *new_src_mem_list =
        (acl_mem_migrate_wrapper_t *)acl_realloc(
            local_event->cmd.info.memory_migration.src_mem_list,
            num_mem_objects * sizeof(acl_mem_migrate_wrapper_t));

    if (!new_src_mem_list) {
      return CL_OUT_OF_RESOURCES;
    }

    local_event->cmd.info.memory_migration.src_mem_list = new_src_mem_list;
  } else {
    local_event->cmd.info.memory_migration.src_mem_list =
        (acl_mem_migrate_wrapper_t *)acl_malloc(
            num_mem_objects * sizeof(acl_mem_migrate_wrapper_t));

    if (!local_event->cmd.info.memory_migration.src_mem_list) {
      return CL_OUT_OF_RESOURCES;
    }
  }

  for (i = 0; i < num_mem_objects; ++i) {
    local_event->cmd.info.memory_migration.src_mem_list[i].src_mem =
        mem_objects[i];
    local_event->cmd.info.memory_migration.src_mem_list[i]
        .destination_physical_device_id = physical_id;
    local_event->cmd.info.memory_migration.src_mem_list[i].destination_mem_id =
        mem_id;

    clRetainMemObject(mem_objects[i]);
  }

  if (event) {
    *event = local_event;
  } else {
    // User didn't care, so forget about the event.
    clReleaseEvent(local_event);
    acl_idle_update(command_queue->context); // Clean up early
  }

  return CL_SUCCESS;
}

ACL_EXPORT CL_API_ENTRY cl_int CL_API_CALL clEnqueueMigrateMemObjects(
    cl_command_queue command_queue, cl_uint num_mem_objects,
    const cl_mem *mem_objects, cl_mem_migration_flags flags,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  return clEnqueueMigrateMemObjectsIntelFPGA(
      command_queue, num_mem_objects, mem_objects, flags,
      num_events_in_wait_list, event_wait_list, event);
}

/**
 * Read <size> bytes of data from device global
 *
 * @param command_queue the queue system this command will belong
 * @param program contains the device global
 * @param name name of device global, used to look up for device global address
 * in autodiscovery string
 * @param blocking_read whether the operation is blocking or not
 * @param size number of bytes to read / write
 * @param offset offset from the extracted address of device global
 * @param ptr pointer that will hold the data copied from device global
 * @param num_events_in_wait_list number of event that device global read depend
 * on
 * @param event_wait_list events that device global read depend on
 * @param event the info about the execution of device global read will be
 * stored in the event
 * @return status code, CL_SUCCESS if all operations are successful.
 */
ACL_EXPORT
CL_API_ENTRY cl_int clEnqueueReadGlobalVariableINTEL(
    cl_command_queue command_queue, cl_program program, const char *name,
    cl_bool blocking_read, size_t size, size_t offset, void *ptr,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {

  acl_print_debug_msg("Entering clEnqueueReadGlobalVariableINTEL function.\n");

  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE; // There is no context yet.
  }

  cl_int status = 0;

  // Get context from program, command_queue and event
  cl_context context = program->context;
  cl_device_id device = command_queue->device;
  unsigned int physical_device_id = device->def.physical_device_id;

  if (!acl_context_is_valid(context)) {
    return CL_INVALID_CONTEXT;
  }

  if (!acl_program_is_valid(program)) {
    ERR_RET(CL_INVALID_PROGRAM, context, "Invalid program was provided");
  }

  if (ptr == NULL) {
    ERR_RET(CL_INVALID_VALUE, context,
            "Invalid pointer was provided to host data");
  }

  if (name == NULL) {
    ERR_RET(CL_INVALID_VALUE, context, "Invalid Device Global Name");
  }

  if ((num_events_in_wait_list > 0 && !event_wait_list) ||
      (!num_events_in_wait_list && event_wait_list)) {
    ERR_RET(CL_INVALID_EVENT_WAIT_LIST, context,
            "Invalid event_wait_list or num_events_in_wait_list");
  }

  uint64_t device_global_addr;

  std::unordered_map<std::string, acl_device_global_mem_def_t> dev_global_map =
      command_queue->device->loaded_bin->get_devdef()
          .autodiscovery_def.device_global_mem_defs;

  std::unordered_map<std::string, acl_device_global_mem_def_t>::const_iterator
      dev_global = dev_global_map.find(std::string(name));

  if (dev_global != dev_global_map.end()) {
    device_global_addr = dev_global->second.address;
  } else {
    // For unit test purpose
    // Device global name not found in kernel dev_bin, try to find in the sysdef
    // setup by unit tests.
    dev_global_map = acl_present_board_def()
                         ->device[0]
                         .autodiscovery_def.device_global_mem_defs;
    dev_global = dev_global_map.find(std::string(name));
    if (dev_global != dev_global_map.end()) {
      device_global_addr = dev_global->second.address;
      return CL_SUCCESS; // This is for unit test purpose. Unit test would stop
                         // here.
    } else {
      acl_print_debug_msg("clEnqueueReadGlobalVariableINTEL Cannot find Device "
                          "Global address from the name %s\n",
                          name);
      ERR_RET(CL_INVALID_ARG_VALUE, context,
              "Cannot find Device Global address from the name");
    }
  }
  cl_event local_event = 0; // used for blocking

  // Create an event/command to actually move the data at the appropriate
  // time.
  status =
      acl_create_event(command_queue, num_events_in_wait_list, event_wait_list,
                       CL_COMMAND_READ_GLOBAL_VARIABLE_INTEL, &local_event);

  if (status != CL_SUCCESS)
    return status;

  local_event->cmd.info.device_global_info.offset = offset;
  local_event->cmd.info.device_global_info.read_ptr = ptr;
  local_event->cmd.info.device_global_info.device_global_addr =
      device_global_addr;
  local_event->cmd.info.device_global_info.name = name;
  local_event->cmd.info.device_global_info.size = size;
  local_event->cmd.info.device_global_info.physical_device_id =
      physical_device_id;
  acl_idle_update(
      command_queue
          ->context); // If nothing's blocking, then complete right away
  if (blocking_read) {
    status = clWaitForEvents(1, &local_event);
  }
  if (event) {
    *event = local_event;
  } else {
    // User didn't care, so forget about the event.
    clReleaseEvent(local_event);
    acl_idle_update(command_queue->context); // Clean up early
  }
  acl_print_debug_msg("Exiting clEnqueueReadGlobalVariableINTEL function\n");
  return CL_SUCCESS;
}

/**
 * Write <size> bytes of data from user provided host pointer into device global
 *
 * @param command_queue the queue system this command belongs
 * @param program contains device global write
 * @param name name of device global, used to look up for device global address
 * in autodiscovery string
 * @param blocking_write whether the operation is blocking or not
 * @param size number of bytes to read / write
 * @param offset offset from the extracted address of device global
 * @param ptr pointer that will hold the data copied from device global
 * @param num_events_in_wait_list number of event that device global write
 * depend on
 * @param event_wait_list events that device global read depend on
 * @param event the info about the execution of device global write will be
 * stored in the event
 * @return status code, CL_SUCCESS if all operations are successful.
 */
ACL_EXPORT
CL_API_ENTRY cl_int clEnqueueWriteGlobalVariableINTEL(
    cl_command_queue command_queue, cl_program program, const char *name,
    cl_bool blocking_write, size_t size, size_t offset, const void *ptr,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {

  acl_print_debug_msg("Entering clEnqueueWriteGlobalVariableINTEL function\n");

  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE; // There is no context yet.
  }

  cl_int status = 0;
  // Get context from program, command_queue and event
  cl_context context = program->context;
  cl_device_id device = command_queue->device;
  unsigned int physical_device_id = device->def.physical_device_id;

  if (!acl_context_is_valid(context)) {
    return CL_INVALID_CONTEXT;
  }

  if (!acl_program_is_valid(program)) {
    ERR_RET(CL_INVALID_PROGRAM, context, "Invalid program was provided");
  }

  if (ptr == NULL) {
    ERR_RET(CL_INVALID_VALUE, context,
            "Invalid pointer was provided to host data");
  }

  if (name == NULL) {
    ERR_RET(CL_INVALID_VALUE, context, "Invalid Device Global Name");
  }

  if ((num_events_in_wait_list > 0 && !event_wait_list) ||
      (!num_events_in_wait_list && event_wait_list)) {
    ERR_RET(CL_INVALID_EVENT_WAIT_LIST, context,
            "Invalid event_wait_list or num_events_in_wait_list");
  }

  uint64_t device_global_addr;

  std::unordered_map<std::string, acl_device_global_mem_def_t> dev_global_map =
      command_queue->device->loaded_bin->get_devdef()
          .autodiscovery_def.device_global_mem_defs;

  std::unordered_map<std::string, acl_device_global_mem_def_t>::const_iterator
      dev_global = dev_global_map.find(std::string(name));

  if (dev_global != dev_global_map.end()) {
    device_global_addr = dev_global->second.address;
  } else {
    // For unit test purpose
    // Device global name not found in kernel dev_bin, try to find in the sysdef
    // setup by unit tests.
    dev_global_map = acl_present_board_def()
                         ->device[0]
                         .autodiscovery_def.device_global_mem_defs;
    dev_global = dev_global_map.find(std::string(name));
    if (dev_global != dev_global_map.end()) {
      device_global_addr = dev_global->second.address;
      return CL_SUCCESS; // This is for unit test purpose. Unit test would stop
                         // here.
    } else {
      acl_print_debug_msg("clEnqueueWriteGlobalVariableINTEL Cannot find "
                          "Device Global address from the name %s\n",
                          name);
      ERR_RET(CL_INVALID_ARG_VALUE, context,
              "Cannot find Device Global address from the name");
    }
  }

  cl_event local_event = 0; // used for blocking

  // Create an event/command to actually move the data at the appropriate
  // time.
  status =
      acl_create_event(command_queue, num_events_in_wait_list, event_wait_list,
                       CL_COMMAND_WRITE_GLOBAL_VARIABLE_INTEL, &local_event);

  if (status != CL_SUCCESS)
    return status;

  local_event->cmd.info.device_global_info.offset = offset;
  local_event->cmd.info.device_global_info.write_ptr = ptr;
  local_event->cmd.info.device_global_info.device_global_addr =
      device_global_addr;
  local_event->cmd.info.device_global_info.name = name;
  local_event->cmd.info.device_global_info.size = size;
  local_event->cmd.info.device_global_info.physical_device_id =
      physical_device_id;

  acl_idle_update(
      command_queue
          ->context); // If nothing's blocking, then complete right away

  if (blocking_write) {
    status = clWaitForEvents(1, &local_event);
  }

  if (event) {
    *event = local_event;
  } else {
    // User didn't care, so forget about the event.
    clReleaseEvent(local_event);
    acl_idle_update(command_queue->context); // Clean up early
  }
  acl_print_debug_msg("Exiting clEnqueueWriteGlobalVariableINTEL function\n");
  return CL_SUCCESS;
}

//////////////////////////////
// Internals

static void l_get_working_range(const acl_block_allocation_t *block_allocation,
                                unsigned physical_device_id,
                                unsigned target_mem_id, unsigned bank_id,
                                acl_addr_range_t *working_range,
                                void **initial_try) {
  acl_assert_locked();

  if (block_allocation->region == &(acl_platform.global_mem)) {
    const auto *global_mem_defs = &(acl_platform.device[physical_device_id]
                                        .def.autodiscovery_def.global_mem_defs);

    assert(target_mem_id < acl_platform.device[physical_device_id]
                               .def.autodiscovery_def.num_global_mem_systems);
    const auto &global_mem_def = (*global_mem_defs)[target_mem_id];

    acl_addr_range_t physical_range = global_mem_def.range;
    *working_range = global_mem_def.get_usable_range();
    size_t num_banks = global_mem_def.num_global_banks;
    num_banks = num_banks ? num_banks : 1;
    // Bank size must be calculated using the physical size of the memory since
    // it's a physical property of the memory.
    size_t bank_size =
        ((size_t)physical_range.next - (size_t)physical_range.begin) /
        num_banks;

    working_range->begin =
        ACL_CREATE_DEVICE_ADDRESS(physical_device_id, working_range->begin);
    working_range->next =
        ACL_CREATE_DEVICE_ADDRESS(physical_device_id, working_range->next);
    *initial_try = working_range->begin;

    // If user requested a certain bank within global_mem, start there
    // instead of working_range.begin. If in acl_allocate_block it doesn't find
    // a free block somewhere between the requested bank and end of memory, it
    // will try again from the beginning of memory. WARNING: Nothing prevents
    // the block from straddling across two banks
    if (bank_id > 0) {
      // Bank start and end addresses are calculated using the physical_range
      // since banks correspond to the physical layout of the memory.
      *initial_try =
          ACL_CREATE_DEVICE_ADDRESS(physical_device_id, physical_range.begin);
      *initial_try = (void *)((size_t)(*initial_try) +
                              ((bank_id - 1) % num_banks) * bank_size);
      if (*initial_try < working_range->begin) {
        // Some of the first bank's memory might not be usable so initial_try
        // needs to be adjusted to the first usable memory address.
        *initial_try = working_range->begin;
      }
    }
  } else {
    *working_range = block_allocation->region->range;
    *initial_try = working_range->begin;
  }
}

// Do device allocation on target device and memory.
// Try allocating first on target DIMM, then try entire memory range.
static int acl_allocate_block(acl_block_allocation_t *block_allocation,
                              const cl_mem mem, unsigned physical_device_id,
                              unsigned target_mem_id) {
  acl_assert_locked();

  int result = 0;
  acl_addr_range_t where; // The candidate range of addresses
  char trying_preferred_bank = 0;
  char tried_preferred_bank = 0;

  size_t alloc_size = mem->size;
  // For images, need to allocate additional memory for meta data (width,
  // height, etc)
  if (is_image(mem)) {
    alloc_size +=
        get_offset_for_image_param(mem->context, mem->mem_object_type, "data");
  }

  // Find a free range inside the region.
  // Must keep it aligned, so up the alloc_size if necessary.
  if (alloc_size & (ACL_MEM_ALIGN - 1)) {
    alloc_size += ACL_MEM_ALIGN - (alloc_size & (ACL_MEM_ALIGN - 1));
  }

  acl_addr_range_t working_range = {0, 0};
  void *initial_try = NULL;

  l_get_working_range(block_allocation, physical_device_id, target_mem_id,
                      mem->bank_id, &working_range, &initial_try);

#ifdef MEM_DEBUG_MSG
  printf("acl_allocate_block size:%zx, working_range:%zx - %zx, initial "
         "try:%zx \n",
         alloc_size, (size_t)(working_range.begin),
         (size_t)(working_range.next), (size_t)initial_try);
#endif

  // Use a first-fit algorithm:  We walk the list of allocated blocks.
  // They are stored in allocated-address order.
  // In each iteration we look at the unallocated gap before the next
  // allocated block.
#define TRY_AT(P) (where.begin = (P)), (where.next = ((char *)P) + alloc_size)

  if (initial_try != working_range.begin) {
    trying_preferred_bank = 1;
  }

  TRY_AT(initial_try);

  // The candidate range could overlap an allocated block. Traverse the
  // allocation chain for this region to check that the candidate range does
  // not overlap.
  do {
    acl_block_allocation_t **block_ptr =
        &(block_allocation->region->first_block);
    acl_block_allocation_t *block = *block_ptr;
    while ((char *)working_range.next - (char *)where.next >= 0) {
      // We haven't yet run off the end of the region. Keep going.
      if (block == NULL) {
        // No allocated buffers span higher addresses.
        // So we're at the gap between the end of the last allocated
        // block and the end of the region.
        // Use this gap.
        block_allocation->range = where;
        block_allocation->next_block_in_region = NULL; // link out
        *block_ptr = block_allocation;                 // link in
        result = 1;                                    // Signal success.
        break;
      } else {
        // Look at the gap before this allocated block.
        // The first address of the gap is where.begin.
        if ((char *)block->range.begin - (char *)where.next >= 0) {
          // We found a gap big enough for the new allocation.
          // Insert it here.
          block_allocation->range = where;
          block_allocation->next_block_in_region = block; // link out
          *block_ptr = block_allocation;                  // link in
          result = 1;
          break;
        } else {
          if ((char *)block->range.next > (char *)where.begin) {
            // If user specified a preferred bank, don't try any
            // regions before its address.
            TRY_AT(block->range.next);
          }
          block_ptr = &(block->next_block_in_region);
          block = *block_ptr;
        }
      }
    }

    // If we started looking for memory in a preferred bank but didn't find
    // anything, try again searching all of global memory.
    TRY_AT(working_range.begin);
    tried_preferred_bank = trying_preferred_bank;
    trying_preferred_bank = 0;
  } while (result == 0 && tried_preferred_bank);

#ifdef MEM_DEBUG_MSG
  printf(
      "acl_allocate_block finished: result:%i, block_allocation:%zx - %zx \n",
      result, (size_t)(block_allocation->range.begin),
      (size_t)(block_allocation->range.next));
#endif
  return result;
}

void acl_resize_reserved_allocations_for_device(cl_mem mem,
                                                acl_device_def_t &def) {
  acl_assert_locked();

  unsigned int physical_device_id = def.physical_device_id;
  unsigned int num_global_mem_systems =
      def.autodiscovery_def.num_global_mem_systems;

#ifdef MEM_DEBUG_MSG
  printf(
      "resizing reserved_allocations, physical_device_id:%u, target_size:%u \n",
      physical_device_id, num_global_mem_systems);
#endif

  mem->reserved_allocations[physical_device_id].resize(num_global_mem_systems);
  mem->reserved_allocations_count[physical_device_id].resize(
      num_global_mem_systems);
}

cl_int acl_reserve_buffer_block(cl_mem mem, acl_mem_region_t *region,
                                unsigned physical_device_id,
                                unsigned target_mem_id) {
  acl_assert_locked();

#ifdef MEM_DEBUG_MSG
  printf("acl_reserve_buffer_block mem:%zx, region:%zx, physical_device_id:%u, "
         "target_mem_id:%u \n",
         (size_t)mem, (size_t)(region), physical_device_id, target_mem_id);
#endif

  acl_block_allocation_t *block_allocation = acl_new<acl_block_allocation_t>();
  if (block_allocation == nullptr)
    return -1;
  block_allocation->region = region;

  // Can't call this function if there's already a reserved block for this
  // device:
  assert(mem->reserved_allocations[physical_device_id].size() > target_mem_id);
  assert(mem->reserved_allocations[physical_device_id][target_mem_id] == NULL);
  assert(mem->reserved_allocations_count[physical_device_id][target_mem_id] ==
         0);

  int result = acl_allocate_block(block_allocation, mem, physical_device_id,
                                  target_mem_id);

  // For images, copy the additional meta data (width, height, etc) after
  // allocating
  if (result && is_image(mem)) {
    result = copy_image_metadata(mem);
  }

  if (!result) {
    acl_delete(block_allocation);
    return result;
  }

  mem->reserved_allocations[physical_device_id][target_mem_id] =
      block_allocation;
  block_allocation->mem_obj = mem;

#ifdef MEM_DEBUG_MSG
  printf("acl_reserve_buffer_block finished block_allocation:%zx, range:%zx - "
         "%zx \n",
         (size_t)block_allocation, (size_t)(block_allocation->range.begin),
         (size_t)(block_allocation->range.next));
#endif
  return result;
}

static int copy_image_metadata(cl_mem mem) {
  acl_assert_locked();
  {
    cl_int errcode;
    size_t element_size;
    const acl_hal_t *hal = acl_get_hal();

    element_size = acl_get_image_element_size(
        mem->context, mem->fields.image_objs.image_format, &errcode);
    if (errcode != CL_SUCCESS) {
      return 0;
    }

    void *local_meta_data = malloc(
        get_offset_for_image_param(mem->context, mem->mem_object_type, "data"));

    safe_memcpy((char *)local_meta_data +
                    get_offset_for_image_param(mem->context,
                                               mem->mem_object_type, "width"),
                &(mem->fields.image_objs.image_desc->image_width), 4, 4, 4);
    if (mem->mem_object_type == CL_MEM_OBJECT_IMAGE2D ||
        mem->mem_object_type == CL_MEM_OBJECT_IMAGE3D ||
        mem->mem_object_type == CL_MEM_OBJECT_IMAGE2D_ARRAY) {
      safe_memcpy((char *)local_meta_data +
                      get_offset_for_image_param(
                          mem->context, mem->mem_object_type, "height"),
                  &(mem->fields.image_objs.image_desc->image_height), 4, 4, 4);
    }
    if (mem->mem_object_type == CL_MEM_OBJECT_IMAGE3D) {
      safe_memcpy((char *)local_meta_data +
                      get_offset_for_image_param(mem->context,
                                                 mem->mem_object_type, "depth"),
                  &(mem->fields.image_objs.image_desc->image_depth), 4, 4, 4);
    }
    if (mem->mem_object_type == CL_MEM_OBJECT_IMAGE2D_ARRAY ||
        mem->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
      safe_memcpy((char *)local_meta_data +
                      get_offset_for_image_param(
                          mem->context, mem->mem_object_type, "array_size"),
                  &(mem->fields.image_objs.image_desc->image_array_size), 4, 4,
                  4);
    }
    safe_memcpy((char *)local_meta_data +
                    get_offset_for_image_param(mem->context,
                                               mem->mem_object_type,
                                               "channel_data_type"),
                &(mem->fields.image_objs.image_format->image_channel_data_type),
                4, 4, 4);
    safe_memcpy((char *)local_meta_data +
                    get_offset_for_image_param(
                        mem->context, mem->mem_object_type, "channel_order"),
                &(mem->fields.image_objs.image_format->image_channel_order), 4,
                4, 4);
    safe_memcpy((char *)local_meta_data +
                    get_offset_for_image_param(
                        mem->context, mem->mem_object_type, "element_size"),
                &(element_size), 8, 8, 8);

    hal->copy_hostmem_to_globalmem(
        0, local_meta_data, (char *)mem->block_allocation->range.begin,
        get_offset_for_image_param(mem->context, mem->mem_object_type, "data"));

    free(local_meta_data);
  }
  return 1;
}

// Remove the block from the region's allocation list.
// (Need to keep this condition lined up with the behaviour
// in clCreateBuffer.
static void remove_mem_block_linked_list(acl_block_allocation_t *block) {
  acl_block_allocation_t **region_block_ptr = &(block->region->first_block);
  while (*region_block_ptr) {
    if (*region_block_ptr == block) {
      // Remove block from linked list
      *region_block_ptr = block->next_block_in_region;
      // Break loop since the block will appear only once
      break;
    }
    // Advance to next cl_mem in the region
    region_block_ptr = &((*region_block_ptr)->next_block_in_region);
  }
}

void acl_mem_destructor_callback(cl_mem memobj) {
  acl_mem_destructor_user_callback *cb_head, *temp;
  acl_assert_locked();
  // Call the notification function registered via
  // clSetMemObjectDestructorCallback calls. All of the callbacks in the list
  // are called in the reverse order they were registered. Note we can't check
  // if acl_mem_is_valid here, since the ref. count is zero. The function
  // calling this function is responsible for passing valid mem objects.
  cb_head = memobj->destructor_callback_list;
  while (cb_head) {
    acl_mem_destructor_notify_fn_t mem_destructor_notify_fn =
        cb_head->mem_destructor_notify_fn;
    void *notify_user_data = cb_head->notify_user_data;
    // Removing that callback from the list and calling it.
    temp = cb_head;
    cb_head = cb_head->next;
    memobj->destructor_callback_list = cb_head;
    acl_free(temp);
    {
      acl_suspend_lock_guard lock{acl_mutex_wrapper};
      mem_destructor_notify_fn(memobj, notify_user_data);
    }
  }
}

int acl_mem_is_valid(cl_mem mem) {
  acl_assert_locked();
#ifdef REMOVE_VALID_CHECKS
  return 1;
#else
  if (!acl_is_valid_ptr(mem)) {
    return 0;
  }
  if (!acl_ref_count(mem)) {
    return 0;
  }
  if (!acl_context_is_valid(mem->context)) {
    return 0;
  }
  if (mem->mem_object_type == CL_MEM_OBJECT_BUFFER &&
      acl_ref_count(mem) <= mem->fields.buffer_objs.num_subbuffers) {
    return 0;
  }
  return 1;
#endif
}

// Iterate through device's global memories and return the ID of the first
// device private global memory.
// This is needed over acl_get_default_memory(dev) because device can have
// both device private and shared virtual memory.
// Returns the id of the memory that clCreateBuffer would allocate in by default
// (i.e. when CL_MEM_USE_HOST_PTR is not used) or -1 if no such memory exists.
int acl_get_default_device_global_memory(const acl_device_def_t &dev) {
  int lowest_gmem_idx = -1;
  cl_ulong lowest_gmem_begin = 0xFFFFFFFFFFFFFFFF;
  acl_assert_locked();

  // If the device has no physical memory then clCreateBuffer will fall back to
  // allocating device global memory in the default memory.
  if (!acl_svm_device_supports_physical_memory(dev.physical_device_id))
    return acl_get_default_memory(dev);
  for (unsigned gmem_idx = 0;
       gmem_idx < dev.autodiscovery_def.num_global_mem_systems; gmem_idx++) {
    if (dev.autodiscovery_def.global_mem_defs[gmem_idx].type ==
            ACL_GLOBAL_MEM_DEVICE_PRIVATE &&
        (size_t)dev.autodiscovery_def.global_mem_defs[gmem_idx].range.begin <
            lowest_gmem_begin) {
      lowest_gmem_begin =
          (size_t)dev.autodiscovery_def.global_mem_defs[gmem_idx].range.begin;
      lowest_gmem_idx = static_cast<int>(gmem_idx);
    }
  }

  // This can return -1, but that means there's no device private memory
  return lowest_gmem_idx;
}

// Memory systems are listed with the default memory at the first index
int acl_get_default_memory(const acl_device_def_t &) { return 0; }

static void *l_get_address_of_writable_copy(cl_mem mem,
                                            unsigned int physical_device_id,
                                            int *on_host_ptr,
                                            cl_bool is_dest_unmap) {
  acl_assert_locked();
  // If this is an SVM buffer and the device supports SVM then the user expects
  // us to use SVM. Use the host memory.
  if (mem->is_svm && acl_svm_device_supports_any_svm(physical_device_id)) {
    if (on_host_ptr)
      (*on_host_ptr) = 1;
    return mem->host_mem.aligned_ptr;
    // If the device only supports SVM, we have to use SVM. Use the host memory.
  } else if (!acl_svm_device_supports_physical_memory(physical_device_id)) {
    if (mem->block_allocation->region->is_host_accessible) {
      // We never automatically map host buffers to the device, so the writable
      // copy is always on the host.
      // Need to use range.begin instead of host_mem.aligned_ptr in case either
      //    (a) this buffer was created with CL_MEM_USE_HOST_PTR,
      // or (b) ACL is not using host malloc.
      if (on_host_ptr)
        (*on_host_ptr) = 1;
      return mem->block_allocation->range.begin;
    } else {
      if (on_host_ptr)
        (*on_host_ptr) = 1;
      return mem->host_mem.aligned_ptr;
    }
  } else {
    // When unmapping a mem object, always take the device address
    if (is_dest_unmap) {
      if (on_host_ptr)
        (*on_host_ptr) = 0;
      return mem->block_allocation->range.begin;
    } else {
      if (mem->block_allocation->region->is_host_accessible) {
        // We never automatically map host buffers to the device, so the
        // writable copy is always on the host. Need to use range.begin instead
        // of host_mem.aligned_ptr in case either
        //    (a) this buffer was created with CL_MEM_USE_HOST_PTR,
        // or (b) ACL is not using host malloc.
        if (on_host_ptr)
          (*on_host_ptr) = 1;
        return mem->block_allocation->range.begin;
      } else if (mem->writable_copy_on_host) {
        // It's home is on the device, but it's mapped to the host right now.
        if (on_host_ptr)
          (*on_host_ptr) = 1;
        return mem->host_mem.aligned_ptr;
      } else {
        // It's home is on the device, and it's not mapped to the host.
        if (on_host_ptr)
          (*on_host_ptr) = 0;
        return mem->block_allocation->range.begin;
      }
    }
  }
}

// clEnqueueReadBuffer and clEnqueueWriteBuffer are almost entirely the
// same.
cl_int l_enqueue_mem_transfer(cl_command_queue command_queue, cl_bool blocking,
                              cl_mem src_buffer, size_t src_offset[3],
                              size_t src_row_pitch, size_t src_slice_pitch,
                              cl_mem dst_buffer, size_t dst_offset[3],
                              size_t dst_row_pitch, size_t dst_slice_pitch,
                              size_t cb[3], cl_uint num_events,
                              const cl_event *events, cl_event *event,
                              cl_command_type type, cl_map_flags map_flags) {
  acl_assert_locked();

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  cl_context context = command_queue->context;

  if ((num_events > 0 && !events) || (!num_events && events)) {
    ERR_RET(CL_INVALID_EVENT_WAIT_LIST, context,
            "Invalid event_wait_list or num_events_in_wait_list");
  }

  if (!acl_mem_is_valid(src_buffer))
    ERR_RET(CL_INVALID_MEM_OBJECT, context, "Source buffer is invalid");
  if (!acl_mem_is_valid(dst_buffer))
    ERR_RET(CL_INVALID_MEM_OBJECT, context, "Destination buffer is invalid");

  if (command_queue->context != src_buffer->context)
    ERR_RET(CL_INVALID_CONTEXT, context,
            "Source buffer is not in the same context as the command queue");
  if (command_queue->context != dst_buffer->context)
    ERR_RET(
        CL_INVALID_CONTEXT, context,
        "Destination buffer is not in the same context as the command queue");

  if (cb[0] == 0)
    ERR_RET(CL_INVALID_VALUE, context, "Region 'x' size is zero");
  if (cb[1] == 0)
    ERR_RET(CL_INVALID_VALUE, context, "Region 'y' size is zero");
  if (cb[2] == 0)
    ERR_RET(CL_INVALID_VALUE, context, "Region 'z' size is zero");

  // Check that the requested operation is compatible with the flags the src/dst
  // buffers were created with. Checks for image related operations have not
  // been implemented.
  switch (type) {
  case CL_COMMAND_READ_BUFFER:
  case CL_COMMAND_READ_BUFFER_RECT:
    if (src_buffer->flags & CL_MEM_HOST_WRITE_ONLY ||
        src_buffer->flags & CL_MEM_HOST_NO_ACCESS) {
      ERR_RET(CL_INVALID_OPERATION, context,
              "clEnqueueReadBuffer cannot be called on a buffer "
              "created with CL_MEM_HOST_WRITE_ONLY or CL_MEM_HOST_NO_ACCESS");
    }
    break;
  case CL_COMMAND_WRITE_BUFFER:
  case CL_COMMAND_WRITE_BUFFER_RECT:
    if (dst_buffer->flags & CL_MEM_COPY_HOST_PTR &&
        dst_buffer->copy_host_ptr_skip_check) {
      // Don't block the call to clEnqueueWriteBuffer when we're initializing
      // a read-only or no-access buffer created with CL_MEM_COPY_HOST_PTR since
      // it's an internal usage of the API.
      dst_buffer->copy_host_ptr_skip_check = CL_FALSE;
      break;
    }

    if (dst_buffer->flags & CL_MEM_HOST_READ_ONLY ||
        dst_buffer->flags & CL_MEM_HOST_NO_ACCESS) {
      ERR_RET(CL_INVALID_OPERATION, context,
              "clEnqueueWriteBuffer cannot be called on a buffer "
              "created with CL_MEM_HOST_READ_ONLY or CL_MEM_HOST_NO_ACCESS");
    }
    break;
  case CL_COMMAND_COPY_BUFFER:
  case CL_COMMAND_COPY_BUFFER_RECT:
  case CL_COMMAND_FILL_BUFFER:
  case CL_COMMAND_UNMAP_MEM_OBJECT:
    break;
  case CL_COMMAND_MAP_BUFFER:
    if ((src_buffer->flags & CL_MEM_HOST_WRITE_ONLY ||
         src_buffer->flags & CL_MEM_HOST_NO_ACCESS) &&
        map_flags & CL_MAP_READ) {
      ERR_RET(
          CL_INVALID_OPERATION, context,
          "clEnqeueueMapBuffer with CL_MAP_READ cannot be called on a buffer "
          "created with CL_MEM_HOST_WRITE_ONLY or CL_MEM_HOST_NO_ACCESS");
    }

    if ((src_buffer->flags & CL_MEM_HOST_READ_ONLY ||
         src_buffer->flags & CL_MEM_HOST_NO_ACCESS) &&
        (map_flags & CL_MAP_WRITE ||
         map_flags & CL_MAP_WRITE_INVALIDATE_REGION)) {
      ERR_RET(CL_INVALID_OPERATION, context,
              "clEnqeueueMapBuffer with CL_MAP_WRITE or "
              "CL_MAP_WRITE_INVALIDATE_REGION "
              "cannot be called on a buffer created with CL_MEM_HOST_READ_ONLY "
              "or CL_MEM_HOST_NO_ACCESS");
    }
    break;
  case CL_COMMAND_READ_IMAGE:
  case CL_COMMAND_WRITE_IMAGE:
  case CL_COMMAND_COPY_IMAGE:
  case CL_COMMAND_COPY_IMAGE_TO_BUFFER:
  case CL_COMMAND_COPY_BUFFER_TO_IMAGE:
  case CL_COMMAND_MAP_IMAGE:
  case CL_COMMAND_FILL_IMAGE:
#ifndef ACL_SUPPORT_IMAGES
    ERR_RET(CL_INVALID_OPERATION, context, "Device does not support images");
#endif
    break;
  default:
    assert(0 && "Command is not a memory transfer related command type");
  }

  cl_int errcode_ret;
  size_t src_element_size;

  // Check that the copy area of the source buffer is within the allocated
  // memory
  switch (src_buffer->mem_object_type) {
    size_t default_slice_pitch;
  case CL_MEM_OBJECT_BUFFER:
    if ((src_offset[2] + cb[2] - 1) * src_slice_pitch +
            (src_offset[1] + cb[1] - 1) * src_row_pitch + src_offset[0] +
            cb[0] >
        src_buffer->size)
      ERR_RET(
          CL_INVALID_VALUE, context,
          "Source buffer offset plus byte count exceeds source buffer size");
    if (src_row_pitch != 0 && src_row_pitch < cb[0])
      ERR_RET(CL_INVALID_VALUE, context,
              "Source buffer row pitch is less than region 'x' size");

    if (dst_buffer->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
      default_slice_pitch = src_row_pitch;
    } else {
      default_slice_pitch = cb[1] * src_row_pitch;
    }
    if (src_slice_pitch != 0 && src_slice_pitch < default_slice_pitch) {
      ERR_RET(
          CL_INVALID_VALUE, context,
          "Source buffer slice pitch is less than region 'y' size * row pitch");
    }
    break;
  case CL_MEM_OBJECT_IMAGE1D:
  case CL_MEM_OBJECT_IMAGE1D_BUFFER:
    src_element_size = acl_get_image_element_size(
        src_buffer->context, src_buffer->fields.image_objs.image_format,
        &errcode_ret);
    if (errcode_ret != CL_SUCCESS) {
      return errcode_ret;
    }
    if (src_offset[0] + cb[0] >
        src_buffer->fields.image_objs.image_desc->image_width *
            src_element_size)
      ERR_RET(CL_INVALID_VALUE, context,
              "Source image offset plus region exceeds image width");
    if (src_offset[1] != 0 || src_offset[2] != 0)
      ERR_RET(CL_INVALID_VALUE, context,
              "Non-zero offset specified for invalid index in source image");
    if (cb[1] != 1 || cb[2] != 1)
      ERR_RET(CL_INVALID_VALUE, context,
              "Non-one region specified for invalid index in source image");
    break;
  case CL_MEM_OBJECT_IMAGE1D_ARRAY:
    src_element_size = acl_get_image_element_size(
        src_buffer->context, src_buffer->fields.image_objs.image_format,
        &errcode_ret);
    if (errcode_ret != CL_SUCCESS) {
      return errcode_ret;
    }
    if (src_offset[0] + cb[0] >
        src_buffer->fields.image_objs.image_desc->image_width *
            src_element_size)
      ERR_RET(CL_INVALID_VALUE, context,
              "Source image offset plus region exceeds image width");
    if (src_offset[1] + cb[1] >
        src_buffer->fields.image_objs.image_desc->image_array_size)
      ERR_RET(CL_INVALID_VALUE, context,
              "Source image offset plus region exceeds image array size");
    if (src_offset[2] != 0)
      ERR_RET(CL_INVALID_VALUE, context,
              "Non-zero offset specified for invalid index in source image");
    if (cb[2] != 1)
      ERR_RET(CL_INVALID_VALUE, context,
              "Non-one region specified for invalid index in source image");
    break;
  case CL_MEM_OBJECT_IMAGE2D:
    src_element_size = acl_get_image_element_size(
        src_buffer->context, src_buffer->fields.image_objs.image_format,
        &errcode_ret);
    if (errcode_ret != CL_SUCCESS) {
      return errcode_ret;
    }
    if (src_offset[0] + cb[0] >
        src_buffer->fields.image_objs.image_desc->image_width *
            src_element_size)
      ERR_RET(CL_INVALID_VALUE, context,
              "Source image offset plus region exceeds image width");
    if (src_offset[1] + cb[1] >
        src_buffer->fields.image_objs.image_desc->image_height)
      ERR_RET(CL_INVALID_VALUE, context,
              "Source image offset plus region exceeds image height");
    if (src_offset[2] != 0)
      ERR_RET(CL_INVALID_VALUE, context,
              "Non-zero offset specified for invalid index in source image");
    if (cb[2] != 1)
      ERR_RET(CL_INVALID_VALUE, context,
              "Non-one region specified for invalid index in source image");
    break;
  case CL_MEM_OBJECT_IMAGE2D_ARRAY:
    src_element_size = acl_get_image_element_size(
        src_buffer->context, src_buffer->fields.image_objs.image_format,
        &errcode_ret);
    if (errcode_ret != CL_SUCCESS) {
      return errcode_ret;
    }
    if (src_offset[0] + cb[0] >
        src_buffer->fields.image_objs.image_desc->image_width *
            src_element_size)
      ERR_RET(CL_INVALID_VALUE, context,
              "Source image offset plus region exceeds image width");
    if (src_offset[1] + cb[1] >
        src_buffer->fields.image_objs.image_desc->image_height)
      ERR_RET(CL_INVALID_VALUE, context,
              "Source image offset plus region exceeds image height");
    if (src_offset[2] + cb[2] >
        src_buffer->fields.image_objs.image_desc->image_array_size)
      ERR_RET(CL_INVALID_VALUE, context,
              "Source image offset plus region exceeds image array size");
    break;
  case CL_MEM_OBJECT_IMAGE3D:
    src_element_size = acl_get_image_element_size(
        src_buffer->context, src_buffer->fields.image_objs.image_format,
        &errcode_ret);
    if (errcode_ret != CL_SUCCESS) {
      return errcode_ret;
    }
    if (src_offset[0] + cb[0] >
        src_buffer->fields.image_objs.image_desc->image_width *
            src_element_size)
      ERR_RET(CL_INVALID_VALUE, context,
              "Source image offset plus region exceeds image width");
    if (src_offset[1] + cb[1] >
        src_buffer->fields.image_objs.image_desc->image_height)
      ERR_RET(CL_INVALID_VALUE, context,
              "Source image offset plus region exceeds image height");
    if (src_offset[2] + cb[2] >
        src_buffer->fields.image_objs.image_desc->image_depth)
      ERR_RET(CL_INVALID_VALUE, context,
              "Source image offset plus region exceeds image depth");
    break;
  default:
    std::stringstream ss;
    ss << "Memory transfers of source mem object type not supported 0x"
       << std::hex << src_buffer->mem_object_type;
    ERR_RET(CL_INVALID_VALUE, context, ss.str().c_str());
    break;
  }

  size_t dst_element_size;

  // Check that the copy area of the destination buffer is within the allocated
  // memory
  switch (dst_buffer->mem_object_type) {
  case CL_MEM_OBJECT_BUFFER:
    if ((dst_offset[2] + cb[2] - 1) * dst_slice_pitch +
            (dst_offset[1] + cb[1] - 1) * dst_row_pitch + dst_offset[0] +
            cb[0] >
        dst_buffer->size)
      ERR_RET(CL_INVALID_VALUE, context,
              "Destination buffer offset plus byte count exceeds destination "
              "buffer size");
    if (dst_row_pitch != 0 && dst_row_pitch < cb[0])
      ERR_RET(CL_INVALID_VALUE, context,
              "Destination buffer row pitch is less than region 'x' size");
    if (dst_slice_pitch != 0 && dst_slice_pitch < cb[1] * dst_row_pitch)
      ERR_RET(CL_INVALID_VALUE, context,
              "Destination buffer slice pitch is less than region 'y' size * "
              "row pitch");
    break;
  case CL_MEM_OBJECT_IMAGE1D:
  case CL_MEM_OBJECT_IMAGE1D_BUFFER:
    dst_element_size = acl_get_image_element_size(
        dst_buffer->context, dst_buffer->fields.image_objs.image_format,
        &errcode_ret);
    if (errcode_ret != CL_SUCCESS) {
      return errcode_ret;
    }
    if (dst_offset[0] + cb[0] >
        dst_buffer->fields.image_objs.image_desc->image_width *
            dst_element_size)
      ERR_RET(CL_INVALID_VALUE, context,
              "Destination image offset plus region exceeds image width");
    if (dst_offset[1] != 0 || dst_offset[2] != 0)
      ERR_RET(
          CL_INVALID_VALUE, context,
          "Non-zero offset specified for invalid index in destination image");
    if (cb[1] != 1 || cb[2] != 1)
      ERR_RET(
          CL_INVALID_VALUE, context,
          "Non-one region specified for invalid index in destination image");
    break;
  case CL_MEM_OBJECT_IMAGE1D_ARRAY:
    dst_element_size = acl_get_image_element_size(
        dst_buffer->context, dst_buffer->fields.image_objs.image_format,
        &errcode_ret);
    if (errcode_ret != CL_SUCCESS) {
      return errcode_ret;
    }
    if (dst_offset[0] + cb[0] >
        dst_buffer->fields.image_objs.image_desc->image_width *
            dst_element_size)
      ERR_RET(CL_INVALID_VALUE, context,
              "Destination image offset plus region exceeds image width");
    if (dst_offset[1] + cb[1] >
        dst_buffer->fields.image_objs.image_desc->image_array_size)
      ERR_RET(CL_INVALID_VALUE, context,
              "Destination image offset plus region exceeds image array size");
    if (dst_offset[2] != 0)
      ERR_RET(
          CL_INVALID_VALUE, context,
          "Non-zero offset specified for invalid index in destination image");
    if (cb[2] != 1)
      ERR_RET(
          CL_INVALID_VALUE, context,
          "Non-one region specified for invalid index in destination image");
    break;
  case CL_MEM_OBJECT_IMAGE2D:
    dst_element_size = acl_get_image_element_size(
        dst_buffer->context, dst_buffer->fields.image_objs.image_format,
        &errcode_ret);
    if (errcode_ret != CL_SUCCESS) {
      return errcode_ret;
    }
    if (dst_offset[0] + cb[0] >
        dst_buffer->fields.image_objs.image_desc->image_width *
            dst_element_size)
      ERR_RET(CL_INVALID_VALUE, context,
              "Destination image offset plus region exceeds image width");
    if (dst_offset[1] + cb[1] >
        dst_buffer->fields.image_objs.image_desc->image_height)
      ERR_RET(CL_INVALID_VALUE, context,
              "Destination image offset plus region exceeds image height");
    if (dst_offset[2] != 0)
      ERR_RET(
          CL_INVALID_VALUE, context,
          "Non-zero offset specified for invalid index in destination image");
    if (cb[2] != 1)
      ERR_RET(
          CL_INVALID_VALUE, context,
          "Non-one region specified for invalid index in destination image");
    break;
  case CL_MEM_OBJECT_IMAGE2D_ARRAY:
    dst_element_size = acl_get_image_element_size(
        dst_buffer->context, dst_buffer->fields.image_objs.image_format,
        &errcode_ret);
    if (errcode_ret != CL_SUCCESS) {
      return errcode_ret;
    }
    if (dst_offset[0] + cb[0] >
        dst_buffer->fields.image_objs.image_desc->image_width *
            dst_element_size)
      ERR_RET(CL_INVALID_VALUE, context,
              "Destination image offset plus region exceeds image width");
    if (dst_offset[1] + cb[1] >
        dst_buffer->fields.image_objs.image_desc->image_height)
      ERR_RET(CL_INVALID_VALUE, context,
              "Destination image offset plus region exceeds image height");
    if (dst_offset[2] + cb[2] >
        dst_buffer->fields.image_objs.image_desc->image_array_size)
      ERR_RET(CL_INVALID_VALUE, context,
              "Destination image offset plus region exceeds image array size");
    break;
  case CL_MEM_OBJECT_IMAGE3D:
    dst_element_size = acl_get_image_element_size(
        dst_buffer->context, dst_buffer->fields.image_objs.image_format,
        &errcode_ret);
    if (errcode_ret != CL_SUCCESS) {
      return errcode_ret;
    }
    if (dst_offset[0] + cb[0] >
        dst_buffer->fields.image_objs.image_desc->image_width *
            dst_element_size) {
      ERR_RET(CL_INVALID_VALUE, context,
              "Destination image offset plus region exceeds image width");
    }
    if (dst_offset[1] + cb[1] >
        dst_buffer->fields.image_objs.image_desc->image_height) {
      ERR_RET(CL_INVALID_VALUE, context,
              "Destination image offset plus region exceeds image height");
    }
    if (dst_offset[2] + cb[2] >
        dst_buffer->fields.image_objs.image_desc->image_depth) {
      ERR_RET(CL_INVALID_VALUE, context,
              "Destination image offset plus region exceeds image depth");
    }
    break;
  default:
    std::stringstream ss;
    ss << "Memory transfers of destination mem object type not supported 0x"
       << std::hex << dst_buffer->mem_object_type;
    ERR_RET(CL_INVALID_VALUE, context, ss.str().c_str());
    break;
  }

  cl_event local_event = 0; // used for blocking

  // Create an event/command to actually move the data at the appropriate
  // time.
  cl_int status =
      acl_create_event(command_queue, num_events, events, type, &local_event);
  if (status != CL_SUCCESS)
    return status; // already signalled callback
  local_event->cmd.info.mem_xfer.src_mem = src_buffer;
  clRetainMemObject(src_buffer);
  local_event->cmd.info.mem_xfer.dst_mem = dst_buffer;
  clRetainMemObject(dst_buffer);
  for (size_t i = 0; i < 3; ++i) {
    local_event->cmd.info.mem_xfer.src_offset[i] = src_offset[i];
    local_event->cmd.info.mem_xfer.dst_offset[i] = dst_offset[i];
    local_event->cmd.info.mem_xfer.cb[i] = cb[i];
  }
  local_event->cmd.info.mem_xfer.src_row_pitch = src_row_pitch;
  local_event->cmd.info.mem_xfer.src_slice_pitch = src_slice_pitch;
  local_event->cmd.info.mem_xfer.dst_row_pitch = dst_row_pitch;
  local_event->cmd.info.mem_xfer.dst_slice_pitch = dst_slice_pitch;
  local_event->cmd.info.mem_xfer.map_flags = map_flags;
  local_event->cmd.info.mem_xfer.is_auto_map = 0;

  acl_idle_update(
      command_queue
          ->context); // If nothing's blocking, then complete right away

  if (blocking) {
    status = clWaitForEvents(1, &local_event);
  }

  if (event) {
    *event = local_event;
  } else {
    // User didn't care, so forget about the event.
    clReleaseEvent(local_event);
    acl_idle_update(command_queue->context); // Clean up early
  }

  if (blocking && status == CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST)
    return status;

  return CL_SUCCESS;
}

void acl_forcibly_release_all_memory_for_context(cl_context context) {
  acl_assert_locked();
  acl_forcibly_release_all_memory_for_context_in_region(
      context, &acl_platform.host_auto_mem);
  acl_forcibly_release_all_memory_for_context_in_region(
      context, &acl_platform.host_user_mem);
  acl_forcibly_release_all_memory_for_context_in_region(context,
                                                        context->global_mem);
}

static void acl_forcibly_release_all_memory_for_context_in_region(
    cl_context context, acl_mem_region_t *region) {
  // Remove all memory blocks associated with the given context from the
  // given region.

  acl_block_allocation_t **block_ptr = &(region->first_block);
  acl_assert_locked();

  while (*block_ptr) {
    acl_block_allocation_t *block = *block_ptr;
    // Calling the user registered callbacks for when memory is destroyed,
    // before freeing resources.
    acl_mem_destructor_callback(block->mem_obj);

    if (block->mem_obj->context == context) {
      // This mem is associated with the given context.

      // Change pointer to instead point to the next memory in order to
      // remove this memory from the linked list
      *block_ptr = block->next_block_in_region;

      // Avoid memory leak of host mallocs.
      if (block->mem_obj->host_mem.raw) {
        acl_mem_aligned_free(block->mem_obj->context,
                             &block->mem_obj->host_mem);
      }

      acl_free_cl_mem(block->mem_obj);

    } else {
      // Advance to next block on the allocation list.
      block_ptr = &(block->next_block_in_region);
    }
  }
}

//////////////////////////////
// Internals -- Command completion

acl_aligned_ptr_t acl_mem_aligned_malloc(size_t size) {
  acl_aligned_ptr_t result;

  // Use malloc
  // Allocate something slightly larger to ensure alignment.
  size_t offset;
  const size_t alloc_size = size + ACL_MEM_ALIGN;

  acl_assert_locked();

  result.size = size;
  result.alignment = ACL_MEM_ALIGN;
  if (alloc_size < size) // watch for wraparound!
    return result;
  result.raw = acl_malloc(alloc_size);

  if (result.raw == 0)
    return result;
  offset = ((uintptr_t)(char *)result.raw) & (ACL_MEM_ALIGN - 1);
  if (offset)
    offset = ACL_MEM_ALIGN - offset;
  result.aligned_ptr = (void *)(((char *)result.raw) + offset);
  result.size = alloc_size;
  return result;
}

void acl_mem_aligned_free(cl_context context, acl_aligned_ptr_t *ptr) {
  acl_assert_locked();

  if (ptr->device_addr != 0L) {
    acl_get_hal()->legacy_shared_free(context, ptr->raw, ptr->size);
  } else {
    acl_free(ptr->raw);
  }
  *ptr = acl_aligned_ptr_t();
}

// Map a buffer into host memory.
// Return 1 if we made forward progress, 0 otherwise.
int acl_mem_map_buffer(cl_event event) {
  int result = 0;
  acl_assert_locked();

  if (event->cmd.trivial) {
    // The buffer is defined to always be host accessible.
    // So just count the mappings.
    cl_mem mem = event->cmd.info.trivial_mem_mapping.mem;
    acl_set_execution_status(event, CL_SUBMITTED);
    acl_set_execution_status(event, CL_RUNNING);
    mem->mapping_count++;
    acl_set_execution_status(event, CL_COMPLETE);
    acl_print_debug_msg("mem[%p] map trivial. refcount %u\n", mem,
                        acl_ref_count(mem));
    result = 1;
  } else {
    // Otherwise we might have to move data.
    result = acl_submit_mem_transfer_device_op(event);
  }
  return result;
}

// Unmap a buffer from host memory.
// Return 1 if we made forward progress, 0 otherwise.
int acl_mem_unmap_mem_object(cl_event event) {
  int result = 0;
  acl_assert_locked();

  if (event->cmd.trivial) {
    // We only grant the Map request if the buffer is already in the host
    // address space.
    // So just count the mappings.
    cl_mem mem = event->cmd.info.trivial_mem_mapping.mem;
    acl_set_execution_status(event, CL_SUBMITTED);
    acl_set_execution_status(event, CL_RUNNING);
    mem->mapping_count--;
    acl_print_debug_msg("mem[%p] unmap trivial ->refcount %u\n", mem,
                        acl_ref_count(mem));
    acl_set_execution_status(event, CL_COMPLETE);
    result = 1;
  } else {
    // Otherwise we might have to move data.
    result = acl_submit_mem_transfer_device_op(event);
  }
  return result;
}

// Submit an op to the device op queue to copy memory.
// Return 1 if we made forward progress, 0 otherwise.
int acl_submit_mem_transfer_device_op(cl_event event) {
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
  int src_on_host;
  int dst_on_host;

  // Precautionary, but it also nudges the device scheduler to try
  // to free up old operation slots.
  acl_forget_proposed_device_ops(doq);

  // Figure out where the memory is and going
  // Similar check is done in l_mem_transfer_buffer_explicitly
  l_get_address_of_writable_copy(
      event->cmd.info.mem_xfer.src_mem,
      event->command_queue->device->def.physical_device_id, &src_on_host,
      CL_FALSE);
  l_get_address_of_writable_copy(
      event->cmd.info.mem_xfer.dst_mem,
      event->command_queue->device->def.physical_device_id, &dst_on_host,
      (cl_bool)(event->cmd.type == CL_COMMAND_UNMAP_MEM_OBJECT));
  if (src_on_host) {
    if (dst_on_host) {
      last_op =
          acl_propose_device_op(doq, ACL_DEVICE_OP_MEM_TRANSFER_COPY, event);
    } else {
      last_op =
          acl_propose_device_op(doq, ACL_DEVICE_OP_MEM_TRANSFER_WRITE, event);
    }
  } else {
    if (dst_on_host) {
      last_op =
          acl_propose_device_op(doq, ACL_DEVICE_OP_MEM_TRANSFER_READ, event);
    } else {
      last_op =
          acl_propose_device_op(doq, ACL_DEVICE_OP_MEM_TRANSFER_COPY, event);
    }
  }

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

int acl_is_device_accessible(cl_mem mem) {
  if (mem->mem_object_type == CL_MEM_OBJECT_PIPE) {
    return 0;
  }

#if defined(ACL_HOST_MEMORY_SHARED)
  if (mem->block_allocation->region->is_user_provided) {
    // even though the memory host_ptr points to is physically accessible
    // to device, it's not usable because it was probably allocated with
    // malloc(), hence it's not paged and not physically contiguous.
    return 0;
  }
#endif

  return mem->block_allocation->region->is_device_accessible;
}

ACL_EXPORT
void acl_mem_transfer_buffer(void *user_data, acl_device_op_t *op) {
  cl_event event = op->info.event;
  acl_assert_locked();

  user_data = user_data; // Only used by the test mock.
  if (acl_event_is_valid(event) &&
      acl_command_queue_is_valid(event->command_queue)) {
    cl_context context = event->command_queue->context;
    l_mem_transfer_buffer_explicitly(
        context, op, event->command_queue->device->def.physical_device_id,
        event->cmd);
  } else {
    acl_set_device_op_execution_status(op, -1);
  }
}

ACL_EXPORT
void acl_mem_migrate_buffer(void *user_data, acl_device_op_t *op) {
#ifdef MEM_DEBUG_MSG
  printf("acl_mem_migrate_buffer\n");
#endif

  cl_event event = op->info.event;

  user_data = user_data; // Disable compile warnings.

  acl_set_device_op_execution_status(op, CL_SUBMITTED);
  acl_set_device_op_execution_status(op, CL_RUNNING);

  if (!acl_event_is_valid(event) ||
      !acl_command_queue_is_valid(event->command_queue)) {
    acl_set_device_op_execution_status(op, -1);
  }
  acl_mem_migrate_t memory_migration;
  unsigned int physical_id =
      event->command_queue->device->def.physical_device_id;
  cl_bool device_supports_any_svm =
      acl_svm_device_supports_any_svm(physical_id);
  cl_bool device_supports_physical_memory =
      acl_svm_device_supports_physical_memory(physical_id);
  unsigned int index = op->info.index;

  if (event->cmd.type == CL_COMMAND_MIGRATE_MEM_OBJECTS) {
    memory_migration = event->cmd.info.memory_migration;
  } else {
    memory_migration = event->cmd.info.ndrange_kernel.memory_migration;
  }

  assert(index < memory_migration.num_mem_objects);

  {
    const cl_mem src_mem = memory_migration.src_mem_list[index].src_mem;
    const unsigned int dest_device =
        memory_migration.src_mem_list[index].destination_physical_device_id;
    const unsigned int dest_mem_id =
        memory_migration.src_mem_list[index].destination_mem_id;

#ifdef MEM_DEBUG_MSG
    printf("object %d, mem %zx, count %d:\n", index, (size_t)src_mem,
           src_mem->reserved_allocations_count[dest_device][dest_mem_id]);
#endif

    assert(src_mem->reserved_allocations[dest_device].size() > dest_mem_id);
    assert(src_mem->reserved_allocations[dest_device][dest_mem_id] != NULL);

    // Handle deferred allocations first, as it's a special case
    // If the allocation was deferred then just update the block allocation.
    if (src_mem->allocation_deferred) {
#ifdef MEM_DEBUG_MSG
      printf("allocation was deferred\n");
#endif

      // The empty block allocation that src_mem started with must be freed
      // (since it isn't in the reserved_allocations list):
      acl_delete(src_mem->block_allocation);

      src_mem->block_allocation =
          src_mem->reserved_allocations[dest_device][dest_mem_id];
      src_mem->mem_id = dest_mem_id;
      src_mem->allocation_deferred = 0;
      // Reserved allocations count is decremented below in the "already at
      // destination" branch
    }

    // If the src_mem is auto_mapped we must first unmap it
    // Note: This guarantees we have the live copy when dealing with sub or
    // parent buffers. If we already had the live copy, then auto_mapped would
    // be false. If we didn't, the auto_unmap_mem command, once complete, will
    // ensure that this copy is the live copy.
    if (src_mem->auto_mapped) {
#ifdef MEM_DEBUG_MSG
      printf("(auto mapped) ");
#endif

      auto_unmap_mem(event->command_queue->context, physical_id, src_mem, NULL);
    }

    // Do nothing for SVM:
    if ((src_mem->is_svm && device_supports_any_svm) ||
        (!device_supports_physical_memory)) {
#ifdef MEM_DEBUG_MSG
      printf("svm\n");
#endif
      acl_set_device_op_execution_status(
          op, CL_COMPLETE); // There will be no mem transfer, so we must set the
                            // op status ourselves

      // If memory is already at the destination, do nothing unless there's a
      // pending copy from the host:
    } else if (src_mem->block_allocation ==
               src_mem->reserved_allocations[dest_device][dest_mem_id]) {
#ifdef MEM_DEBUG_MSG
      printf("already at dest\n");
#endif

      if (src_mem->mem_cpy_host_ptr_pending == 1) {
// A host->device copy was deferred at some point and is still pending
#ifdef MEM_DEBUG_MSG
        printf("copy host pointer pending\n");
#endif
        const acl_hal_t *const hal = acl_get_hal();
        void *host_mem_address = src_mem->host_mem.aligned_ptr;
        void *device_mem_address = src_mem->block_allocation->range.begin;

        // Do a blocking copy. Before, we had a non-blocking copy and
        // that caused a bug.
        hal->copy_hostmem_to_globalmem(0 /* blocking */, host_mem_address,
                                       device_mem_address, src_mem->size);

        // The copy to the device is done, so it is no longer pending (i.e.
        // don't enter this if-block again) and the writable copy is no
        // longer on the host, it is on the device.
        src_mem->mem_cpy_host_ptr_pending = 0;
        src_mem->writable_copy_on_host = 0;

// We did a blocking copy, so this operation is done!
#ifdef MEM_DEBUG_MSG
        printf("Done hostmem->globalmem copy, setting status to complete\n");
#endif
        acl_set_device_op_execution_status(op, CL_COMPLETE);
      } else {
// We can fall into this else-block if we are trying to do a
// deferred migration, again. This time around there is no migration
// to do, so mark the operation as complete.
#ifdef MEM_DEBUG_MSG
        printf("Memory op is already complete\n");
#endif
        acl_set_device_op_execution_status(op, CL_COMPLETE);
      }

      src_mem->reserved_allocations_count[dest_device][dest_mem_id]--;
      // Otherwise the memory needs to be moved:
    } else {
#ifdef MEM_DEBUG_MSG
      printf("moving ");
#endif
      if (src_mem->block_allocation->region != NULL) {
        bool is_user_provided_flag =
            src_mem->block_allocation->region
                ->is_user_provided; // This flag is used for guarding some of
                                    // the code and will be changed in the
                                    // middle

        const unsigned int src_device =
            ACL_GET_PHYSICAL_ID(src_mem->block_allocation->range.begin);
        const unsigned int src_mem_id = src_mem->mem_id;

        if (!is_user_provided_flag) {
          assert(src_device != dest_device ||
                 src_mem_id !=
                     dest_mem_id); // We shouldn't get here if the source and
                                   // destination are the same place
        }

        {
          int mem_on_host;
          void *old_mem_address = l_get_address_of_writable_copy(
              src_mem, physical_id, &mem_on_host, CL_FALSE);
          void *new_mem_address =
              src_mem->reserved_allocations[dest_device][dest_mem_id]
                  ->range.begin;
          const acl_hal_t *const hal = acl_get_hal();

#ifdef MEM_DEBUG_MSG
          printf("from %u:%u:%zx to %u:%u:%zx ", src_device, src_mem_id,
                 (size_t)(ACL_STRIP_PHYSICAL_ID(old_mem_address)), dest_device,
                 dest_mem_id, (size_t)(ACL_STRIP_PHYSICAL_ID(new_mem_address)));
#endif

          if (!is_user_provided_flag && !mem_on_host) {

            // Assert that the memory is actually at its source:
            assert(src_mem->reserved_allocations[src_device].size() >
                   src_mem_id);
            assert(src_mem->reserved_allocations[src_device][src_mem_id] !=
                   NULL);
            assert(src_mem->reserved_allocations[src_device][src_mem_id] ==
                   src_mem->block_allocation);
          }

          src_mem->reserved_allocations_count
              [dest_device]
              [dest_mem_id]--; // this reserved allocation has been used
          src_mem->block_allocation =
              src_mem->reserved_allocations[dest_device][dest_mem_id];
          src_mem->mem_id = dest_mem_id;

          if (!is_user_provided_flag) {
            if (!mem_on_host) {
              // Transfer the memory:
              hal->copy_globalmem_to_globalmem(event, old_mem_address,
                                               new_mem_address, src_mem->size);
            } else {
#ifdef MEM_DEBUG_MSG
              printf("(on host) ");
#endif
              hal->copy_hostmem_to_globalmem(event, old_mem_address,
                                             new_mem_address, src_mem->size);
            }
            if (src_mem->reserved_allocations_count[src_device][src_mem_id] <=
                0) { //"<=" instead of "==" just in case

#ifdef MEM_DEBUG_MSG
              printf("release block %zx (%u:%u) ",
                     (size_t)(src_mem->reserved_allocations[src_device]
                                                           [src_mem_id]),
                     src_device, src_mem_id);
#endif
              remove_mem_block_linked_list(
                  src_mem->reserved_allocations[src_device][src_mem_id]);
              acl_delete(src_mem->reserved_allocations[src_device][src_mem_id]);
              src_mem->reserved_allocations[src_device][src_mem_id] = NULL;
            }
          } else {
#ifdef MEM_DEBUG_MSG
            printf("\nhostmem->globalmem copy with user pointer\n");
#endif
            // POTENTIAL RACE CONDITION
            // 'copy_hostmem_to_globalmem' is non-blocking if event != 0.
            // What happens if another migration is performed on this same
            // memory in the (near) future? E.g. if two kernels share the
            // same pointer created with CL_MEM_USE_HOST_PTR. Based on my
            // debugging, if a second migration happens on this memory, we
            // go into the previous else-if block (i.e. the memory is
            // already at the destination) and since the copy is not
            // pending (we just did it) the operation gets marked as
            // CL_COMPLETE. This may be a race condition!
            hal->copy_hostmem_to_globalmem(event, old_mem_address,
                                           new_mem_address, src_mem->size);
            src_mem->writable_copy_on_host = 0;
          }
          // If nobody else has reserved the region we just moved from we can
          // free it:
        }
      } else {
// Bad event
#ifdef MEM_DEBUG_MSG
        printf("\t acl_mem_migrate_buffer: bad event");
#endif
        acl_set_device_op_execution_status(op, -1);
      }
#ifdef MEM_DEBUG_MSG
      printf("\n");
#endif
    }
  }

#ifdef MEM_DEBUG_MSG
  printf("acl_mem_migrate_buffer finished\n");
#endif
}

static void auto_unmap_mem(cl_context context, unsigned int physical_id,
                           cl_mem src_mem, acl_device_op_t *op) {
  acl_command_info_t dst_unmap_cmd;
  dst_unmap_cmd.type = CL_COMMAND_UNMAP_MEM_OBJECT;
  dst_unmap_cmd.info.mem_xfer.is_auto_map = 1;
  if (src_mem->flags & CL_MEM_READ_ONLY) {
    dst_unmap_cmd.info.mem_xfer.map_flags = CL_MAP_READ;
  } else {
    dst_unmap_cmd.info.mem_xfer.map_flags = CL_MAP_WRITE;
  }
  dst_unmap_cmd.info.mem_xfer.src_mem = context->unwrapped_host_mem;
  dst_unmap_cmd.info.mem_xfer.src_offset[0] =
      (size_t)((char *)src_mem->host_mem.aligned_ptr - (char *)ACL_MEM_ALIGN);
  dst_unmap_cmd.info.mem_xfer.src_offset[1] = 0;
  dst_unmap_cmd.info.mem_xfer.src_offset[2] = 0;
  dst_unmap_cmd.info.mem_xfer.dst_mem = src_mem;
  dst_unmap_cmd.info.mem_xfer.dst_offset[0] = 0;
  dst_unmap_cmd.info.mem_xfer.dst_offset[1] = 0;
  dst_unmap_cmd.info.mem_xfer.dst_offset[2] = 0;
  dst_unmap_cmd.info.mem_xfer.cb[0] = src_mem->size;
  dst_unmap_cmd.info.mem_xfer.cb[1] = 1;
  dst_unmap_cmd.info.mem_xfer.cb[2] = 1;
  dst_unmap_cmd.info.mem_xfer.src_row_pitch = dst_unmap_cmd.info.mem_xfer.cb[0];
  dst_unmap_cmd.info.mem_xfer.src_slice_pitch = 1;
  dst_unmap_cmd.info.mem_xfer.dst_row_pitch = dst_unmap_cmd.info.mem_xfer.cb[0];
  dst_unmap_cmd.info.mem_xfer.dst_slice_pitch = 1;
  l_mem_transfer_buffer_explicitly(context, op, physical_id, dst_unmap_cmd);
}

static void sync_subbuffers(cl_mem mem, cl_context context, acl_device_op_t *op,
                            unsigned int physical_device_id) {
  int other_buffer_on_host;
  cl_mem other_buffer_mem;
  // For sub-buffers, first check the parent.
  if (mem->fields.buffer_objs.is_subbuffer) {

    other_buffer_mem = mem->fields.buffer_objs.parent;
    // For parent buffers, just go through the sub buffers
  } else {
    other_buffer_mem = mem->fields.buffer_objs.next_sub;
  }

  while (other_buffer_mem != NULL) {
    // Overlaps
    if (!((mem->fields.buffer_objs.sub_origin + mem->size <=
           other_buffer_mem->fields.buffer_objs.sub_origin) ||
          (mem->fields.buffer_objs.sub_origin >=
           other_buffer_mem->fields.buffer_objs.sub_origin +
               other_buffer_mem->size))) {

      l_get_address_of_writable_copy(other_buffer_mem, physical_device_id,
                                     &other_buffer_on_host, CL_FALSE);
      // If other buffer is on the device and writable, it is considered the
      // live version. If there are 2 or more overlapping sub buffers that meet
      // this, then 2 overlapping sub buffers were writable at the same time and
      // the behaviour is undefined. If this is host accessible, the live data
      // is immediately copied back to the device as soon as the event is
      // complete. If the event is not complete, we have 2 copies of writable
      // data at the same time and the behaviour is undefined.
      if (!other_buffer_on_host &&
          !(other_buffer_mem->flags & CL_MEM_READ_ONLY) &&
          !other_buffer_mem->block_allocation->region->is_host_accessible) {
        acl_command_info_t other_cmd;
        if (!other_buffer_mem->host_mem.aligned_ptr) {
          acl_context_callback(context, "Could not allocate backing store for "
                                        "a device buffer with a sub buffer.");
          acl_set_device_op_execution_status(op, -1);
          return;
        }
        other_cmd.type = CL_COMMAND_MAP_BUFFER;
        other_cmd.info.mem_xfer.is_auto_map = 1;
        if (other_buffer_mem->flags & CL_MEM_READ_ONLY) {
          other_cmd.info.mem_xfer.map_flags = CL_MAP_READ;
        } else {
          other_cmd.info.mem_xfer.map_flags = CL_MAP_WRITE;
        }
        other_cmd.info.mem_xfer.src_mem = other_buffer_mem;
        other_cmd.info.mem_xfer.src_offset[0] = 0;
        other_cmd.info.mem_xfer.src_offset[1] = 0;
        other_cmd.info.mem_xfer.src_offset[2] = 0;
        other_cmd.info.mem_xfer.dst_mem = context->unwrapped_host_mem;
        other_cmd.info.mem_xfer.dst_offset[0] =
            (size_t)((char *)other_buffer_mem->host_mem.aligned_ptr -
                     (char *)ACL_MEM_ALIGN);
        other_cmd.info.mem_xfer.dst_offset[1] = 0;
        other_cmd.info.mem_xfer.dst_offset[2] = 0;
        other_cmd.info.mem_xfer.cb[0] = other_buffer_mem->size;
        other_cmd.info.mem_xfer.cb[1] = 1;
        other_cmd.info.mem_xfer.cb[2] = 1;
        other_cmd.info.mem_xfer.src_row_pitch = other_cmd.info.mem_xfer.cb[0];
        other_cmd.info.mem_xfer.src_slice_pitch = 1;
        other_cmd.info.mem_xfer.dst_row_pitch = other_cmd.info.mem_xfer.cb[0];
        other_cmd.info.mem_xfer.dst_slice_pitch = 1;

        // Setting other_cmd.trivial to avoid coverity warning. Trivial is not
        // used in this instance.
        other_cmd.trivial = 0;
        l_mem_transfer_buffer_explicitly(context, NULL, physical_device_id,
                                         other_cmd);
      }
    }
    other_buffer_mem = other_buffer_mem->fields.buffer_objs.next_sub;
  }
}

// Determine if memory transfer operation requires data transfer.
// RTE can make various optimizations based on flags the buffer was created
// with and/or mapped with.
// Furthermore, if they don't result in transfers, they can be executed along
// with other memory transfer ops without conflict in device-op queue.
int acl_mem_op_requires_transfer(const acl_command_info_t &cmd) {
  cl_mem dst_mem = cmd.info.mem_xfer.dst_mem;

  // We should skip the memory transfer when mapping and the
  // map flag is CL_MAP_WRITE_INVALIDATE_REGION since we don't care about the
  // contents of the mapped region. We should also skip the memory transfer when
  // unmapping if the buffer was previously mapped with CL_MAP_READ (if the
  // destination memory's writable copy is not the host)
  return !(cmd.type == CL_COMMAND_MAP_BUFFER &&
           (cmd.info.mem_xfer.map_flags & CL_MAP_WRITE_INVALIDATE_REGION)) &&
         !(cmd.type == CL_COMMAND_UNMAP_MEM_OBJECT &&
           !dst_mem->writable_copy_on_host);
}

// Transfer memory, using an explicit context and cmd.
// Signal command state transitions to the given event, which may be NULL.
// If event is NULL, then do the transfer in a blocking manner.
static void l_mem_transfer_buffer_explicitly(cl_context context,
                                             acl_device_op_t *op,
                                             unsigned int physical_device_id,
                                             const acl_command_info_t &cmd) {
  void *src_base;
  void *dst_base;
  void *src_data_base;
  void *dst_data_base;
  int src_on_host;
  int dst_on_host;
  cl_mem src_mem = cmd.info.mem_xfer.src_mem;
  cl_mem dst_mem = cmd.info.mem_xfer.dst_mem;
  acl_assert_locked();

  acl_set_device_op_execution_status(op, CL_RUNNING);

  switch (cmd.type) {
  case CL_COMMAND_MAP_BUFFER:
  case CL_COMMAND_UNMAP_MEM_OBJECT:
  case CL_COMMAND_READ_BUFFER:
  case CL_COMMAND_WRITE_BUFFER:
  case CL_COMMAND_COPY_BUFFER:
    break;
  default:
    acl_context_callback(
        context, "Internal error: invalid memory transfer completion type. "
                 "Corrupt host memory?");
    acl_set_device_op_execution_status(op, -1);
    return;
  }

  if (!acl_mem_is_valid(src_mem)) {
    acl_context_callback(
        context, "Internal error: Invalid source buffer for memory transfer "
                 "completion. Corrupt host memory?");
    acl_set_device_op_execution_status(op, -1);
    return;
  }
  if (!acl_mem_is_valid(dst_mem)) {
    acl_context_callback(
        context, "Internal error: Invalid destination "
                 "buffer for memory transfer completion. Corrupt host memory?");
    acl_set_device_op_execution_status(op, -1);
    return;
  }

  // Need to know the base address of both buffers.
  // Note that either of these can be a host address or a device address.
  //
  // The tricky bit is the map (respectively unmap) case, but the caller
  // has already set the destination (respectively source) address to the host
  // side pointer.

  src_base = l_get_address_of_writable_copy(src_mem, physical_device_id,
                                            &src_on_host, CL_FALSE);
  dst_base = l_get_address_of_writable_copy(
      dst_mem, physical_device_id, &dst_on_host,
      (cl_bool)(cmd.type == CL_COMMAND_UNMAP_MEM_OBJECT));

  bool should_transfer = acl_mem_op_requires_transfer(cmd);

  if (should_transfer) {
    // We'll perform the transfer.
    void **src;
    void **dst;
    size_t src_element_size;
    size_t dst_element_size;
    cl_int errcode_ret;
    const acl_hal_t *const hal = acl_get_hal();
    cl_bool single_copy = CL_TRUE;
    size_t src_row_pitch;
    size_t src_slice_pitch;
    size_t dst_row_pitch;
    size_t dst_slice_pitch;

    void (*hal_dma_fn)(cl_event, const void *, void *, size_t) = 0;
    size_t *size, num_memory_transfers, imem_xfr;

    if (is_image(src_mem)) {
      src_element_size = acl_get_image_element_size(
          src_mem->context, src_mem->fields.image_objs.image_format,
          &errcode_ret);
      if (errcode_ret != CL_SUCCESS) {
        acl_context_callback(context, "Invalid image for memory transfer.");
        acl_set_device_op_execution_status(op, -1);
        return;
      }
    } else {
      src_element_size = 1;
    }
    if (is_image(dst_mem)) {
      dst_element_size = acl_get_image_element_size(
          dst_mem->context, dst_mem->fields.image_objs.image_format,
          &errcode_ret);
      if (errcode_ret != CL_SUCCESS) {
        acl_context_callback(context, "Invalid image for memory transfer.");
        acl_set_device_op_execution_status(op, -1);
        return;
      }
    } else {
      dst_element_size = 1;
    }
    // If this is an image, element size was set above. Otherwise, element size
    // is one.
    if (is_image(src_mem) && is_image(dst_mem) &&
        src_element_size != dst_element_size) {
      acl_context_callback(context,
                           "Invalid image for memory transfer. Element size "
                           "between source and destination don't line up.");
      acl_set_device_op_execution_status(op, -1);
      return;
    }

    if (src_mem->mem_object_type == CL_MEM_OBJECT_BUFFER) {
      src_row_pitch = cmd.info.mem_xfer.src_row_pitch;
      src_slice_pitch = cmd.info.mem_xfer.src_slice_pitch;
    } else if (is_image(src_mem)) {
      src_row_pitch =
          src_mem->fields.image_objs.image_desc->image_width * src_element_size;
      if (src_mem->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
        src_slice_pitch = src_row_pitch;
      } else {
        src_slice_pitch =
            src_mem->fields.image_objs.image_desc->image_height * src_row_pitch;
      }
    } else {
      acl_context_callback(context, "Invalid memory type for memory transfer.");
      acl_set_device_op_execution_status(op, -1);
      return;
    }
    if (dst_mem->mem_object_type == CL_MEM_OBJECT_BUFFER) {
      dst_row_pitch = cmd.info.mem_xfer.dst_row_pitch;
      dst_slice_pitch = cmd.info.mem_xfer.dst_slice_pitch;
    } else if (is_image(dst_mem)) {
      dst_row_pitch =
          dst_mem->fields.image_objs.image_desc->image_width * dst_element_size;
      if (dst_mem->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
        dst_slice_pitch = dst_row_pitch;
      } else {
        dst_slice_pitch =
            dst_mem->fields.image_objs.image_desc->image_height * dst_row_pitch;
      }
    } else {
      acl_context_callback(context, "Invalid memory type for memory transfer.");
      acl_set_device_op_execution_status(op, -1);
      return;
    }

    // Check if we can do this in a single copy. We can if the sizes line up.
    if (((src_mem == src_mem->context->unwrapped_host_mem ||
          cmd.info.mem_xfer.src_offset[0] == 0) &&
         (dst_mem == dst_mem->context->unwrapped_host_mem ||
          cmd.info.mem_xfer.dst_offset[0] == 0) &&
         cmd.info.mem_xfer.cb[0] == src_row_pitch &&
         src_row_pitch == dst_row_pitch) &&
        (cmd.info.mem_xfer.cb[2] == 1 ||
         (cmd.info.mem_xfer.src_offset[1] == 0 &&
          cmd.info.mem_xfer.dst_offset[1] == 0 &&
          cmd.info.mem_xfer.cb[1] * cmd.info.mem_xfer.cb[0] ==
              src_slice_pitch &&
          src_slice_pitch == dst_slice_pitch))) {
      single_copy = CL_TRUE;
    } else {
      single_copy = CL_FALSE;
    }

    // If this is a buffer with sub-buffers or a sub-buffer, need to make sure
    // all overlapping regions are in sync first For buffers & sub-buffers,
    // there can only be one active writable copy on any device at a time. If
    // this buffer/sub-buffer is writable and on a device, it _must_ be the live
    // copy so we don't need to do anything (or there are multiple live copies,
    // in which case behaviour is undefined, and we still don't need to do
    // anything). Only check for buffers & sub-buffers if the source is on the
    // host or this memory is read only
    if ((src_mem->mem_object_type == CL_MEM_OBJECT_BUFFER &&
         (src_on_host || (src_mem->flags & CL_MEM_READ_ONLY))) &&
        acl_is_sub_or_parent_buffer(src_mem)) {
      sync_subbuffers(src_mem, context, op, physical_device_id);

      // If the buffer is read only, we just copied back any buffers that
      // changed and the writable copy is now on the host. This is important for
      // sub buffers, where we don't want to copy the data back from multiple
      // buffers & subbuffers if they are read only. (For dst_mem, this flag
      // will be set based on its ultimate destination)
      if (!src_on_host && (src_mem->flags & CL_MEM_READ_ONLY)) {
        src_mem->writable_copy_on_host = 1;
        src_base = l_get_address_of_writable_copy(src_mem, physical_device_id,
                                                  &src_on_host, CL_FALSE);
      }
    }

    // Repeat for the destination buffer if this is a destination buffer that
    // has been auto-mapped and it is a sub buffer or has sub buffers that may
    // have changed the live data
    if (dst_mem->mem_object_type == CL_MEM_OBJECT_BUFFER &&
        dst_mem->auto_mapped) {
      if (acl_is_sub_or_parent_buffer(dst_mem)) {
        sync_subbuffers(dst_mem, context, op, physical_device_id);
      }

      // If the destination has been automapped, and this operation will not
      // unmap the object, then undo the automap before continuing.
      if (cmd.type != CL_COMMAND_UNMAP_MEM_OBJECT) {
        acl_command_info_t dst_unmap_cmd;
        dst_unmap_cmd.type = CL_COMMAND_UNMAP_MEM_OBJECT;
        dst_unmap_cmd.info.mem_xfer.is_auto_map = 1;
        if (dst_mem->flags & CL_MEM_READ_ONLY) {
          dst_unmap_cmd.info.mem_xfer.map_flags = CL_MAP_READ;
        } else {
          dst_unmap_cmd.info.mem_xfer.map_flags = CL_MAP_WRITE;
        }
        dst_unmap_cmd.info.mem_xfer.src_mem = context->unwrapped_host_mem;
        dst_unmap_cmd.info.mem_xfer.src_offset[0] =
            (size_t)((char *)dst_mem->host_mem.aligned_ptr -
                     (char *)ACL_MEM_ALIGN);
        dst_unmap_cmd.info.mem_xfer.src_offset[1] = 0;
        dst_unmap_cmd.info.mem_xfer.src_offset[2] = 0;
        dst_unmap_cmd.info.mem_xfer.dst_mem = dst_mem;
        dst_unmap_cmd.info.mem_xfer.dst_offset[0] = 0;
        dst_unmap_cmd.info.mem_xfer.dst_offset[1] = 0;
        dst_unmap_cmd.info.mem_xfer.dst_offset[2] = 0;
        dst_unmap_cmd.info.mem_xfer.cb[0] = dst_mem->size;
        dst_unmap_cmd.info.mem_xfer.cb[1] = 1;
        dst_unmap_cmd.info.mem_xfer.cb[2] = 1;
        dst_unmap_cmd.info.mem_xfer.src_row_pitch =
            dst_unmap_cmd.info.mem_xfer.cb[0];
        dst_unmap_cmd.info.mem_xfer.src_slice_pitch = 1;
        dst_unmap_cmd.info.mem_xfer.dst_row_pitch =
            dst_unmap_cmd.info.mem_xfer.cb[0];
        dst_unmap_cmd.info.mem_xfer.dst_slice_pitch = 1;
        l_mem_transfer_buffer_explicitly(context, NULL, physical_device_id,
                                         dst_unmap_cmd);
        dst_base = l_get_address_of_writable_copy(dst_mem, physical_device_id,
                                                  &dst_on_host, CL_FALSE);
      }
    }

    // Determine the base address of the data, excluding any metadata
    if (is_image(src_mem)) {
      src_data_base =
          ((char *)src_base) +
          get_offset_for_image_param(context, src_mem->mem_object_type, "data");
    } else {
      src_data_base = src_base;
    }
    if (is_image(dst_mem)) {
      dst_data_base =
          ((char *)dst_base) +
          get_offset_for_image_param(context, dst_mem->mem_object_type, "data");
    } else {
      dst_data_base = dst_base;
    }

    if (is_image(dst_mem) && !dst_on_host) {
      // If we are copying to an image on a device, make sure we have copied the
      // meta data
      copy_image_metadata(dst_mem);
    }

    // Can do a single copy. Determine starting address & size of this copy
    if (single_copy) {
      num_memory_transfers = 1;
      src = (void **)acl_malloc(sizeof(void *) * num_memory_transfers);
      dst = (void **)acl_malloc(sizeof(void *) * num_memory_transfers);
      size = (size_t *)acl_malloc(sizeof(size_t) * num_memory_transfers);
      size[0] = cmd.info.mem_xfer.cb[0] * cmd.info.mem_xfer.cb[1] *
                cmd.info.mem_xfer.cb[2];

      src[0] = ((char *)src_data_base) +
               cmd.info.mem_xfer.src_offset[0] * src_element_size +
               cmd.info.mem_xfer.src_offset[1] * src_row_pitch +
               cmd.info.mem_xfer.src_offset[2] * src_slice_pitch;
      dst[0] = ((char *)dst_data_base) +
               cmd.info.mem_xfer.dst_offset[0] * dst_element_size +
               cmd.info.mem_xfer.dst_offset[1] * dst_row_pitch +
               cmd.info.mem_xfer.dst_offset[2] * dst_slice_pitch;
      // Need to do multiple copies to cover all affected regions of memory.
      // Currently we treat each row as a separate region. It may be possible to
      // optimize this more in the future. For example we may be able to copy
      // slices if we are copying complete rows.
    } else {
      size_t deep;
      size_t row;
      num_memory_transfers = cmd.info.mem_xfer.cb[1] * cmd.info.mem_xfer.cb[2];
      assert(num_memory_transfers > 0);
      src = (void **)acl_malloc(sizeof(void *) * num_memory_transfers);
      dst = (void **)acl_malloc(sizeof(void *) * num_memory_transfers);
      size = (size_t *)acl_malloc(sizeof(size_t) * num_memory_transfers);
      for (deep = 0; deep < cmd.info.mem_xfer.cb[2]; ++deep) {
        for (row = 0; row < cmd.info.mem_xfer.cb[1]; ++row) {
          src[row + deep * cmd.info.mem_xfer.cb[1]] =
              ((char *)src_data_base) +
              cmd.info.mem_xfer.src_offset[0] * src_element_size +
              (cmd.info.mem_xfer.src_offset[1] + row) * src_row_pitch +
              (cmd.info.mem_xfer.src_offset[2] + deep) * src_slice_pitch;
          dst[row + deep * cmd.info.mem_xfer.cb[1]] =
              ((char *)dst_data_base) +
              cmd.info.mem_xfer.dst_offset[0] * dst_element_size +
              (cmd.info.mem_xfer.dst_offset[1] + row) * dst_row_pitch +
              (cmd.info.mem_xfer.dst_offset[2] + deep) * dst_slice_pitch;
          size[row + deep * cmd.info.mem_xfer.cb[1]] = cmd.info.mem_xfer.cb[0];
        }
      }
    }

    // Determine which HAL copy function to use, depending on where the memory
    // is
    if (src_on_host) {
      if (dst_on_host) {
        hal_dma_fn = hal->copy_hostmem_to_hostmem;
      } else {
        hal_dma_fn = hal->copy_hostmem_to_globalmem;
      }
    } else {
      if (dst_on_host) {
        hal_dma_fn = hal->copy_globalmem_to_hostmem;
      } else {
        hal_dma_fn = hal->copy_globalmem_to_globalmem;
      }
    }

    for (imem_xfr = 0; imem_xfr < num_memory_transfers; ++imem_xfr) {
      acl_print_debug_msg(
          "      l_mem_transfer_buffer_explicitly %d %d (%p,%p,%lu)\n",
          src_on_host, dst_on_host, src[imem_xfr], dst[imem_xfr],
          (unsigned long)size[imem_xfr]);
// Go for it!
#ifdef MEM_DEBUG_MSG
      printf("l_mem_transfer_buffer_explicitly src %zx dest %zx ([%d]%zx -> "
             "[%d]%zx)\n",
             (size_t)src_mem, (size_t)dst_mem, src_on_host,
             (size_t)src[imem_xfr], dst_on_host, (size_t)dst[imem_xfr]);
#endif
      hal_dma_fn((op ? op->info.event : 0), src[imem_xfr], dst[imem_xfr],
                 size[imem_xfr]);
    }

    if (is_image(src_mem)) {
      if (op == NULL) {
        size_t element_size;
        void *local_meta_data = malloc(get_offset_for_image_param(
            src_mem->context, src_mem->mem_object_type, "data"));
        int errcode;
        element_size = acl_get_image_element_size(
            src_mem->context, src_mem->fields.image_objs.image_format,
            &errcode);

        if (errcode != CL_SUCCESS) {
          acl_context_callback(context, "Could not determine image type.");
          acl_set_device_op_execution_status(op, -1);

          acl_free(src);
          acl_free(dst);
          acl_free(size);
          free(local_meta_data);
          return;
        }

        safe_memcpy(
            (char *)local_meta_data +
                get_offset_for_image_param(src_mem->context,
                                           src_mem->mem_object_type, "width"),
            &(src_mem->fields.image_objs.image_desc->image_width), 4, 4, 4);
        if (src_mem->mem_object_type == CL_MEM_OBJECT_IMAGE2D ||
            src_mem->mem_object_type == CL_MEM_OBJECT_IMAGE3D ||
            src_mem->mem_object_type == CL_MEM_OBJECT_IMAGE2D_ARRAY) {
          safe_memcpy(
              (char *)local_meta_data +
                  get_offset_for_image_param(
                      src_mem->context, src_mem->mem_object_type, "height"),
              &(src_mem->fields.image_objs.image_desc->image_height), 4, 4, 4);
        }
        if (src_mem->mem_object_type == CL_MEM_OBJECT_IMAGE3D) {
          safe_memcpy(
              (char *)local_meta_data +
                  get_offset_for_image_param(src_mem->context,
                                             src_mem->mem_object_type, "depth"),
              &(src_mem->fields.image_objs.image_desc->image_depth), 4, 4, 4);
        }
        if (src_mem->mem_object_type == CL_MEM_OBJECT_IMAGE2D_ARRAY ||
            src_mem->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
          safe_memcpy(
              (char *)local_meta_data +
                  get_offset_for_image_param(
                      src_mem->context, src_mem->mem_object_type, "array_size"),
              &(src_mem->fields.image_objs.image_desc->image_array_size), 4, 4,
              4);
        }
        safe_memcpy(
            (char *)local_meta_data +
                get_offset_for_image_param(src_mem->context,
                                           src_mem->mem_object_type,
                                           "channel_data_type"),
            &(src_mem->fields.image_objs.image_format->image_channel_data_type),
            4, 4, 4);
        safe_memcpy(
            (char *)local_meta_data +
                get_offset_for_image_param(src_mem->context,
                                           src_mem->mem_object_type,
                                           "channel_order"),
            &(src_mem->fields.image_objs.image_format->image_channel_order), 4,
            4, 4);
        safe_memcpy((char *)local_meta_data +
                        get_offset_for_image_param(src_mem->context,
                                                   src_mem->mem_object_type,
                                                   "element_size"),
                    &(element_size), 8, 8, 8);

        hal_dma_fn((op ? op->info.event : 0), local_meta_data, dst_base,
                   get_offset_for_image_param(context, src_mem->mem_object_type,
                                              "data"));

        free(local_meta_data);
      }
    }

    acl_free(src);
    acl_free(dst);
    acl_free(size);
  } else {
    // If the HAL isn't doing anything for us, then it won't signal
    // completion either.
    // Must do that for ourselves.
    acl_set_device_op_execution_status(op, CL_COMPLETE);
  }

  // Track the mapping count and the location of the writable copy.
  if (cmd.type == CL_COMMAND_MAP_BUFFER) {
    if (!cmd.info.mem_xfer.is_auto_map) {
      src_mem->mapping_count++;
    } else {
      src_mem->auto_mapped = 1;
    }
    if (cmd.info.mem_xfer.map_flags & CL_MAP_WRITE ||
        cmd.info.mem_xfer.map_flags & CL_MAP_WRITE_INVALIDATE_REGION) {
      // The writable copy is now on the host.
      src_mem->writable_copy_on_host = 1;
    }
  } else if (cmd.type == CL_COMMAND_UNMAP_MEM_OBJECT) {
    if (!cmd.info.mem_xfer.is_auto_map) {
      dst_mem->mapping_count--;
    }
    // If this memory is explicitly unmapped by the user, we can release
    // the auto-mapping as well.
    dst_mem->auto_mapped = 0;
    if (dst_mem->mapping_count == 0) {
      // No more mappings
      // The writable copy is back in the home location.
      dst_mem->writable_copy_on_host =
          dst_mem->block_allocation->region->is_host_accessible;
    }
  }
}

cl_bool acl_is_sub_or_parent_buffer(cl_mem mem) {
  return (cl_bool)(mem->fields.buffer_objs.is_subbuffer ||
                   mem->fields.buffer_objs.next_sub != NULL);
}

static cl_bool is_image(cl_mem mem) {
  return (cl_bool)(mem->mem_object_type == CL_MEM_OBJECT_IMAGE2D ||
                   mem->mem_object_type == CL_MEM_OBJECT_IMAGE3D ||
                   mem->mem_object_type == CL_MEM_OBJECT_IMAGE2D_ARRAY ||
                   mem->mem_object_type == CL_MEM_OBJECT_IMAGE1D ||
                   mem->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY ||
                   mem->mem_object_type == CL_MEM_OBJECT_IMAGE1D_BUFFER);
}

static void l_free_image_members(cl_mem mem) {
  if (mem->fields.image_objs.image_format != NULL) {
    acl_free(mem->fields.image_objs.image_format);
  }
  if (mem->fields.image_objs.image_desc != NULL) {
    if (mem->fields.image_objs.image_desc->buffer != NULL) {
      clReleaseMemObject(mem->fields.image_objs.image_desc->buffer);
      mem->fields.image_objs.image_desc->buffer = NULL;
    }
    if (mem->fields.image_objs.image_desc->mem_object != NULL) {
      clReleaseMemObject(mem->fields.image_objs.image_desc->mem_object);
      mem->fields.image_objs.image_desc->mem_object = NULL;
    }
    acl_free(mem->fields.image_objs.image_desc);
  }
}

void acl_copy_device_buffers_to_host_before_programming(
    cl_context _context, unsigned int physical_device_id,
    void(CL_CALLBACK *read_callback)(cl_mem, int)) {
  // Copy all device buffers into host memory.
  // Regardless of which context they belong to.
  //
  // We need this because reprogramming the device wipes out DDR
  // memory, where device global buffers live.
  // After reprogramming, we'll unmap the buffers again.
  // The reason this works is quite subtle:
  //
  //  a. It's the programmer's responsibility to fully unmap any
  //     buffers used by a kernel *before* the kernel launches.
  //     The OpenCL 1.2 spec makes this clear in section 5.4.3
  //     "Accessing mapped regions of a memory object".
  //
  //  b. By the waiting done prior to programming the device, no other
  //     kernels are running on this device.
  //
  //  c. There may be buffers in device memory (not mapped into host
  //     address space) that are not arguments to this kernel.  They will be
  //     mapped by these calls, and then unmapped (and thus put back
  //     into device memory after reprogramming).
  //
  //  d. There may be buffers mapped into the host that are not
  //     arguments to this kernel. So they don't fall under case (a).
  //     So they have a positive map count before mapping as
  //     writable here.  After programming, they will be unmapped
  //     again.
  //     We split into two cases:
  //       d.1.  The buffers were originally mapped for write
  //             (CL_MAP_WRITE).
  //             After mapping again for write, here, then
  //             programming, then unmapping, they will continue to
  //             be mapped with CL_MAP_WRITE.  Just exactly as the
  //             user expects.  (Our automatic mapping was invisible
  //             to the user.)
  //       d.2.  The buffers were originally mapped read-only, CL_MAP_READ.
  //             After mapping as CL_MAP_WRITE here, then reprogramming,
  //             then unmapped, those buffers will *stay*
  //             as CL_MAP_WRITE after the unmap.
  //             But this is ok because:
  //                d.2.i.   The programmer promised not to update the data.
  //                d.2.ii.  This buffer is not valid to use in a
  //                         kernel until it is completely unmapped again, by
  //                         user unmap calls.
  //             So it is ok for the host to have the writable copy.
  cl_mem mem = 0;
  acl_block_allocation_t *block = 0;
  acl_assert_locked();

  if (debug_mode > 0) {
    printf(" Explicit read of all device side buffers\n");
    printf(" Context %p gm %p. ... p gm %p \n", _context, _context->global_mem,
           &acl_platform.global_mem);
    printf(" first_mem %p\n", _context->global_mem->first_block);
  }

  for (block = _context->global_mem->first_block; block != NULL;
       block = block->next_block_in_region) {
    mem = block->mem_obj;

    if (debug_mode > 0) {
      printf("   Consider:\n");
      acl_dump_mem_internal(mem);
    }

    if (mem->allocation_deferred || mem->mem_cpy_host_ptr_pending ||
        ACL_GET_PHYSICAL_ID(mem->block_allocation->range.begin) !=
            physical_device_id) {
      // If the memory isn't actually allocated yet OR it's not on the device
      // being reprogrammed then just skip OR we haven't even dont the memory
      // copied to the device yet
      continue;
    }
    // Copy to host only if the writable copy is not on the host.
    if (!mem->writable_copy_on_host) {
      cl_context context2 = mem->context;
      acl_command_info_t cmd;

      if (debug_mode > 0) {
        printf(" Explicit read of mem [%p]\n", mem);
        acl_dump_mem_internal(mem);
      }

      cmd.type = CL_COMMAND_READ_BUFFER;
      cmd.trivial = 0; // not used
      cmd.info.mem_xfer.src_mem = mem;
      cmd.info.mem_xfer.src_offset[0] = 0;
      cmd.info.mem_xfer.src_offset[1] = 0;
      cmd.info.mem_xfer.src_offset[2] = 0;
      cmd.info.mem_xfer.dst_mem = context2->unwrapped_host_mem;
      if (mem->flags & CL_MEM_USE_HOST_PTR) {
        cmd.info.mem_xfer.dst_offset[0] =
            (size_t)((char *)mem->fields.buffer_objs.host_ptr -
                     (char *)ACL_MEM_ALIGN);
      } else {
        cmd.info.mem_xfer.dst_offset[0] =
            (size_t)((char *)mem->host_mem.aligned_ptr - (char *)ACL_MEM_ALIGN);
      }
      cmd.info.mem_xfer.dst_offset[1] = 0;
      cmd.info.mem_xfer.dst_offset[2] = 0;
      cmd.info.mem_xfer.is_auto_map = 0;
      if (mem->mem_object_type == CL_MEM_OBJECT_BUFFER) {
        cmd.info.mem_xfer.cb[0] = mem->size;
        cmd.info.mem_xfer.cb[1] = 1;
        cmd.info.mem_xfer.cb[2] = 1;
      } else if (mem->mem_object_type == CL_MEM_OBJECT_IMAGE2D) {
        cl_int local_errcode_ret;
        size_t element_size = acl_get_image_element_size(
            mem->context, mem->fields.image_objs.image_format,
            &local_errcode_ret);
        if (local_errcode_ret != CL_SUCCESS) {
          acl_context_callback(context2,
                               "Invalid cl_mem object mirrored to device.");
        }
        cmd.info.mem_xfer.cb[0] =
            mem->fields.image_objs.image_desc->image_width * element_size;
        cmd.info.mem_xfer.cb[1] =
            mem->fields.image_objs.image_desc->image_height;
        cmd.info.mem_xfer.cb[2] = 1;
      } else if (mem->mem_object_type == CL_MEM_OBJECT_IMAGE3D) {
        cl_int local_errcode_ret;
        size_t element_size = acl_get_image_element_size(
            mem->context, mem->fields.image_objs.image_format,
            &local_errcode_ret);
        if (local_errcode_ret != CL_SUCCESS) {
          acl_context_callback(context2,
                               "Invalid cl_mem object mirrored to device.");
        }
        cmd.info.mem_xfer.cb[0] =
            mem->fields.image_objs.image_desc->image_width * element_size;
        cmd.info.mem_xfer.cb[1] =
            mem->fields.image_objs.image_desc->image_height;
        cmd.info.mem_xfer.cb[2] =
            mem->fields.image_objs.image_desc->image_depth;
      } else if (mem->mem_object_type == CL_MEM_OBJECT_IMAGE2D_ARRAY) {
        cl_int local_errcode_ret;
        size_t element_size = acl_get_image_element_size(
            mem->context, mem->fields.image_objs.image_format,
            &local_errcode_ret);
        if (local_errcode_ret != CL_SUCCESS) {
          acl_context_callback(context2,
                               "Invalid cl_mem object mirrored to device.");
        }
        cmd.info.mem_xfer.cb[0] =
            mem->fields.image_objs.image_desc->image_width * element_size;
        cmd.info.mem_xfer.cb[1] =
            mem->fields.image_objs.image_desc->image_height;
        cmd.info.mem_xfer.cb[2] =
            mem->fields.image_objs.image_desc->image_array_size;
      } else if (mem->mem_object_type == CL_MEM_OBJECT_IMAGE1D ||
                 mem->mem_object_type == CL_MEM_OBJECT_IMAGE1D_BUFFER) {
        cl_int local_errcode_ret;
        size_t element_size = acl_get_image_element_size(
            mem->context, mem->fields.image_objs.image_format,
            &local_errcode_ret);
        if (local_errcode_ret != CL_SUCCESS) {
          acl_context_callback(context2,
                               "Invalid cl_mem object mirrored to device.");
        }
        cmd.info.mem_xfer.cb[0] =
            mem->fields.image_objs.image_desc->image_width * element_size;
        cmd.info.mem_xfer.cb[1] = 1;
        cmd.info.mem_xfer.cb[2] = 1;
      } else if (mem->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
        cl_int local_errcode_ret;
        size_t element_size = acl_get_image_element_size(
            mem->context, mem->fields.image_objs.image_format,
            &local_errcode_ret);
        if (local_errcode_ret != CL_SUCCESS) {
          acl_context_callback(context2,
                               "Invalid cl_mem object mirrored to device.");
        }
        cmd.info.mem_xfer.cb[0] =
            mem->fields.image_objs.image_desc->image_width * element_size;
        cmd.info.mem_xfer.cb[1] =
            mem->fields.image_objs.image_desc->image_array_size;
        cmd.info.mem_xfer.cb[2] = 1;
      } else {
        acl_context_callback(context2,
                             "Invalid cl_mem object mirrored to device.");
        cmd.info.mem_xfer.cb[0] = mem->size;
        cmd.info.mem_xfer.cb[1] = 1;
        cmd.info.mem_xfer.cb[2] = 1;
      }
      cmd.info.mem_xfer.map_flags = 0; // not used
      cmd.info.mem_xfer.src_row_pitch = cmd.info.mem_xfer.cb[0];
      cmd.info.mem_xfer.src_slice_pitch = 1;
      cmd.info.mem_xfer.dst_row_pitch = cmd.info.mem_xfer.cb[0];
      cmd.info.mem_xfer.dst_slice_pitch = 1;

      if (read_callback)
        read_callback(mem, 0);
      l_mem_transfer_buffer_explicitly(context2, 0 /*blocking*/,
                                       physical_device_id, cmd);
      if (read_callback)
        read_callback(mem, 1);
    }
  }
}

void acl_copy_device_buffers_from_host_after_programming(
    cl_context _context, unsigned int physical_device_id,
    void(CL_CALLBACK *write_callback)(cl_mem, int)) {
  // Copy all device buffers back from host memory.
  cl_mem mem = 0;
  acl_block_allocation_t *block = 0;
  acl_assert_locked();

  acl_print_debug_msg(" Explicit write of all device side buffers\n");

  for (block = _context->global_mem->first_block; block != NULL;
       block = block->next_block_in_region) {
    mem = block->mem_obj;

    acl_print_debug_msg("   write mem[%p]?\n", mem);

    if (mem->allocation_deferred || mem->mem_cpy_host_ptr_pending ||
        ACL_GET_PHYSICAL_ID(mem->block_allocation->range.begin) !=
            physical_device_id) {
      // If the memory isn't actually allocated yet OR it's not on the device
      // being reprogrammed then just skip OR we haven't even dont the memory
      // copied to the device yet
      continue;
    }
    // Copy back from host only if the "writable" copy is not on the host.
    if (!mem->writable_copy_on_host) {
      cl_context context2 = mem->context;
      acl_command_info_t cmd;
      acl_print_debug_msg(" Explicit write of mem[%p]\n", mem);

      if (debug_mode > 0) {
        acl_dump_mem_internal(mem);
      }

      cmd.type = CL_COMMAND_WRITE_BUFFER;
      cmd.info.mem_xfer.src_mem = context2->unwrapped_host_mem;
      if (mem->flags & CL_MEM_USE_HOST_PTR) {
        cmd.info.mem_xfer.src_offset[0] =
            (size_t)((char *)mem->fields.buffer_objs.host_ptr -
                     (char *)ACL_MEM_ALIGN);
      } else {
        cmd.info.mem_xfer.src_offset[0] =
            (size_t)((char *)mem->host_mem.aligned_ptr - (char *)ACL_MEM_ALIGN);
      }
      cmd.info.mem_xfer.src_offset[1] = 0;
      cmd.info.mem_xfer.src_offset[2] = 0;
      cmd.info.mem_xfer.dst_mem = mem;
      cmd.info.mem_xfer.dst_offset[0] = 0;
      cmd.info.mem_xfer.dst_offset[1] = 0;
      cmd.info.mem_xfer.dst_offset[2] = 0;
      cmd.info.mem_xfer.is_auto_map = 0;
      cmd.trivial = 0; // not used
      if (mem->mem_object_type == CL_MEM_OBJECT_BUFFER) {
        cmd.info.mem_xfer.cb[0] = mem->size;
        cmd.info.mem_xfer.cb[1] = 1;
        cmd.info.mem_xfer.cb[2] = 1;
      } else if (mem->mem_object_type == CL_MEM_OBJECT_IMAGE2D) {
        cl_int local_errcode_ret;
        size_t element_size = acl_get_image_element_size(
            mem->context, mem->fields.image_objs.image_format,
            &local_errcode_ret);
        if (local_errcode_ret != CL_SUCCESS) {
          acl_context_callback(context2,
                               "Invalid cl_mem object mirrored to device.");
        }
        cmd.info.mem_xfer.cb[0] =
            mem->fields.image_objs.image_desc->image_width * element_size;
        cmd.info.mem_xfer.cb[1] =
            mem->fields.image_objs.image_desc->image_height;
        cmd.info.mem_xfer.cb[2] = 1;
      } else if (mem->mem_object_type == CL_MEM_OBJECT_IMAGE3D) {
        cl_int local_errcode_ret;
        size_t element_size = acl_get_image_element_size(
            mem->context, mem->fields.image_objs.image_format,
            &local_errcode_ret);
        if (local_errcode_ret != CL_SUCCESS) {
          acl_context_callback(context2,
                               "Invalid cl_mem object mirrored to device.");
        }
        cmd.info.mem_xfer.cb[0] =
            mem->fields.image_objs.image_desc->image_width * element_size;
        cmd.info.mem_xfer.cb[1] =
            mem->fields.image_objs.image_desc->image_height;
        cmd.info.mem_xfer.cb[2] =
            mem->fields.image_objs.image_desc->image_depth;
      } else if (mem->mem_object_type == CL_MEM_OBJECT_IMAGE2D_ARRAY) {
        cl_int local_errcode_ret;
        size_t element_size = acl_get_image_element_size(
            mem->context, mem->fields.image_objs.image_format,
            &local_errcode_ret);
        if (local_errcode_ret != CL_SUCCESS) {
          acl_context_callback(context2,
                               "Invalid cl_mem object mirrored to device.");
        }
        cmd.info.mem_xfer.cb[0] =
            mem->fields.image_objs.image_desc->image_width * element_size;
        cmd.info.mem_xfer.cb[1] =
            mem->fields.image_objs.image_desc->image_height;
        cmd.info.mem_xfer.cb[2] =
            mem->fields.image_objs.image_desc->image_array_size;
      } else if (mem->mem_object_type == CL_MEM_OBJECT_IMAGE1D ||
                 mem->mem_object_type == CL_MEM_OBJECT_IMAGE1D_BUFFER) {
        cl_int local_errcode_ret;
        size_t element_size = acl_get_image_element_size(
            mem->context, mem->fields.image_objs.image_format,
            &local_errcode_ret);
        if (local_errcode_ret != CL_SUCCESS) {
          acl_context_callback(context2,
                               "Invalid cl_mem object mirrored to device.");
        }
        cmd.info.mem_xfer.cb[0] =
            mem->fields.image_objs.image_desc->image_width * element_size;
        cmd.info.mem_xfer.cb[1] = 1;
        cmd.info.mem_xfer.cb[2] = 1;
      } else if (mem->mem_object_type == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
        cl_int local_errcode_ret;
        size_t element_size = acl_get_image_element_size(
            mem->context, mem->fields.image_objs.image_format,
            &local_errcode_ret);
        if (local_errcode_ret != CL_SUCCESS) {
          acl_context_callback(context2,
                               "Invalid cl_mem object mirrored to device.");
        }
        cmd.info.mem_xfer.cb[0] =
            mem->fields.image_objs.image_desc->image_width * element_size;
        cmd.info.mem_xfer.cb[1] =
            mem->fields.image_objs.image_desc->image_array_size;
        cmd.info.mem_xfer.cb[2] = 1;
      } else {
        acl_context_callback(context2,
                             "Invalid cl_mem object mirrored to device.");
        cmd.info.mem_xfer.cb[0] = mem->size;
        cmd.info.mem_xfer.cb[1] = 1;
        cmd.info.mem_xfer.cb[2] = 1;
      }
      cmd.info.mem_xfer.map_flags = 0; // not used

      cmd.info.mem_xfer.src_row_pitch = cmd.info.mem_xfer.cb[0];
      cmd.info.mem_xfer.src_slice_pitch = 1;
      cmd.info.mem_xfer.dst_row_pitch = cmd.info.mem_xfer.cb[0];
      cmd.info.mem_xfer.dst_slice_pitch = 1;

      if (write_callback)
        write_callback(mem, 0);
      l_mem_transfer_buffer_explicitly(context2, 0 /*blocking*/,
                                       physical_device_id, cmd);
      if (write_callback)
        write_callback(mem, 1);
    }
  }
}

static void acl_print_all_mem_in_region(acl_mem_region_t *region);
void acl_print_all_mem(void) {
  acl_assert_locked();

  acl_print_debug_msg("===============================================Current "
                      "memory allocations=====\n");

  acl_print_debug_msg("Host_auto_mem:");
  acl_print_all_mem_in_region(&acl_platform.host_auto_mem);

  acl_print_debug_msg("Host user mem:");
  acl_print_all_mem_in_region(&acl_platform.host_user_mem);

  acl_print_debug_msg("Global mem:");
  acl_print_all_mem_in_region(&acl_platform.global_mem);

  acl_print_debug_msg("Warning: if using an offline device, then not showing "
                      "emulated host mem\n");

  acl_print_debug_msg("========================================================"
                      "======================\n\n");
}

static void acl_print_all_mem_in_region(acl_mem_region_t *region) {
  int num_prints = 0;
  acl_block_allocation_t *block;
  cl_mem mem;
  acl_assert_locked();

  for (block = region->first_block; block != NULL;
       block = block->next_block_in_region) {
    mem = block->mem_obj;
    if (num_prints == 0)
      acl_print_debug_msg("\n");

    acl_print_debug_msg("\tAt %p: Range from %p to %p, size %lu", mem,
                        block->range.begin, block->range.next,
                        mem->size <= 10000 ? mem->size : 9999);
    if (mem->size > 10000)
      acl_print_debug_msg("+\n");
    else
      acl_print_debug_msg("\n");
    ++num_prints;
  }
  if (num_prints == 0)
    acl_print_debug_msg("\tN/A\n");
}

void acl_print_mem(cl_mem mem) {
  acl_assert_locked();

  if (debug_mode > 0) {
    printf("mem at %p:\n", mem);
    printf("\t- Range from %p to %p\n", mem->block_allocation->range.begin,
           mem->block_allocation->range.next);
    printf("\t- Size %zu\n", mem->size);
    printf("\t- Refcount = %d\n", acl_ref_count(mem));
    printf("\t- Context %p\n", mem->context);
    printf("\t- Host ptr = %p\n", mem->fields.buffer_objs.host_ptr);
  }
}

static void acl_print_all_detailed_mem_in_region(acl_mem_region_t *region);
void acl_print_all_mem_detail(void) {
  acl_assert_locked();

  if (acl_platform.host_auto_mem.first_block == NULL &&
      acl_platform.host_user_mem.first_block == NULL &&
      acl_platform.global_mem.first_block == NULL) {
    acl_print_debug_msg("====================================================="
                        "No mem allocated.========\n\n");
    return;
  }

  acl_print_debug_msg("===============================================Current "
                      "memory allocations=====\n");

  acl_print_debug_msg("Host_auto_mem:");
  acl_print_all_detailed_mem_in_region(&acl_platform.host_auto_mem);

  acl_print_debug_msg("Host user mem:");
  acl_print_all_detailed_mem_in_region(&acl_platform.host_user_mem);

  acl_print_debug_msg("Global mem:");
  acl_print_all_detailed_mem_in_region(&acl_platform.global_mem);

  acl_print_debug_msg("Warning: if using an offline device, then not showing "
                      "emulated host mem\n");

  acl_print_debug_msg("========================================================"
                      "======================\n\n");
}

static void acl_print_all_detailed_mem_in_region(acl_mem_region_t *region) {
  int num_prints = 0;
  acl_block_allocation_t *block;
  cl_mem mem;
  acl_assert_locked();

  for (block = region->first_block; block != NULL;
       block = block->next_block_in_region) {
    mem = block->mem_obj;
    if (num_prints == 0)
      acl_print_debug_msg("\n");
    acl_print_debug_msg("   ");
    acl_print_mem(mem);
    ++num_prints;
  }
  if (num_prints == 0)
    acl_print_debug_msg("\tN/A\n");
}

#ifdef ACL_DEBUG
void acl_dump_mem(cl_mem mem) { acl_dump_mem_internal(mem); }
#endif

static void acl_dump_mem_internal(cl_mem mem) {
  acl_assert_locked();

  if (debug_mode > 0) {
    printf("           Mem[%p] = {\n", mem);
    printf("              .refcnt             %d\n", acl_ref_count(mem));
    printf("              .flags              0x%x\n",
           (unsigned int)mem->flags);
    printf("              %s \n",
           (mem->writable_copy_on_host ? "writable copy on host"
                                       : "writable copy on device"));
    if (mem->block_allocation != NULL) {
      printf("              .region             %p\n",
             mem->block_allocation->region);
      printf("              %s \n",
             (mem->block_allocation->region->is_user_provided
                  ? "user provided"
                  : "not user provided"));
      printf("              %s \n",
             (mem->block_allocation->region->is_host_accessible
                  ? "host accessible"
                  : "not host accessible"));
      printf("              %s \n",
             (mem->block_allocation->region->is_device_accessible
                  ? "device accessible"
                  : "not device accessible"));
      printf("              %s \n",
             (mem->block_allocation->region->uses_host_system_malloc
                  ? "is malloc"
                  : "not malloc"));
      printf("              .begin             %p\n",
             mem->block_allocation->range.begin);
      printf("              .end               %p\n",
             mem->block_allocation->range.next);
    }
    printf("              .mappings          %d\n", mem->mapping_count);
    acl_print_debug_msg("              .size              %lu\n", mem->size);
    printf("              .host_ptr          %p\n",
           mem->fields.buffer_objs.host_ptr);
    printf("              .host_mem.aligned_ptr  %p\n",
           mem->host_mem.aligned_ptr);
    printf("              .host_mem.raw      %p\n", mem->host_mem.raw);
    printf("           }\n");
  }
}

size_t acl_get_image_element_size(cl_context context,
                                  const cl_image_format *image_format,
                                  cl_int *errcode_ret) {
  size_t channel_data_type_size;
  cl_bool multiply_by_channel_order;
  size_t channel_order_size;
  acl_assert_locked();

  switch (image_format->image_channel_data_type) {
  case CL_SNORM_INT8:
  case CL_SIGNED_INT8:
    channel_data_type_size = 1;
    multiply_by_channel_order = CL_TRUE;
    break;
  case CL_SNORM_INT16:
  case CL_SIGNED_INT16:
    channel_data_type_size = 2;
    multiply_by_channel_order = CL_TRUE;
    break;
  case CL_SIGNED_INT32:
    channel_data_type_size = 4;
    multiply_by_channel_order = CL_TRUE;
    break;
  case CL_UNORM_INT8:
  case CL_UNSIGNED_INT8:
    channel_data_type_size = 1;
    multiply_by_channel_order = CL_TRUE;
    break;
  case CL_UNORM_INT16:
  case CL_UNSIGNED_INT16:
    channel_data_type_size = 2;
    multiply_by_channel_order = CL_TRUE;
    break;
  case CL_UNSIGNED_INT32:
    channel_data_type_size = 4;
    multiply_by_channel_order = CL_TRUE;
    break;
  case CL_HALF_FLOAT:
    channel_data_type_size = sizeof(float) / 2;
    multiply_by_channel_order = CL_TRUE;
    break;
  case CL_FLOAT:
    channel_data_type_size = sizeof(float);
    multiply_by_channel_order = CL_TRUE;
    break;
  case CL_UNORM_SHORT_565:
    channel_data_type_size = 2;
    multiply_by_channel_order = CL_FALSE;
    break;
  case CL_UNORM_SHORT_555:
    channel_data_type_size = 2;
    multiply_by_channel_order = CL_FALSE;
    break;
  case CL_UNORM_INT_101010:
    channel_data_type_size = 4;
    multiply_by_channel_order = CL_FALSE;
    break;
  default:
    BAIL_INFO(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR, context,
              "image_channel_data_type not valid");
  }

  switch (image_format->image_channel_order) {
  case CL_R:
  case CL_A:
    channel_order_size = 1;
    break;
  case CL_INTENSITY:
  case CL_LUMINANCE:
    if (image_format->image_channel_data_type != CL_UNORM_INT8 &&
        image_format->image_channel_data_type != CL_UNORM_INT16 &&
        image_format->image_channel_data_type != CL_SNORM_INT8 &&
        image_format->image_channel_data_type != CL_SNORM_INT16 &&
        image_format->image_channel_data_type != CL_FLOAT &&
        image_format->image_channel_data_type != CL_HALF_FLOAT) {
      BAIL_INFO(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR, context,
                "Combination of image_channel_order & image_channel_data_type "
                "not supported");
    }
    channel_order_size = 1;
    break;
  case CL_RG:
  case CL_RA:
    channel_order_size = 2;
    break;
  case CL_RGB:
    channel_order_size = 3;
    if (image_format->image_channel_data_type != CL_UNORM_SHORT_565 &&
        image_format->image_channel_data_type != CL_UNORM_SHORT_555 &&
        image_format->image_channel_data_type != CL_UNORM_INT_101010) {
      BAIL_INFO(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR, context,
                "Combination of image_channel_order & image_channel_data_type "
                "not supported");
    }
    break;
  case CL_RGBA:
    channel_order_size = 4;
    break;
  case CL_ARGB:
  case CL_BGRA:
    channel_order_size = 4;
    if (image_format->image_channel_data_type != CL_UNORM_INT8 &&
        image_format->image_channel_data_type != CL_SNORM_INT8 &&
        image_format->image_channel_data_type != CL_SIGNED_INT8 &&
        image_format->image_channel_data_type != CL_UNSIGNED_INT8) {
      BAIL_INFO(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR, context,
                "Combination of image_channel_order & image_channel_data_type "
                "not supported");
    }
    break;
  default: {
    std::stringstream ss;
    ss << "image_channel_order 0x" << std::hex
       << image_format->image_channel_order << " not valid";
    BAIL_INFO(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR, context, ss.str().c_str());
  }
  }
  if (!multiply_by_channel_order) {
    channel_order_size = 1;
  }
  if (errcode_ret) {
    *errcode_ret = CL_SUCCESS;
  }

  return channel_data_type_size * channel_order_size;
}

size_t get_offset_for_image_param(cl_context context,
                                  cl_mem_object_type mem_object_type,
                                  const char *name) {
  acl_assert_locked();

  switch (mem_object_type) {
  case CL_MEM_OBJECT_IMAGE2D:
    if (strncmp(name, "width", MAX_NAME_SIZE) == 0) {
      return 0;
    } else if (strncmp(name, "height", MAX_NAME_SIZE) == 0) {
      return 4;
    } else if (strncmp(name, "channel_data_type", MAX_NAME_SIZE) == 0) {
      return 8;
    } else if (strncmp(name, "channel_order", MAX_NAME_SIZE) == 0) {
      return 12;
    } else if (strncmp(name, "element_size", MAX_NAME_SIZE) == 0) {
      return 16;
    } else if (strncmp(name, "data", MAX_NAME_SIZE) == 0) {
      // Check that the aligned location is at least as big
      // as the minimum location we can fit data
      assert(24 <= ACL_MEM_ALIGN);
      return ACL_MEM_ALIGN;
    } else {
      acl_context_callback(context, "Invalid or unsupported image field");
      return 0;
    }
    break;
  case CL_MEM_OBJECT_IMAGE3D:
    if (strncmp(name, "width", MAX_NAME_SIZE) == 0) {
      return 0;
    } else if (strncmp(name, "height", MAX_NAME_SIZE) == 0) {
      return 4;
    } else if (strncmp(name, "depth", MAX_NAME_SIZE) == 0) {
      return 8;
    } else if (strncmp(name, "channel_data_type", MAX_NAME_SIZE) == 0) {
      return 12;
    } else if (strncmp(name, "channel_order", MAX_NAME_SIZE) == 0) {
      return 16;
    } else if (strncmp(name, "element_size", MAX_NAME_SIZE) == 0) {
      return 24;
    } else if (strncmp(name, "data", MAX_NAME_SIZE) == 0) {
      // Check that the aligned location is at least as big
      // as the minimum location we can fit data
      assert(32 <= ACL_MEM_ALIGN);
      return ACL_MEM_ALIGN;
    } else {
      acl_context_callback(context, "Invalid or unsupported image field");
      return 0;
    }
    break;
  case CL_MEM_OBJECT_IMAGE2D_ARRAY:
    if (strncmp(name, "width", MAX_NAME_SIZE) == 0) {
      return 0;
    } else if (strncmp(name, "height", MAX_NAME_SIZE) == 0) {
      return 4;
    } else if (strncmp(name, "array_size", MAX_NAME_SIZE) == 0) {
      return 8;
    } else if (strncmp(name, "channel_data_type", MAX_NAME_SIZE) == 0) {
      return 12;
    } else if (strncmp(name, "channel_order", MAX_NAME_SIZE) == 0) {
      return 16;
    } else if (strncmp(name, "element_size", MAX_NAME_SIZE) == 0) {
      return 24;
    } else if (strncmp(name, "data", MAX_NAME_SIZE) == 0) {
      // Check that the aligned location is at least as big
      // as the minimum location we can fit data
      assert(32 <= ACL_MEM_ALIGN);
      return ACL_MEM_ALIGN;
    } else {
      acl_context_callback(context, "Invalid or unsupported image field");
      return 0;
    }
    break;
  case CL_MEM_OBJECT_IMAGE1D:
    if (strncmp(name, "width", MAX_NAME_SIZE) == 0) {
      return 0;
    } else if (strncmp(name, "channel_data_type", MAX_NAME_SIZE) == 0) {
      return 12;
    } else if (strncmp(name, "channel_order", MAX_NAME_SIZE) == 0) {
      return 16;
    } else if (strncmp(name, "element_size", MAX_NAME_SIZE) == 0) {
      return 24;
    } else if (strncmp(name, "data", MAX_NAME_SIZE) == 0) {
      // Check that the aligned location is at least as big
      // as the minimum location we can fit data
      assert(32 <= ACL_MEM_ALIGN);
      return ACL_MEM_ALIGN;
    } else {
      acl_context_callback(context, "Invalid or unsupported image field");
      return 0;
    }
    break;
  case CL_MEM_OBJECT_IMAGE1D_ARRAY:
    if (strncmp(name, "width", MAX_NAME_SIZE) == 0) {
      return 0;
    } else if (strncmp(name, "array_size", MAX_NAME_SIZE) == 0) {
      return 8;
    } else if (strncmp(name, "channel_data_type", MAX_NAME_SIZE) == 0) {
      return 12;
    } else if (strncmp(name, "channel_order", MAX_NAME_SIZE) == 0) {
      return 16;
    } else if (strncmp(name, "element_size", MAX_NAME_SIZE) == 0) {
      return 24;
    } else if (strncmp(name, "data", MAX_NAME_SIZE) == 0) {
      // Check that the aligned location is at least as big
      // as the minimum location we can fit data
      assert(32 <= ACL_MEM_ALIGN);
      return ACL_MEM_ALIGN;
    } else {
      acl_context_callback(context, "Invalid or unsupported image field");
      return 0;
    }
    break;
  case CL_MEM_OBJECT_IMAGE1D_BUFFER:
    if (strncmp(name, "width", MAX_NAME_SIZE) == 0) {
      return 0;
    } else if (strncmp(name, "channel_data_type", MAX_NAME_SIZE) == 0) {
      return 12;
    } else if (strncmp(name, "channel_order", MAX_NAME_SIZE) == 0) {
      return 16;
    } else if (strncmp(name, "element_size", MAX_NAME_SIZE) == 0) {
      return 24;
    } else if (strncmp(name, "data", MAX_NAME_SIZE) == 0) {
      // Check that the aligned location is at least as big
      // as the minimum location we can fit data
      assert(32 <= ACL_MEM_ALIGN);
      return ACL_MEM_ALIGN;
    } else {
      acl_context_callback(context, "Invalid or unsupported image field");
      return 0;
    }
    break;
  default:
    acl_context_callback(context, "Invalid image type used");
    return 0;
    break;
  }
}

int acl_submit_migrate_mem_device_op(cl_event event) {
  int result = 0;
  if (!acl_event_is_valid(event)) {
    return result;
  }
  if (!acl_command_queue_is_valid(event->command_queue)) {
    return result;
  }
  // No user-level scheduling blocks this memory transfer.
  // So submit it to the device op queue.
  // But only if it isn't already enqueued there.
  if (!event->last_device_op &&
      event->cmd.type == CL_COMMAND_MIGRATE_MEM_OBJECTS) {
    unsigned int ibuf;
    int ok = 1;
    acl_mem_migrate_t memory_migration = event->cmd.info.memory_migration;
    acl_device_op_queue_t *doq = &(acl_platform.device_op_queue);
    acl_device_op_t *last_op = 0;

    // Precautionary, but it also nudges the device scheduler to try
    // to free up old operation slots.
    acl_forget_proposed_device_ops(doq);

    // Arrange for mem migration
    for (ibuf = 0; ok && ibuf < memory_migration.num_mem_objects; ibuf++) {
      acl_device_op_t *last_op2 = acl_propose_indexed_device_op(
          doq, ACL_DEVICE_OP_MEM_MIGRATION, event, ibuf);
      ok = (last_op2 != NULL);
    }

    if (ok) {
      // We managed to enqueue everything.
      event->last_device_op = last_op;
      acl_commit_proposed_device_ops(doq);
      result = 1;
    } else {
      // Back off, and wait until later when we have more space in the
      // device op queue.
      acl_forget_proposed_device_ops(doq);
    }
  }
  return result;
}

// This function takes an image element with a cl_image_format format_from and
// outputs an image element with cl_image_format format_to. The implementation
// does not cover all of the cases (yet), as it is written to only cover the use
// cases by clEnqueueFillImage and currently supported image formats. It is
// straight forward to add other cases, as per needed.
cl_int acl_convert_image_format(const void *input_element, void *output_element,
                                cl_image_format format_from,
                                cl_image_format format_to) {
  cl_int status = 0;
  if (format_from.image_channel_order == CL_RGBA) {
    switch (format_to.image_channel_order) {
    case CL_R:
      switch (format_from.image_channel_data_type) {
      case CL_FLOAT:
        switch (format_to.image_channel_data_type) {
        case CL_SNORM_INT8: // { CL_RGBA, CL_FLOAT } ->  { CL_R, CL_SNORM_INT8 }
          ((cl_char *)output_element)[0] =
              (cl_char)(((cl_float *)input_element)[0] * 127.0f);
          break;
        case CL_SNORM_INT16: // { CL_RGBA, CL_FLOAT } ->  { CL_R, CL_SNORM_INT16
                             // }
          ((cl_short *)output_element)[0] =
              (cl_short)(((cl_float *)input_element)[0] * 32767.0f);
          break;
        case CL_UNORM_INT8: // { CL_RGBA, CL_FLOAT } ->  { CL_R, CL_UNORM_INT8 }
          ((cl_uchar *)output_element)[0] =
              (cl_uchar)(((cl_float *)input_element)[0] * 255.0f);
          break;
        case CL_UNORM_INT16: // { CL_RGBA, CL_FLOAT } ->  { CL_R, CL_UNORM_INT16
                             // }
          ((cl_ushort *)output_element)[0] =
              (cl_ushort)(((cl_float *)input_element)[0] * 65535.0f);
          break;
        case CL_FLOAT: // { CL_RGBA, CL_FLOAT } ->  { CL_R, CL_FLOAT }
          ((cl_float *)output_element)[0] =
              (cl_float)(((cl_float *)input_element)[0]);
          break;
        default:
          status = -1;
        } // 3rd switch (format_to.image_channel_data_type)
        break;

      case CL_SIGNED_INT32:
        switch (format_to.image_channel_data_type) {
        case CL_SIGNED_INT8: // { CL_RGBA, CL_SIGNED_INT32 } ->  { CL_R,
                             // CL_SIGNED_INT8 }
          ((cl_char *)output_element)[0] =
              (cl_char)(((cl_int *)input_element)[0] & 0xFF);
          break;
        case CL_SIGNED_INT16: // { CL_RGBA, CL_SIGNED_INT32 } ->  { CL_R,
                              // CL_SIGNED_INT16 }
          ((cl_short *)output_element)[0] =
              (cl_short)(((cl_int *)input_element)[0] & 0xFFFF);
          break;
        case CL_SIGNED_INT32: // { CL_RGBA, CL_SIGNED_INT32 } ->  { CL_R,
                              // CL_SIGNED_INT32 }
          ((cl_int *)output_element)[0] =
              (cl_int)(((cl_int *)input_element)[0]);
          break;
        default:
          status = -1;
        } // 3rd switch (format_to.image_channel_data_type)
        break;

      case CL_UNSIGNED_INT32:
        switch (format_to.image_channel_data_type) {
        case CL_UNSIGNED_INT8: // { CL_RGBA, CL_UNSIGNED_INT32 } ->  { CL_R,
                               // CL_UNSIGNED_INT8 }
          ((cl_uchar *)output_element)[0] =
              (cl_uchar)(((cl_uint *)input_element)[0] & 0xFF);
          break;
        case CL_UNSIGNED_INT16: // { CL_RGBA, CL_UNSIGNED_INT32 } ->  { CL_R,
                                // CL_UNSIGNED_INT16 }
          ((cl_ushort *)output_element)[0] =
              (cl_ushort)(((cl_uint *)input_element)[0] & 0xFFFF);
          break;
        case CL_UNSIGNED_INT32: // { CL_RGBA, CL_UNSIGNED_INT32 } ->  { CL_R,
                                // CL_UNSIGNED_INT32 }
          ((cl_uint *)output_element)[0] =
              (cl_uint)(((cl_uint *)input_element)[0]);
          break;
        default:
          status = -1;
        } // 3rd switch (format_to.image_channel_data_type)
        break;

      default:
        status = -1; // Returning -1 for conversions not supported (yet).
      }              // 2nd switch (format_from.image_channel_data_type)
      break;
    //**********************************************
    case CL_RG:
      switch (format_from.image_channel_data_type) {
      case CL_FLOAT:
        switch (format_to.image_channel_data_type) {
        case CL_SNORM_INT8: // { CL_RGBA, CL_FLOAT } ->  { CL_RG, CL_SNORM_INT8
                            // }
          ((cl_char *)output_element)[0] =
              (cl_char)(((cl_float *)input_element)[0] * 127.0f);
          ((cl_char *)output_element)[1] =
              (cl_char)(((cl_float *)input_element)[1] * 127.0f);
          break;
        case CL_SNORM_INT16: // { CL_RGBA, CL_FLOAT } ->  { CL_RG,
                             // CL_SNORM_INT16 }
          ((cl_short *)output_element)[0] =
              (cl_short)(((cl_float *)input_element)[0] * 32767.0f);
          ((cl_short *)output_element)[1] =
              (cl_short)(((cl_float *)input_element)[1] * 32767.0f);
          break;
        case CL_UNORM_INT8: // { CL_RGBA, CL_FLOAT } ->  { CL_RG, CL_UNORM_INT8
                            // }
          ((cl_uchar *)output_element)[0] =
              (cl_uchar)(((cl_float *)input_element)[0] * 255.0f);
          ((cl_uchar *)output_element)[1] =
              (cl_uchar)(((cl_float *)input_element)[1] * 255.0f);
          break;
        case CL_UNORM_INT16: // { CL_RGBA, CL_FLOAT } ->  { CL_RG,
                             // CL_UNORM_INT16 }
          ((cl_ushort *)output_element)[0] =
              (cl_ushort)(((cl_float *)input_element)[0] * 65535.0f);
          ((cl_ushort *)output_element)[1] =
              (cl_ushort)(((cl_float *)input_element)[1] * 65535.0f);
          break;
        case CL_FLOAT: // { CL_RGBA, CL_FLOAT } ->  { CL_RG, CL_FLOAT }
          ((cl_float *)output_element)[0] =
              (cl_float)(((cl_float *)input_element)[0]);
          ((cl_float *)output_element)[1] =
              (cl_float)(((cl_float *)input_element)[1]);
          break;
        default:
          status = -1;
        } // 3rd switch (format_to.image_channel_data_type)
        break;

      case CL_SIGNED_INT32:
        switch (format_to.image_channel_data_type) {
        case CL_SIGNED_INT8: // { CL_RGBA, CL_SIGNED_INT32 } ->  { CL_RG,
                             // CL_SIGNED_INT8 }
          ((cl_char *)output_element)[0] =
              (cl_char)(((cl_int *)input_element)[0] & 0xFF);
          ((cl_char *)output_element)[1] =
              (cl_char)(((cl_int *)input_element)[1] & 0xFF);
          break;
        case CL_SIGNED_INT16: // { CL_RGBA, CL_SIGNED_INT32 } ->  { CL_RG,
                              // CL_SIGNED_INT16 }
          ((cl_short *)output_element)[0] =
              (cl_short)(((cl_int *)input_element)[0] & 0xFFFF);
          ((cl_short *)output_element)[1] =
              (cl_short)(((cl_int *)input_element)[1] & 0xFFFF);
          break;
        case CL_SIGNED_INT32: // { CL_RGBA, CL_SIGNED_INT32 } ->  { CL_RG,
                              // CL_SIGNED_INT32 }
          ((cl_int *)output_element)[0] =
              (cl_int)(((cl_int *)input_element)[0]);
          ((cl_int *)output_element)[1] =
              (cl_int)(((cl_int *)input_element)[1]);
          break;
        default:
          status = -1;
        } // 3rd switch (format_to.image_channel_data_type)
        break;

      case CL_UNSIGNED_INT32:
        switch (format_to.image_channel_data_type) {
        case CL_UNSIGNED_INT8: // { CL_RGBA, CL_UNSIGNED_INT32 } ->  { CL_RG,
                               // CL_UNSIGNED_INT8 }
          ((cl_uchar *)output_element)[0] =
              (cl_uchar)(((cl_uint *)input_element)[0] & 0xFF);
          ((cl_uchar *)output_element)[1] =
              (cl_uchar)(((cl_uint *)input_element)[1] & 0xFF);
          break;
        case CL_UNSIGNED_INT16: // { CL_RGBA, CL_UNSIGNED_INT32 } ->  { CL_RG,
                                // CL_UNSIGNED_INT16 }
          ((cl_ushort *)output_element)[0] =
              (cl_ushort)(((cl_uint *)input_element)[0] & 0xFFFF);
          ((cl_ushort *)output_element)[1] =
              (cl_ushort)(((cl_uint *)input_element)[1] & 0xFFFF);
          break;
        case CL_UNSIGNED_INT32: // { CL_RGBA, CL_UNSIGNED_INT32 } ->  { CL_RG,
                                // CL_UNSIGNED_INT32 }
          ((cl_uint *)output_element)[0] =
              (cl_uint)(((cl_uint *)input_element)[0]);
          ((cl_uint *)output_element)[1] =
              (cl_uint)(((cl_uint *)input_element)[1]);
          break;
        default:
          status = -1;
        } // 3rd switch (format_to.image_channel_data_type)
        break;

      default:
        status = -1; // Returning -1 for conversions not supported (yet).
      }              // 2nd switch
      break;
    //**********************************************
    case CL_RGBA:
      switch (format_from.image_channel_data_type) {
      case CL_FLOAT:
        switch (format_to.image_channel_data_type) {
        case CL_SNORM_INT8: // { CL_RGBA, CL_FLOAT } ->  { CL_RGBA,
                            // CL_SNORM_INT8 }
          ((cl_char *)output_element)[0] =
              (cl_char)(((cl_float *)input_element)[0] * 127.0f);
          ((cl_char *)output_element)[1] =
              (cl_char)(((cl_float *)input_element)[1] * 127.0f);
          ((cl_char *)output_element)[2] =
              (cl_char)(((cl_float *)input_element)[2] * 127.0f);
          ((cl_char *)output_element)[3] =
              (cl_char)(((cl_float *)input_element)[3] * 127.0f);
          break;
        case CL_SNORM_INT16: // { CL_RGBA, CL_FLOAT } ->  { CL_RGBA,
                             // CL_SNORM_INT16 }
          ((cl_short *)output_element)[0] =
              (cl_short)(((cl_float *)input_element)[0] * 32767.0f);
          ((cl_short *)output_element)[1] =
              (cl_short)(((cl_float *)input_element)[1] * 32767.0f);
          ((cl_short *)output_element)[2] =
              (cl_short)(((cl_float *)input_element)[2] * 32767.0f);
          ((cl_short *)output_element)[3] =
              (cl_short)(((cl_float *)input_element)[3] * 32767.0f);
          break;
        case CL_UNORM_INT8: // { CL_RGBA, CL_FLOAT } ->  { CL_RGBA,
                            // CL_UNORM_INT8 }
          ((cl_uchar *)output_element)[0] =
              (cl_uchar)(((cl_float *)input_element)[0] * 255.0f);
          ((cl_uchar *)output_element)[1] =
              (cl_uchar)(((cl_float *)input_element)[1] * 255.0f);
          ((cl_uchar *)output_element)[2] =
              (cl_uchar)(((cl_float *)input_element)[2] * 255.0f);
          ((cl_uchar *)output_element)[3] =
              (cl_uchar)(((cl_float *)input_element)[3] * 255.0f);
          break;
        case CL_UNORM_INT16: // { CL_RGBA, CL_FLOAT } ->  { CL_RGBA,
                             // CL_UNORM_INT16 }
          ((cl_ushort *)output_element)[0] =
              (cl_ushort)(((cl_float *)input_element)[0] * 65535.0f);
          ((cl_ushort *)output_element)[1] =
              (cl_ushort)(((cl_float *)input_element)[1] * 65535.0f);
          ((cl_ushort *)output_element)[2] =
              (cl_ushort)(((cl_float *)input_element)[2] * 65535.0f);
          ((cl_ushort *)output_element)[3] =
              (cl_ushort)(((cl_float *)input_element)[3] * 65535.0f);
          break;
        case CL_FLOAT: // { CL_RGBA, CL_FLOAT } ->  { CL_RGBA, CL_FLOAT }
          ((cl_float *)output_element)[0] =
              (cl_float)(((cl_float *)input_element)[0]);
          ((cl_float *)output_element)[1] =
              (cl_float)(((cl_float *)input_element)[1]);
          ((cl_float *)output_element)[2] =
              (cl_float)(((cl_float *)input_element)[2]);
          ((cl_float *)output_element)[3] =
              (cl_float)(((cl_float *)input_element)[3]);
          break;
        default:
          status = -1;
        } // 3rd switch (format_to.image_channel_data_type)
        break;

      case CL_SIGNED_INT32:
        switch (format_to.image_channel_data_type) {
        case CL_SIGNED_INT8: // { CL_RGBA, CL_SIGNED_INT32 } ->  { CL_RGBA,
                             // CL_SIGNED_INT8 }
          ((cl_char *)output_element)[0] =
              (cl_char)(((cl_int *)input_element)[0] & 0xFF);
          ((cl_char *)output_element)[1] =
              (cl_char)(((cl_int *)input_element)[1] & 0xFF);
          ((cl_char *)output_element)[2] =
              (cl_char)(((cl_int *)input_element)[2] & 0xFF);
          ((cl_char *)output_element)[3] =
              (cl_char)(((cl_int *)input_element)[3] & 0xFF);
          break;
        case CL_SIGNED_INT16: // { CL_RGBA, CL_SIGNED_INT32 } ->  { CL_RGBA,
                              // CL_SIGNED_INT16 }
          ((cl_short *)output_element)[0] =
              (cl_short)(((cl_int *)input_element)[0] & 0xFFFF);
          ((cl_short *)output_element)[1] =
              (cl_short)(((cl_int *)input_element)[1] & 0xFFFF);
          ((cl_short *)output_element)[2] =
              (cl_short)(((cl_int *)input_element)[2] & 0xFFFF);
          ((cl_short *)output_element)[3] =
              (cl_short)(((cl_int *)input_element)[3] & 0xFFFF);
          break;
        case CL_SIGNED_INT32: // { CL_RGBA, CL_SIGNED_INT32 } ->  { CL_RGBA,
                              // CL_SIGNED_INT32 }
          ((cl_int *)output_element)[0] =
              (cl_int)(((cl_int *)input_element)[0]);
          ((cl_int *)output_element)[1] =
              (cl_int)(((cl_int *)input_element)[1]);
          ((cl_int *)output_element)[2] =
              (cl_int)(((cl_int *)input_element)[2]);
          ((cl_int *)output_element)[3] =
              (cl_int)(((cl_int *)input_element)[3]);
          break;
        default:
          status = -1;
        } // 3rd switch (format_to.image_channel_data_type)
        break;

      case CL_UNSIGNED_INT32:
        switch (format_to.image_channel_data_type) {
        case CL_UNSIGNED_INT8: // { CL_RGBA, CL_UNSIGNED_INT32 } ->  { CL_RGBA,
                               // CL_UNSIGNED_INT8 }
          ((cl_uchar *)output_element)[0] =
              (cl_uchar)(((cl_uint *)input_element)[0] & 0xFF);
          ((cl_uchar *)output_element)[1] =
              (cl_uchar)(((cl_uint *)input_element)[1] & 0xFF);
          ((cl_uchar *)output_element)[2] =
              (cl_uchar)(((cl_uint *)input_element)[2] & 0xFF);
          ((cl_uchar *)output_element)[3] =
              (cl_uchar)(((cl_uint *)input_element)[3] & 0xFF);
          break;
        case CL_UNSIGNED_INT16: // { CL_RGBA, CL_UNSIGNED_INT32 } ->  { CL_RGBA,
                                // CL_UNSIGNED_INT16 }
          ((cl_ushort *)output_element)[0] =
              (cl_ushort)(((cl_uint *)input_element)[0] & 0xFFFF);
          ((cl_ushort *)output_element)[1] =
              (cl_ushort)(((cl_uint *)input_element)[1] & 0xFFFF);
          ((cl_ushort *)output_element)[2] =
              (cl_ushort)(((cl_uint *)input_element)[2] & 0xFFFF);
          ((cl_ushort *)output_element)[3] =
              (cl_ushort)(((cl_uint *)input_element)[3] & 0xFFFF);
          break;
        case CL_UNSIGNED_INT32: // { CL_RGBA, CL_UNSIGNED_INT32 } ->  { CL_RGBA,
                                // CL_UNSIGNED_INT32 }
          ((cl_uint *)output_element)[0] =
              (cl_uint)(((cl_uint *)input_element)[0]);
          ((cl_uint *)output_element)[1] =
              (cl_uint)(((cl_uint *)input_element)[1]);
          ((cl_uint *)output_element)[2] =
              (cl_uint)(((cl_uint *)input_element)[2]);
          ((cl_uint *)output_element)[3] =
              (cl_uint)(((cl_uint *)input_element)[3]);
          break;
        default:
          status = -1;
        } // 3rd switch (format_to.image_channel_data_type)
        break;

      default:
        status = -1; // Returning -1 for conversions not supported (yet).
      }              // 2nd switch
      break;
    //**********************************************
    case CL_BGRA:
      switch (format_from.image_channel_data_type) {
      case CL_FLOAT:
        switch (format_to.image_channel_data_type) {
        case CL_UNORM_INT8: // { CL_RGBA, CL_FLOAT } ->  { CL_BGRA,
                            // CL_UNORM_INT8 }
          ((cl_uchar *)output_element)[0] =
              (cl_uchar)(((cl_float *)input_element)[2] * 255.0f);
          ((cl_uchar *)output_element)[1] =
              (cl_uchar)(((cl_float *)input_element)[1] * 255.0f);
          ((cl_uchar *)output_element)[2] =
              (cl_uchar)(((cl_float *)input_element)[0] * 255.0f);
          ((cl_uchar *)output_element)[3] =
              (cl_uchar)(((cl_float *)input_element)[3] * 255.0f);
          break;
        default:
          status = -1;
        } // 3rd switch (format_to.image_channel_data_type)
        break;
      default:
        status = -1;
      } // 2nd switch (format_from.image_channel_data_type)
      break;
    default:
      status = -1;
    } // 1st switch (format_to.image_channel_order)
  }   // if (format_from.image_channel_order == CL_RGBA)
  else {
    status = -1; // Returning -1 for conversions not supported (yet).
  }
  return status;
}

void *acl_get_physical_address(cl_mem mem, cl_device_id device) {
  assert(!mem->writable_copy_on_host && "Writable copy is not on device");
  return l_get_address_of_writable_copy(mem, device->def.physical_device_id,
                                        NULL, CL_FALSE);
}

// Submit an op to the device op queue to read device global.
// Return 1 if we made forward progress, 0 otherwise.
cl_int acl_submit_read_device_global_device_op(cl_event event) {
  acl_print_debug_msg(
      "Entering acl_submit_read_device_global_device_op function\n");
  int result = 0;
  acl_assert_locked();

  // No user-level scheduling blocks this device global read
  // So submit it to the device op queue.
  // But only if it isn't already enqueued there.
  if (!acl_event_is_valid(event)) {
    return result;
  }
  // Already enqueued.
  if (event->last_device_op) {
    return result;
  }

  acl_device_op_queue_t *doq = &(acl_platform.device_op_queue);
  acl_device_op_t *last_op = 0;

  // Precautionary, but it also nudges the device scheduler to try
  // to free up old operation slots.
  acl_forget_proposed_device_ops(doq);

  last_op = acl_propose_device_op(doq, ACL_DEVICE_OP_DEVICE_GLOBAL_READ, event);

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
  acl_print_debug_msg(
      "Exiting acl_submit_read_device_global_device_op function\n");
  return result;
}

// Submit a device global write device operation to the device op queue
cl_int acl_submit_write_device_global_device_op(cl_event event) {
  acl_print_debug_msg(
      "Entering acl_submit_write_device_global_device_op function\n");
  int result = 0;
  acl_assert_locked();

  // No user-level scheduling blocks this device global write
  // So submit it to the device op queue.
  // But only if it isn't already enqueued there.
  if (!acl_event_is_valid(event)) {
    return result;
  }
  // Already enqueued.
  if (event->last_device_op) {
    return result;
  }

  acl_device_op_queue_t *doq = &(acl_platform.device_op_queue);
  acl_device_op_t *last_op = 0;

  // Precautionary, but it also nudges the device scheduler to try
  // to free up old operation slots.
  acl_forget_proposed_device_ops(doq);

  last_op =
      acl_propose_device_op(doq, ACL_DEVICE_OP_DEVICE_GLOBAL_WRITE, event);

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
  acl_print_debug_msg(
      "Exiting acl_submit_write_device_global_device_op function\n");
  return result;
}

// Read from a device global
void acl_read_device_global(void *user_data, acl_device_op_t *op) {
  acl_print_debug_msg("Entering acl_read_device_global function\n");
  cl_event event = op->info.event;
  cl_int status = 0;

  acl_assert_locked();

  if (!acl_event_is_valid(event) ||
      !acl_command_queue_is_valid(event->command_queue)) {
    acl_set_device_op_execution_status(op, -1);
    return;
  }

  acl_set_device_op_execution_status(op, CL_SUBMITTED);
  acl_set_device_op_execution_status(op, CL_RUNNING);

  status = acl_get_hal()->simulation_device_global_interface_read(
      event->cmd.info.device_global_info.physical_device_id,
      event->cmd.info.device_global_info.name,
      event->cmd.info.device_global_info.read_ptr,
      (size_t)event->cmd.info.device_global_info.device_global_addr +
          event->cmd.info.device_global_info.offset,
      event->cmd.info.device_global_info.size);
  if (status == 0) {
    acl_set_device_op_execution_status(op, CL_COMPLETE);
  } else {
    acl_set_device_op_execution_status(op, -1);
  }
  acl_print_debug_msg("Exiting acl_read_device_global function\n");
}

// Write into a device global
void acl_write_device_global(void *user_data, acl_device_op_t *op) {

  acl_print_debug_msg("Entering acl_write_device_global function\n");
  cl_event event = op->info.event;
  cl_int status = 0;

  acl_assert_locked();

  if (!acl_event_is_valid(event) ||
      !acl_command_queue_is_valid(event->command_queue)) {
    acl_set_device_op_execution_status(op, -1);
    return;
  }

  acl_set_device_op_execution_status(op, CL_SUBMITTED);
  acl_set_device_op_execution_status(op, CL_RUNNING);

  status = acl_get_hal()->simulation_device_global_interface_write(
      event->cmd.info.device_global_info.physical_device_id,
      event->cmd.info.device_global_info.name,
      event->cmd.info.device_global_info.write_ptr,
      (size_t)event->cmd.info.device_global_info.device_global_addr +
          event->cmd.info.device_global_info.offset,
      event->cmd.info.device_global_info.size);

  if (status == 0) {
    acl_set_device_op_execution_status(op, CL_COMPLETE);
  } else {
    acl_set_device_op_execution_status(op, -1);
  }
  acl_print_debug_msg("Exiting acl_write_device_global function\n");
}

#ifdef __GNUC__
#pragma GCC visibility pop
#endif
