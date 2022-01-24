// Copyright (C) 2020-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_USM_H
#define ACL_USM_H

#include <CL/opencl.h>

#include "acl_types.h"

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

int acl_submit_usm_memcpy(cl_event event);
void acl_usm_memcpy(void *user_data, acl_device_op_t *op);
bool acl_usm_ptr_belongs_to_context(cl_context context, const void *ptr);
acl_usm_allocation_t *acl_get_usm_alloc_from_ptr(cl_context context,
                                                 const void *ptr);

/**
 *  Checks if the device provides CL_UNIFIED_SHARED_MEMORY_ACCESS_INTEL
 *  capabilities for a given allocation query
 *  @param device the device to query
 *  @param query the type of capability that is being queried
 *  @return true is that query has access capabilities
 */
bool acl_usm_has_access_capability(cl_device_id device, cl_device_info query);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif // ACL_USM_H
