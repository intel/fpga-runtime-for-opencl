// Copyright (C) 2014-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// System headers.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// External library headers.
#include <CL/opencl.h>

// Internal headers.
#include <acl.h>
#include <acl_context.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_svm.h>
#include <acl_util.h>

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

//////////////////////////////
// OpenCL API

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL clSVMAllocIntelFPGA(cl_context context,
                                                   cl_svm_mem_flags flags,
                                                   size_t size,
                                                   unsigned int alignment) {
  void *result = NULL;
  acl_svm_entry_t *svm_entry;
  int num_rw_specs = 0;

#ifdef __linux__
  int mem_result;
#endif
#ifndef SYSTEM_SVM
  unsigned int idevice;
  // this context supports SVM
  cl_bool context_has_svm;
#endif
  std::scoped_lock lock{acl_mutex_wrapper};

  // Valid context
#ifndef REMOVE_VALID_CHECKS

  if (!acl_context_is_valid(context))
    return NULL;

  // Check for invalid enum bits
  if (flags & ~(CL_MEM_READ_WRITE | CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY |
                CL_MEM_SVM_FINE_GRAIN_BUFFER | CL_MEM_SVM_ATOMICS)) {
    return NULL;
  }

  // Check for exactly one read/write spec
  if (flags & CL_MEM_READ_WRITE)
    num_rw_specs++;
  if (flags & CL_MEM_READ_ONLY)
    num_rw_specs++;
  if (flags & CL_MEM_WRITE_ONLY)
    num_rw_specs++;
  // Default to CL_MEM_READ_WRITE.
  if (num_rw_specs > 1)
    return NULL;
  if (num_rw_specs == 0)
    flags |= CL_MEM_READ_WRITE;

  // Cannot specify SVM atomics without fine grain
  if ((flags & CL_MEM_SVM_ATOMICS) && !(flags & CL_MEM_SVM_FINE_GRAIN_BUFFER)) {
    return NULL;
  }

  // If SVM atomics specified, check if any device in context supports SVM
  // atomics Right now though, we don't support SVM atomics so just return NULL
  if (flags & CL_MEM_SVM_ATOMICS) {
    return NULL;
  }

  // If fine grain specified, check if any device in context supports fine grain
  // Right now though, we don't support SVM fine grain so just return NULL
  if (flags & CL_MEM_SVM_FINE_GRAIN_BUFFER) {
    return NULL;
  }

  // size is 0 or > CL_DEVICE_MAX_MEM_ALLOC_SIZE value for any device in context
  if (size == 0)
    return NULL;
  if (size > context->max_mem_alloc_size) {
    return NULL;
  }

  // alignment is not a power of two or the OpenCL implementation cannot support
  // the specified alignment for at least one device in context. For now, we
  // only allow alignment to ACL_MEM_ALIGN. If we ever change this, we also need
  // to update the alignment check in clSetKernelArgSVMPointerIntelFPGA.
  // Alignment of '0' means use the default
  if (alignment == 0)
    alignment = ACL_MEM_ALIGN;
  if (alignment != ACL_MEM_ALIGN)
    return NULL;

#endif // !REMOVE_VALID_CHECKS

#ifdef SYSTEM_SVM
#ifdef _WIN32
  result = _aligned_malloc(size, alignment);
#else // LINUX
  mem_result = posix_memalign(&result, alignment, size);
  if (mem_result != 0) {
    return NULL;
  }
#endif
#else
  // Determine if this is SVM memory and, if so, allocate the memory for it
  context_has_svm = CL_FALSE;
  if (acl_get_hal()) {
    for (idevice = 0; idevice < context->num_devices; ++idevice) {
      context_has_svm =
          (cl_bool)(context_has_svm ||
                    acl_svm_device_supports_any_svm(
                        context->device[idevice]->def.physical_device_id));
    }
  }
  // Valid context
  // Allocate
  if (!context_has_svm || acl_get_hal() == NULL ||
      acl_get_hal()->legacy_shared_alloc == NULL) {
// if hal function is not provided, use system alloc
#ifdef _WIN32
    result = _aligned_malloc(size, alignment);
#else // LINUX
    mem_result = posix_memalign(&result, alignment, size);
    if (mem_result != 0) {
      return NULL;
    }
#endif
  } else {
    long long unsigned int offset;
    result = acl_get_hal()->legacy_shared_alloc(context, size, &offset);
  }
#endif // SYSTEM_SVM
  svm_entry = context->svm_list;
  context->svm_list = (acl_svm_entry_t *)malloc(sizeof(acl_svm_entry_t));
  assert(context->svm_list);

  context->svm_list->next = svm_entry;
  if (flags & CL_MEM_READ_ONLY) {
    context->svm_list->read_only = CL_TRUE;
  } else {
    context->svm_list->read_only = CL_FALSE;
  }
  if (flags & CL_MEM_WRITE_ONLY) {
    context->svm_list->write_only = CL_TRUE;
  } else {
    context->svm_list->write_only = CL_FALSE;
  }
  context->svm_list->is_mapped = CL_FALSE;
  context->svm_list->ptr = result;
  context->svm_list->size = size;

  return result;
}

ACL_EXPORT
CL_API_ENTRY void *CL_API_CALL clSVMAlloc(cl_context context,
                                          cl_svm_mem_flags flags, size_t size,
                                          unsigned int alignment) {
  return clSVMAllocIntelFPGA(context, flags, size, alignment);
}

ACL_EXPORT
CL_API_ENTRY void clSVMFreeIntelFPGA(cl_context context, void *svm_pointer) {
  acl_svm_entry_t *last_entry;
  acl_svm_entry_t *next_entry;
  unsigned int idevice;
  cl_bool context_has_svm;
  std::scoped_lock lock{acl_mutex_wrapper};
  context_has_svm = CL_FALSE;
  if (acl_get_hal()) {
    for (idevice = 0; idevice < context->num_devices; ++idevice) {
      context_has_svm =
          (cl_bool)(context_has_svm ||
                    acl_svm_device_supports_any_svm(
                        context->device[idevice]->def.physical_device_id));
    }
  }
#ifndef REMOVE_VALID_CHECKS
  if (!acl_context_is_valid(context))
    return;

  if (svm_pointer == NULL)
    return;

#endif // !REMOVE_VALID_CHECKS
  // Only free the SVM pointer if it is from this context
  if (context->svm_list == NULL)
    return;

  last_entry = NULL;
  next_entry = context->svm_list;
  while (next_entry != NULL) {
    if (next_entry->ptr == svm_pointer) {
      if (last_entry == NULL) {
        context->svm_list = next_entry->next;
      } else {
        last_entry->next = next_entry->next;
      }
#ifdef SYSTEM_SVM
#ifdef _WIN32
      _aligned_free(svm_pointer);
#else // LINUX
      free(svm_pointer);
#endif
#else
      if (!context_has_svm || acl_get_hal() == NULL ||
          acl_get_hal()->legacy_shared_free == NULL) {
#ifdef _WIN32
        _aligned_free(svm_pointer);
#else // LINUX
        free(svm_pointer);
#endif
      } else {
        acl_get_hal()->legacy_shared_free(context, svm_pointer,
                                          next_entry->size);
      }
#endif // SYSTEM_SVM
      free(next_entry);
      break;
    }
    last_entry = next_entry;
    next_entry = next_entry->next;
  }
}

ACL_EXPORT
CL_API_ENTRY void clSVMFree(cl_context context, void *svm_pointer) {
  clSVMFreeIntelFPGA(context, svm_pointer);
}

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueSVMMemcpyIntelFPGA(
    cl_command_queue command_queue, cl_bool blocking_copy, void *dst_ptr,
    const void *src_ptr, size_t size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  cl_event local_event = 0; // used for blocking
  cl_int status;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  if (src_ptr == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer argument cannot be NULL");
  }
  if (dst_ptr == 0) {
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

  // Create an event/command to actually move the data at the appropriate
  // time.
  status =
      acl_create_event(command_queue, num_events_in_wait_list, event_wait_list,
                       CL_COMMAND_SVM_MEMCPY, &local_event);
  if (status != CL_SUCCESS)
    return status; // already signalled callback
  local_event->cmd.info.svm_xfer.src_ptr = src_ptr;
  local_event->cmd.info.svm_xfer.dst_ptr = dst_ptr;
  local_event->cmd.info.svm_xfer.src_size = size;
  local_event->cmd.info.svm_xfer.dst_size = size;

  acl_idle_update(
      command_queue
          ->context); // If nothing's blocking, then complete right away

  if (blocking_copy) {
    status = clWaitForEvents(1, &local_event);
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

ACL_EXPORT
CL_API_ENTRY cl_int CL_API_CALL clEnqueueSVMMemcpy(
    cl_command_queue command_queue, cl_bool blocking_copy, void *dst_ptr,
    const void *src_ptr, size_t size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  return clEnqueueSVMMemcpyIntelFPGA(command_queue, blocking_copy, dst_ptr,
                                     src_ptr, size, num_events_in_wait_list,
                                     event_wait_list, event);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueSVMMemFillIntelFPGA(
    cl_command_queue command_queue, void *svm_ptr, const void *pattern,
    size_t pattern_size, size_t size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  cl_event local_event = 0; // used for blocking
  cl_int status;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }

  if (svm_ptr == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer argument cannot be NULL");
  }
  if (((uintptr_t)svm_ptr) % (pattern_size * 8) != 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer not aligned with pattern size");
  }
  if (pattern == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pattern argument cannot be NULL");
  }
  if (pattern_size == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pattern size argument cannot be 0");
  }
  if (pattern_size != 1 && pattern_size != 2 && pattern_size != 4 &&
      pattern_size != 8 && pattern_size != 16 && pattern_size != 32 &&
      pattern_size != 64 && pattern_size != 128) {
    ERR_RET(
        CL_INVALID_VALUE, command_queue->context,
        "Pattern size argument must be one of {1, 2, 4, 8, 16, 32, 64, 128}");
  }
  if (size == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer size cannot be 0");
  }
  if (size % pattern_size != 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer size must be multiple of pattern size");
  }

  // Create an event/command to actually move the data at the appropriate
  // time.
  status =
      acl_create_event(command_queue, num_events_in_wait_list, event_wait_list,
                       CL_COMMAND_SVM_MEMFILL, &local_event);
  if (status != CL_SUCCESS)
    return status; // already signalled callback
  local_event->cmd.info.svm_xfer.src_ptr = pattern;
  local_event->cmd.info.svm_xfer.dst_ptr = svm_ptr;
  local_event->cmd.info.svm_xfer.src_size = pattern_size;
  local_event->cmd.info.svm_xfer.dst_size = size;

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

CL_API_ENTRY cl_int CL_API_CALL clEnqueueSVMMemFill(
    cl_command_queue command_queue, void *svm_ptr, const void *pattern,
    size_t pattern_size, size_t size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  return clEnqueueSVMMemFillIntelFPGA(
      command_queue, svm_ptr, pattern, pattern_size, size,
      num_events_in_wait_list, event_wait_list, event);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueSVMMapIntelFPGA(
    cl_command_queue command_queue, cl_bool blocking_map, cl_map_flags flags,
    void *svm_ptr, size_t size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  cl_event local_event = 0; // used for blocking
  cl_int status;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (svm_ptr == NULL) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer argument cannot be NULL");
  }
  if (size == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer size cannot be 0");
  }
  if (flags & ~(CL_MAP_READ | CL_MAP_WRITE)) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Invalid or unsupported flags");
  }

  // Create an event/command to actually move the data at the appropriate
  // time.
  status = acl_create_event(command_queue, num_events_in_wait_list,
                            event_wait_list, CL_COMMAND_SVM_MAP, &local_event);
  if (status != CL_SUCCESS)
    return status; // already signalled callback
  // We don't use this right now, but if we ever have to sync up caches we will
  // need this.
  local_event->cmd.info.svm_xfer.dst_ptr = svm_ptr;
  local_event->cmd.info.svm_xfer.dst_size = size;

  acl_idle_update(
      command_queue
          ->context); // If nothing's blocking, then complete right away

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
  return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueSVMMap(
    cl_command_queue command_queue, cl_bool blocking_map, cl_map_flags flags,
    void *svm_ptr, size_t size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  return clEnqueueSVMMapIntelFPGA(command_queue, blocking_map, flags, svm_ptr,
                                  size, num_events_in_wait_list,
                                  event_wait_list, event);
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueSVMUnmapIntelFPGA(cl_command_queue command_queue, void *svm_ptr,
                           cl_uint num_events_in_wait_list,
                           const cl_event *event_wait_list, cl_event *event) {
  cl_event local_event = 0; // used for blocking
  cl_int status;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (svm_ptr == NULL) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Pointer argument cannot be NULL");
  }

  // Create an event/command to actually move the data at the appropriate
  // time.
  status =
      acl_create_event(command_queue, num_events_in_wait_list, event_wait_list,
                       CL_COMMAND_SVM_UNMAP, &local_event);
  if (status != CL_SUCCESS)
    return status; // already signalled callback
  // We don't use this right now, but if we ever have to sync up caches we will
  // need this.
  local_event->cmd.info.svm_xfer.dst_ptr = svm_ptr;

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

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueSVMUnmap(cl_command_queue command_queue, void *svm_ptr,
                  cl_uint num_events_in_wait_list,
                  const cl_event *event_wait_list, cl_event *event) {
  return clEnqueueSVMUnmapIntelFPGA(
      command_queue, svm_ptr, num_events_in_wait_list, event_wait_list, event);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueSVMFreeIntelFPGA(
    cl_command_queue command_queue, cl_uint num_svm_pointers,
    void *svm_pointers[],
    void(CL_CALLBACK *pfn_free_func)(cl_command_queue /* queue */,
                                     cl_uint /* num_svm_pointers */,
                                     void *[] /* svm_pointers[] */,
                                     void * /* user_data */),
    void *user_data, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  cl_event local_event = 0; // used for blocking
  cl_int status;
  std::scoped_lock lock{acl_mutex_wrapper};

  if (!acl_command_queue_is_valid(command_queue)) {
    return CL_INVALID_COMMAND_QUEUE;
  }
  if (svm_pointers == NULL) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "List of SVM pointers argument cannot be NULL");
  }
  if (num_svm_pointers == 0) {
    ERR_RET(CL_INVALID_VALUE, command_queue->context,
            "Number of SVM pointers cannot be 0");
  }

  // Create an event/command to actually move the data at the appropriate
  // time.
  status = acl_create_event(command_queue, num_events_in_wait_list,
                            event_wait_list, CL_COMMAND_SVM_FREE, &local_event);
  if (status != CL_SUCCESS)
    return status; // already signalled callback
  // We don't use this right now, but if we ever have to sync up caches we will
  // need this.
  local_event->cmd.info.svm_free.pfn_free_func = pfn_free_func;
  local_event->cmd.info.svm_free.num_svm_pointers = num_svm_pointers;
  local_event->cmd.info.svm_free.svm_pointers = svm_pointers;
  local_event->cmd.info.svm_free.user_data = user_data;

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

CL_API_ENTRY cl_int CL_API_CALL clEnqueueSVMFree(
    cl_command_queue command_queue, cl_uint num_svm_pointers,
    void *svm_pointers[],
    void(CL_CALLBACK *pfn_free_func)(cl_command_queue /* queue */,
                                     cl_uint /* num_svm_pointers */,
                                     void *[] /* svm_pointers[] */,
                                     void * /* user_data */),
    void *user_data, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  return clEnqueueSVMFreeIntelFPGA(
      command_queue, num_svm_pointers, svm_pointers, pfn_free_func, user_data,
      num_events_in_wait_list, event_wait_list, event);
}

void acl_forcibly_release_all_svm_memory_for_context(cl_context context) {
  acl_assert_locked();
  while (context->svm_list != NULL) {
    clSVMFreeIntelFPGA(context, context->svm_list->ptr);
  }
}

cl_bool acl_ptr_is_exactly_in_context_svm(cl_context context, const void *ptr) {
  acl_svm_entry_t *next_svm_entry;
  acl_assert_locked();
  next_svm_entry = context->svm_list;

  while (next_svm_entry != NULL) {
    if (next_svm_entry->ptr == ptr) {
      return CL_TRUE;
    }
    next_svm_entry = next_svm_entry->next;
  }

  return CL_FALSE;
}

cl_bool acl_ptr_is_contained_in_context_svm(cl_context context,
                                            const void *ptr) {
  acl_svm_entry_t *next_svm_entry;
  acl_assert_locked();
  next_svm_entry = context->svm_list;

  while (next_svm_entry != NULL) {
    if ((char *)ptr >= (char *)next_svm_entry->ptr &&
        (char *)ptr < (char *)next_svm_entry->ptr + next_svm_entry->size) {
      return CL_TRUE;
    }
    next_svm_entry = next_svm_entry->next;
  }

  return CL_FALSE;
}

acl_svm_entry_t *acl_get_svm_entry(cl_context context, void *ptr) {
  acl_svm_entry_t *result = NULL;
  acl_svm_entry_t *next_svm_entry;
  acl_assert_locked();

  next_svm_entry = context->svm_list;

  while (next_svm_entry != NULL) {
    if ((char *)ptr >= (char *)next_svm_entry->ptr &&
        (char *)ptr < (char *)next_svm_entry->ptr + next_svm_entry->size) {
      result = next_svm_entry;
      break;
    }
    next_svm_entry = next_svm_entry->next;
  }

  return result;
}

// Submit an op to the device op queue to copy memory.
// Return 1 if we made forward progress, 0 otherwise.
int acl_svm_op(cl_event event) {
  int result = 0;
  // The buffer is defined to always be host accessible.
  // So just count the mappings.
  const void *src_ptr = event->cmd.info.svm_xfer.src_ptr;
  void *dst_ptr = event->cmd.info.svm_xfer.dst_ptr;
  size_t src_size = event->cmd.info.svm_xfer.src_size;
  size_t dst_size = event->cmd.info.svm_xfer.dst_size;
  acl_assert_locked();

  acl_set_execution_status(event, CL_SUBMITTED);
  acl_set_execution_status(event, CL_RUNNING);
  if (event->cmd.type == CL_COMMAND_SVM_MEMCPY) {
    memcpy(dst_ptr, src_ptr, src_size);
  } else if (event->cmd.type == CL_COMMAND_SVM_MEMFILL) {
    size_t current_pos = 0;
    while (current_pos < dst_size) {
      memcpy((char *)dst_ptr + current_pos, (char *)src_ptr, src_size);
      current_pos += src_size;
    }
  } else if (event->cmd.type == CL_COMMAND_SVM_MAP) {
    // Do nothing. One day we may need to do something if we need to sync up
    // caches.
  } else if (event->cmd.type == CL_COMMAND_SVM_UNMAP) {
    // Do nothing. One day we may need to do something if we need to sync up
    // caches.
  } else if (event->cmd.type == CL_COMMAND_SVM_FREE) {
    // If a user function was defined, pass in the provided values
    if (event->cmd.info.svm_free.pfn_free_func != NULL) {
      event->cmd.info.svm_free.pfn_free_func(
          event->command_queue, event->cmd.info.svm_free.num_svm_pointers,
          event->cmd.info.svm_free.svm_pointers,
          event->cmd.info.svm_free.user_data);
      // Otherwise, just call the regular SVM function on each SVM pointer
      // provided
    } else {
      size_t i;

      for (i = 0; i < event->cmd.info.svm_free.num_svm_pointers; ++i) {
        clSVMFreeIntelFPGA(event->context,
                           event->cmd.info.svm_free.svm_pointers[i]);
      }
    }
  }
  acl_set_execution_status(event, CL_COMPLETE);
  acl_print_debug_msg("SVM operation\n");

  result = 1;

  return result;
}

cl_bool acl_svm_device_supports_any_svm(unsigned int physical_device_id) {
  acl_assert_locked();
  int memories_supported;
  int supports = acl_get_hal()->has_svm_memory_support(physical_device_id,
                                                       &memories_supported);

  if (supports != 0) {
    return CL_TRUE;
  } else {
    return CL_FALSE;
  }
}

cl_bool
acl_svm_device_supports_physical_memory(unsigned int physical_device_id) {
  acl_assert_locked();

  int supports = acl_get_hal()->has_physical_mem(physical_device_id);

  if (supports != 0) {
    return CL_TRUE;
  } else {
    return CL_FALSE;
  }
}
