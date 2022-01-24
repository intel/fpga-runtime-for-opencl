// Copyright (C) 2014-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_SVM_H
#define ACL_SVM_H

#include <CL/opencl.h>

#include "acl_types.h"

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

void acl_forcibly_release_all_svm_memory_for_context(cl_context context);

// Get the information about this SVM pointer from the context.
acl_svm_entry_t *acl_get_svm_entry(cl_context context, void *ptr);

// Check if this SVM pointer is in the context. Can check if it exactly matches
// an allocated pointer or if it is contained within an allocated block of
// memory.
cl_bool acl_ptr_is_exactly_in_context_svm(cl_context context, const void *ptr);
cl_bool acl_ptr_is_contained_in_context_svm(cl_context context,
                                            const void *ptr);

int acl_svm_op(cl_event event);

cl_bool acl_svm_device_supports_any_svm(unsigned int physical_device_id);
cl_bool
acl_svm_device_supports_physical_memory(unsigned int physical_device_id);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif // ACL_SVM_H
