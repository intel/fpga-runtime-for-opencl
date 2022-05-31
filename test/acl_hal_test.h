// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_HAL_TEST_H
#define ACL_HAL_TEST_H

// A simple HAL for testing.

const acl_hal_t *acl_test_get_simple_hal(void);

void acltest_hal_teardown(void);

// Make sure that all these device addresses have host storage representing
// them. And translate them into a host pointer.  It's transient though!
void *acltest_translate_device_address(const void *device_ptr, size_t offset);

void acl_test_hal_set_svm_memory_support(int value);
void acl_test_hal_set_physical_memory_support(bool value);

extern bool acltest_hal_emulate_device_mem;

void acltest_call_event_update_callback(cl_event event, int new_status);
void acltest_call_kernel_update_callback(unsigned int physical_device_id, int activation_id, cl_int status);
void acltest_call_printf_buffer_callback(unsigned int physical_device_id, int activation_id, int size,
                                         int stalled);

#endif
