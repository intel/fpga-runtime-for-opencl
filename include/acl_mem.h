// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_MEM_H
#define ACL_MEM_H

#include <CL/opencl.h>

#include "acl_types.h"

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

// Allocate a block of host memory aligned to ACL_MEM_ALIGN.
acl_aligned_ptr_t acl_mem_aligned_malloc(size_t size);
void acl_mem_aligned_free(cl_context context, acl_aligned_ptr_t *ptr);

// These are basically no-ops because we only support
// non-unified memory (for now).
// Return 1 if they made forward progress, 0 otherwise.
int acl_mem_map_buffer(cl_event event);
int acl_mem_unmap_mem_object(cl_event event);

// Return 1 if accessible, 0 if not
int acl_is_device_accessible(cl_mem mem);

// The event is ready to submit (no other events or conditions block it).
// Call this to try submitting it to the device op queue.
// Return 1 if they made forward progress, 0 otherwise.
int acl_submit_mem_transfer_device_op(cl_event event);

int acl_submit_migrate_mem_device_op(cl_event event);

int acl_realloc_buffer_for_simulator(cl_mem mem,
                                     const unsigned int physical_device_id,
                                     const unsigned int mem_id);

// Actually execute the memory transfer device operation.
// In the normal case source and destination are different, in which case
// the HAL is called and the transfer is non-blocking.
// That is, this call will return before the transfer is complete.
// In all cases, the operation will be marked with a with a CL_COMPLETE
// execution_status (or error) when the transfer is finished.
ACL_EXPORT
void acl_mem_transfer_buffer(void *user_data, acl_device_op_t *op);

// Move the physical memory to the requested device
ACL_EXPORT
void acl_mem_migrate_buffer(void *user_data, acl_device_op_t *op);

int acl_mem_op_requires_transfer(const acl_command_info_t &cmd);

void acl_forcibly_release_all_memory_for_context(cl_context context);

void acl_print_all_mem();
void acl_print_all_mem_detail();
void acl_print_mem(cl_mem mem);
void acl_reset_mem(cl_mem mem);

void acl_copy_device_buffers_to_host_before_programming(
    cl_context context, unsigned int physical_device_id,
    void(CL_CALLBACK *read_callback)(cl_mem, int));
void acl_copy_device_buffers_from_host_after_programming(
    cl_context context, unsigned int physcial_device_id,
    void(CL_CALLBACK *write_callback)(cl_mem, int));

void acl_resize_reserved_allocations_for_device(cl_mem mem,
                                                acl_device_def_t &def);
cl_int acl_reserve_buffer_block(cl_mem mem, acl_mem_region_t *region,
                                unsigned physical_device_id,
                                unsigned target_mem_id);

int acl_get_default_memory(const acl_device_def_t &dev);
int acl_get_default_device_global_memory(const acl_device_def_t &dev);

void acl_mem_destructor_callback(
    cl_mem memobj); // The function that calls the user registered callbacks via
                    // clSetMemObjectDestructorCallback.

size_t acl_get_image_element_size(cl_context context,
                                  const cl_image_format *image_format,
                                  cl_int *errcode_ret);

void *acl_get_physical_address(cl_mem mem, cl_device_id device);

int acl_bind_buffer_to_device(cl_device_id device, cl_mem mem);

cl_bool acl_is_sub_or_parent_buffer(cl_mem mem);

// This callback is used to free the allocated host memory needed for memory
// transfers, currently used in clEnqueueFillBuffer and clEnqueueFillImage
void CL_CALLBACK acl_free_allocation_after_event_completion(
    cl_event event, cl_int event_command_exec_status, void *callback_data);

// Submit a device global read operation to the device op queue
cl_int acl_submit_read_device_global_device_op(cl_event event);
// Submit a device global write device operation to the device op queue
cl_int acl_submit_write_device_global_device_op(cl_event event);

// Read from a device global
void acl_read_device_global(void *user_data, acl_device_op_t *op);

// Write into a device global
void acl_write_device_global(void *user_data, acl_device_op_t *op);

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif
