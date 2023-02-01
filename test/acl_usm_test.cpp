// Copyright (C) 2020-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

// USM is not supported on ARM SoC, so we do not compile this test when
// compiling for ARM
#ifndef __arm__

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter
#endif
#include <CppUTest/TestHarness.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <CL/opencl.h>

#include <acl.h>
#include <acl_context.h>
#include <acl_hal.h>
#include <acl_support.h>
#include <acl_types.h>
#include <acl_usm.h>
#include <acl_util.h>

#include "acl_hal_test.h"
#include "acl_test.h"

#include <cstdio>
#include <string.h>

static void CL_CALLBACK notify_me_print(const char *errinfo,
                                        const void *private_info, size_t cb,
                                        void *user_data);

MT_TEST_GROUP(acl_usm) {
public:
  enum { MEM_SIZE = 1024, SUBBUF_SIZE = 256 };

  void setup() {
    if (threadNum() == 0) {
      acl_test_setup_generic_system();
    }

    syncThreads();

    this->load();
  }

  void teardown() {
    unload_context();
    if (m_context) {
      clReleaseContext(m_context);
      m_context = 0;
    }

    syncThreads();

    if (threadNum() == 0) {
      acl_test_teardown_generic_system();
    }
    acl_test_run_standard_teardown_checks();
  }

  void load(void) {
    m_cq = 0;
    m_context = 0;

    CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &m_platform, 0));
    ACL_LOCKED(CHECK(acl_platform_is_valid(m_platform)));
    CHECK_EQUAL(CL_SUCCESS,
                clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, ACL_MAX_DEVICE,
                               &m_device[0], &m_num_devices));
    CHECK(m_num_devices >= 3);
    m_num_devices = 2;

    load_normal_context();

    // Grab one less devices than those present.
    CHECK(m_device[0]);
    CHECK(m_device[0]->present);
    CHECK(m_device[1]);
    CHECK(m_device[1]->present);

    load_command_queue();
  }

  void unload_context() {
    unload_command_queue();
    if (m_context) {
      syncThreads();
      clReleaseContext(m_context);
      m_context = 0;
      syncThreads();
    }
    CHECK_EQUAL(0, m_context);
  }
  void load_normal_context(void) {
    unload_context();

    cl_int status = CL_INVALID_DEVICE;
    CHECK_EQUAL(0, m_context);
    m_context =
        clCreateContext(acl_test_context_prop_preloaded_binary_only(),
                        m_num_devices, &(m_device[0]), NULL, NULL, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));
  }

  void load_backing_store_context(void) {
    unload_context();
    cl_context_properties props[] = {
        CL_CONTEXT_COMPILER_MODE_INTELFPGA,
        CL_CONTEXT_COMPILER_MODE_OFFLINE_USE_EXE_LIBRARY_INTELFPGA, 0};
    cl_int status = CL_INVALID_VALUE;
    CHECK_EQUAL(0, m_context);
    m_context = clCreateContext(props, m_num_devices, &(m_device[0]),
                                notify_me_print, this, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

    load_command_queue();
  }

  void unload_command_queue(void) {
    if (m_cq) {
      clReleaseCommandQueue(m_cq);
      m_cq = 0;
    }
  }
  void load_command_queue(void) {
    unload_command_queue();
    CHECK(m_num_devices > 0);
    cl_int status = CL_INVALID_DEVICE;
    m_cq = clCreateCommandQueue(m_context, m_device[0],
                                CL_QUEUE_PROFILING_ENABLE, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_command_queue_is_valid(m_cq)));
  }

  void check_event_perfcounters(cl_event event) {
    cl_ulong time[4];
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_QUEUED,
                                        sizeof(time[0]), &time[0], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_SUBMIT,
                                        sizeof(time[1]), &time[1], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START,
                                        sizeof(time[2]), &time[2], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END,
                                        sizeof(time[3]), &time[3], 0));
    CHECK(0 < time[0]);
    CHECK(time[0] < time[1]);
    CHECK(time[1] < time[2]);
    CHECK(time[2] < time[3]);
  }

protected:
  cl_platform_id m_platform;
  cl_context m_context;
  cl_uint m_num_devices;
  cl_device_id m_device[ACL_MAX_DEVICE];
  cl_command_queue m_cq;

public:
  bool yeah;
};

#if 1
static void CL_CALLBACK notify_me_print(const char *errinfo,
                                        const void *private_info, size_t cb,
                                        void *user_data) {
  CppUTestGroupacl_usm *inst = (CppUTestGroupacl_usm *)user_data;
  if (inst) {
    if (inst->yeah)
      printf("Context error: %s\n", errinfo);
  }
  cb = cb;                     // avoid warning on windows
  private_info = private_info; // avoid warning on windows
}
#endif

MT_TEST(acl_usm, alloc_and_free_device_usm) {
  ACL_LOCKED(acl_print_debug_msg("begin alloc_and_free_device_usm\n"));
  const int alignment = ACL_MEM_ALIGN;
  cl_int status;
  this->yeah = true;

  acl_usm_allocation_t *test_device_alloc;

  cl_mem_properties_intel bad_property1[3] = {
      CL_MEM_ALLOC_FLAGS_INTEL - 1, // Undefined property
      CL_MEM_ALLOC_WRITE_COMBINED_INTEL, 0};
  cl_mem_properties_intel bad_property2[5] = {
      CL_MEM_ALLOC_FLAGS_INTEL, CL_MEM_ALLOC_WRITE_COMBINED_INTEL,
      CL_MEM_ALLOC_FLAGS_INTEL - 1, // Undefined property
      CL_MEM_ALLOC_WRITE_COMBINED_INTEL, 0};
  cl_mem_properties_intel good_property1[3] = {
      CL_MEM_ALLOC_FLAGS_INTEL, CL_MEM_ALLOC_WRITE_COMBINED_INTEL, 0};

  ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

  // Alloc & free is error free
  void *test_ptr = clDeviceMemAllocINTEL(m_context, m_device[0], NULL, 8,
                                         alignment, &status);
  CHECK(test_ptr != NULL);
  CHECK(ACL_DEVICE_ALLOCATION(test_ptr));
  CHECK_EQUAL(status, CL_SUCCESS);
  CHECK(!m_context->usm_allocation.empty());
  ACL_LOCKED(CHECK_EQUAL(acl_usm_ptr_belongs_to_context(m_context, test_ptr),
                         CL_TRUE));
  status = clMemFreeINTEL(m_context, test_ptr);
  CHECK_EQUAL(status, CL_SUCCESS);
  ACL_LOCKED(CHECK_EQUAL(acl_usm_ptr_belongs_to_context(m_context, test_ptr),
                         CL_FALSE));
  CHECK(m_context->usm_allocation.empty());

  void *test_ptr2 = clDeviceMemAllocINTEL(m_context, m_device[0], NULL, 8,
                                          alignment, &status);
  CHECK(test_ptr2 != NULL);
  CHECK(ACL_DEVICE_ALLOCATION(test_ptr2));
  CHECK_EQUAL(status, CL_SUCCESS);
  CHECK(!m_context->usm_allocation.empty());
  ACL_LOCKED(CHECK_EQUAL(acl_usm_ptr_belongs_to_context(m_context, test_ptr2),
                         CL_TRUE));
  status = clMemBlockingFreeINTEL(m_context, test_ptr2);
  CHECK_EQUAL(status, CL_SUCCESS);
  ACL_LOCKED(CHECK_EQUAL(acl_usm_ptr_belongs_to_context(m_context, test_ptr2),
                         CL_FALSE));
  CHECK(m_context->usm_allocation.empty());

  // Bad context
  test_ptr = clDeviceMemAllocINTEL(0, m_device[0], NULL, 8, alignment, &status);
  CHECK(test_ptr == NULL);
  CHECK(status != CL_SUCCESS);
  status = clMemFreeINTEL(0, 0);
  CHECK(status != CL_SUCCESS);
  test_ptr2 =
      clDeviceMemAllocINTEL(0, m_device[0], NULL, 8, alignment, &status);
  CHECK(test_ptr2 == NULL);
  CHECK(status != CL_SUCCESS);
  status = clMemBlockingFreeINTEL(0, 0);
  CHECK(status != CL_SUCCESS);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  test_ptr = clDeviceMemAllocINTEL(&fake_context, m_device[0], NULL, 8,
                                   alignment, &status);
  CHECK(test_ptr == NULL);
  CHECK(status != CL_SUCCESS);
  status = clMemFreeINTEL(&fake_context, 0);
  CHECK(status != CL_SUCCESS);
  test_ptr2 = clDeviceMemAllocINTEL(&fake_context, m_device[0], NULL, 8,
                                    alignment, &status);
  CHECK(test_ptr2 == NULL);
  CHECK(status != CL_SUCCESS);
  status = clMemBlockingFreeINTEL(&fake_context, 0);
  CHECK(status != CL_SUCCESS);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();

  // Device not in context
  test_ptr = clDeviceMemAllocINTEL(m_context, m_device[2], NULL, 8, alignment,
                                   &status);
  CHECK(test_ptr == NULL);
  CHECK(status != CL_SUCCESS);
  CHECK(m_context->usm_allocation.empty());

  // Invalid Property
  // Bad bits: First property is invalid
  test_ptr = clDeviceMemAllocINTEL(m_context, m_device[0], &(bad_property1[0]),
                                   8, alignment, &status);
  CHECK(test_ptr == NULL);
  CHECK(status != CL_SUCCESS);
  // Bad bits: Second property is invalid
  test_ptr = clDeviceMemAllocINTEL(m_context, m_device[0], &(bad_property2[0]),
                                   8, alignment, &status);
  CHECK(test_ptr == NULL);
  CHECK(status != CL_SUCCESS);

  // Good bits & produce the correct USM allocation in context
  test_ptr = clDeviceMemAllocINTEL(m_context, m_device[0], &(good_property1[0]),
                                   8, alignment, &status);
  CHECK_EQUAL(status, CL_SUCCESS);
  CHECK(test_ptr != NULL);
  CHECK(!m_context->usm_allocation.empty());
  ACL_LOCKED(CHECK_EQUAL(acl_usm_ptr_belongs_to_context(m_context, test_ptr),
                         CL_TRUE));
  ACL_LOCKED(test_device_alloc =
                 acl_get_usm_alloc_from_ptr(m_context, test_ptr));
  CHECK(test_device_alloc != NULL);
  CHECK(test_device_alloc);
  CHECK_EQUAL(test_device_alloc->range.begin, test_ptr);
  CHECK_EQUAL(test_device_alloc->alloc_flags,
              CL_MEM_ALLOC_WRITE_COMBINED_INTEL);

  // Test internal functions for success
  // Offset is less than size
  size_t offset = 7;
  acl_usm_allocation_t *test_device_alloc2 = NULL;
  void *test_ptr_w_offset = (void *)((unsigned long long)test_ptr + offset);
  ACL_LOCKED(test_device_alloc2 =
                 acl_get_usm_alloc_from_ptr(m_context, test_ptr_w_offset));
  CHECK(test_device_alloc2 != NULL);
  CHECK_EQUAL(test_device_alloc2, test_device_alloc);

  // Test internal functions for failure
  // Offset is equal to size of allocation
  offset = 8;
  test_ptr_w_offset = (void *)((unsigned long long)test_ptr + offset);
  ACL_LOCKED(test_device_alloc2 =
                 acl_get_usm_alloc_from_ptr(m_context, test_ptr_w_offset));
  CHECK(test_device_alloc2 == NULL);

  // Done internal function test
  clMemFreeINTEL(m_context, test_ptr);
  CHECK(m_context->usm_allocation.empty());

  // Default works and defaults to 0
  test_ptr =
      clDeviceMemAllocINTEL(m_context, m_device[0], NULL, 8, alignment, NULL);
  CHECK(test_ptr != NULL);
  CHECK(!m_context->usm_allocation.empty());
  ACL_LOCKED(test_device_alloc =
                 acl_get_usm_alloc_from_ptr(m_context, test_ptr));
  CHECK(test_device_alloc != NULL);
  CHECK(test_device_alloc);
  CHECK_EQUAL(test_device_alloc->range.begin, test_ptr);
  CHECK_EQUAL(test_device_alloc->alloc_flags, 0);
  clMemFreeINTEL(m_context, test_ptr);
  CHECK(m_context->usm_allocation.empty());

  // bad size
  test_ptr =
      clDeviceMemAllocINTEL(m_context, m_device[0], NULL, 0, alignment, NULL);
  CHECK(test_ptr == NULL);
  test_ptr = clDeviceMemAllocINTEL(m_context, m_device[0], NULL, 0xffffffff,
                                   alignment, NULL);
  CHECK(test_ptr == NULL);

  cl_ulong max_alloc = 0;
  clGetDeviceInfo(m_device[0], CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(max_alloc),
                  &max_alloc, 0);
  test_ptr = clDeviceMemAllocINTEL(m_context, m_device[0], NULL,
                                   (size_t)(max_alloc + 1), alignment, NULL);
  CHECK(test_ptr == NULL);

  // bad alignment: Not a power of 2
  test_ptr = clDeviceMemAllocINTEL(m_context, m_device[0], NULL, 8, 3, NULL);
  CHECK(test_ptr == NULL);
  // bad alignment: Not supported by any device
  // All devices currently use ACL_MEM_ALIGN. Try ACL_MEM_ALIGN * 2.
  // If this ever changes (and therefore this test starts to fail) the test
  // should be updated to capture the new expected behaviour.
  test_ptr = clDeviceMemAllocINTEL(m_context, m_device[0], NULL, 8,
                                   ACL_MEM_ALIGN * 2, NULL);
  CHECK(test_ptr == NULL);

  // Alignment 0 passes
  test_ptr = clDeviceMemAllocINTEL(m_context, m_device[0], NULL, 8, 0, NULL);
  CHECK(test_ptr != NULL);
  clMemFreeINTEL(m_context, test_ptr);
  CHECK(m_context->usm_allocation.empty());

  // Multiple allocs & frees work
  test_ptr =
      clDeviceMemAllocINTEL(m_context, m_device[0], NULL, 8, alignment, NULL);
  CHECK(test_ptr != NULL);
  test_ptr2 =
      clDeviceMemAllocINTEL(m_context, m_device[0], NULL, 8, alignment, NULL);
  CHECK(test_ptr2 != NULL);
  CHECK(test_device_alloc);
  ACL_LOCKED(test_device_alloc =
                 acl_get_usm_alloc_from_ptr(m_context, test_ptr));
  CHECK(test_device_alloc != NULL);
  CHECK(test_device_alloc);
  CHECK_EQUAL(test_device_alloc->range.begin, test_ptr);
  ACL_LOCKED(test_device_alloc =
                 acl_get_usm_alloc_from_ptr(m_context, test_ptr2));
  CHECK(test_device_alloc != NULL);
  CHECK(test_device_alloc);
  CHECK_EQUAL(test_device_alloc->range.begin, test_ptr2);
  clMemFreeINTEL(m_context, test_ptr);
  clMemBlockingFreeINTEL(m_context, test_ptr2);
  CHECK(m_context->usm_allocation.empty());

  ACL_LOCKED(acl_print_debug_msg("end alloc_and_free_device_usm\n"));
}

MT_TEST(acl_usm, buffer_location_usm) {
  ACL_LOCKED(acl_print_debug_msg("begin buffer_location_usm\n"));
  const int alignment = ACL_MEM_ALIGN;
  cl_int status;
  this->yeah = true;

  acl_usm_allocation_t *test_device_alloc;

  cl_mem_properties_intel good_property[3] = {
      CL_MEM_ALLOC_BUFFER_LOCATION_INTEL, 0, 0};

  ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

  // Correct USM buffer allocation
  void *test_ptr = clDeviceMemAllocINTEL(
      m_context, m_device[0], &(good_property[0]), 8, alignment, &status);
  CHECK_EQUAL(status, CL_SUCCESS);
  CHECK(ACL_DEVICE_ALLOCATION(test_ptr));
  CHECK(test_ptr != NULL);
  CHECK(!m_context->usm_allocation.empty());
  ACL_LOCKED(CHECK_EQUAL(acl_usm_ptr_belongs_to_context(m_context, test_ptr),
                         CL_TRUE));
  ACL_LOCKED(test_device_alloc =
                 acl_get_usm_alloc_from_ptr(m_context, test_ptr));
  assert(test_device_alloc);
  CHECK_EQUAL(test_device_alloc->range.begin, test_ptr);

  // Check alloc information
  cl_uint read_mem_id = 4;
  size_t ret_size = 9;
  status = clGetMemAllocInfoINTEL(m_context, test_ptr,
                                  CL_MEM_ALLOC_BUFFER_LOCATION_INTEL,
                                  sizeof(cl_uint), &read_mem_id, &ret_size);

  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(0, read_mem_id);
  CHECK_EQUAL(sizeof(cl_uint), ret_size);

  status = clMemFreeINTEL(m_context, test_ptr);
  ACL_LOCKED(CHECK_EQUAL(acl_usm_ptr_belongs_to_context(m_context, test_ptr),
                         CL_FALSE));
  CHECK(m_context->usm_allocation.empty());

  // Check when given pointer is already freed
  status = clGetMemAllocInfoINTEL(m_context, test_ptr,
                                  CL_MEM_ALLOC_BUFFER_LOCATION_INTEL,
                                  sizeof(cl_uint), &read_mem_id, &ret_size);

  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(0, read_mem_id);
  CHECK_EQUAL(sizeof(cl_uint), ret_size);
  CHECK_EQUAL(status, CL_SUCCESS);

  ACL_LOCKED(acl_print_debug_msg("end buffer_location_usm\n"));
}

MT_TEST(acl_usm, alloc_and_free_shared_usm) {
  ACL_LOCKED(acl_print_debug_msg("begin alloc_and_free_shared_usm\n"));
  const int alignment = 16;
  cl_int status;
  this->yeah = true;

  acl_usm_allocation_t *test_shared_alloc;

  cl_mem_properties_intel bad_property1[3] = {
      CL_MEM_ALLOC_FLAGS_INTEL - 1, // Undefined property
      CL_MEM_ALLOC_WRITE_COMBINED_INTEL, 0};
  cl_mem_properties_intel bad_property2[5] = {
      CL_MEM_ALLOC_FLAGS_INTEL, CL_MEM_ALLOC_WRITE_COMBINED_INTEL,
      CL_MEM_ALLOC_FLAGS_INTEL - 1, // Undefined property
      CL_MEM_ALLOC_WRITE_COMBINED_INTEL, 0};
  cl_mem_properties_intel good_property1[3] = {
      CL_MEM_ALLOC_FLAGS_INTEL, CL_MEM_ALLOC_WRITE_COMBINED_INTEL, 0};

  ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

  // Alloc & free is error free
  void *test_ptr = clSharedMemAllocINTEL(m_context, m_device[0], NULL, 8,
                                         alignment, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(test_ptr != NULL);
  CHECK(!m_context->usm_allocation.empty());
  ACL_LOCKED(CHECK_EQUAL(acl_usm_ptr_belongs_to_context(m_context, test_ptr),
                         CL_TRUE));
  status = clMemFreeINTEL(m_context, test_ptr);
  CHECK_EQUAL(status, CL_SUCCESS);
  ACL_LOCKED(CHECK_EQUAL(acl_usm_ptr_belongs_to_context(m_context, test_ptr),
                         CL_FALSE));
  CHECK(m_context->usm_allocation.empty());

  // Bad context
  test_ptr = clSharedMemAllocINTEL(0, m_device[0], NULL, 8, alignment, &status);
  CHECK(test_ptr == NULL);
  CHECK(status != CL_SUCCESS);
  status = clMemFreeINTEL(0, 0);
  CHECK(status != CL_SUCCESS);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  test_ptr = clSharedMemAllocINTEL(&fake_context, m_device[0], NULL, 8,
                                   alignment, &status);
  CHECK(test_ptr == NULL);
  CHECK(status != CL_SUCCESS);
  status = clMemFreeINTEL(&fake_context, 0);
  CHECK(status != CL_SUCCESS);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();

  // Shared not in context
  test_ptr = clSharedMemAllocINTEL(m_context, m_device[2], NULL, 8, alignment,
                                   &status);
  CHECK(test_ptr == NULL);
  CHECK(status != CL_SUCCESS);
  CHECK(m_context->usm_allocation.empty());

  // Invalid Property
  // Bad bits: First property is invalid
  test_ptr = clSharedMemAllocINTEL(m_context, m_device[0], &(bad_property1[0]),
                                   8, alignment, &status);
  CHECK(test_ptr == NULL);
  CHECK(status != CL_SUCCESS);
  // Bad bits: Second property is invalid
  test_ptr = clSharedMemAllocINTEL(m_context, m_device[0], &(bad_property2[0]),
                                   8, alignment, &status);
  CHECK(test_ptr == NULL);
  CHECK(status != CL_SUCCESS);

  // Good bits & produce the correct USM allocation in context
  test_ptr = clSharedMemAllocINTEL(m_context, m_device[0], &(good_property1[0]),
                                   8, alignment, &status);
  CHECK_EQUAL(status, CL_SUCCESS);
  CHECK(test_ptr != NULL);
  CHECK(!m_context->usm_allocation.empty());
  ACL_LOCKED(CHECK_EQUAL(acl_usm_ptr_belongs_to_context(m_context, test_ptr),
                         CL_TRUE));
  ACL_LOCKED(test_shared_alloc =
                 acl_get_usm_alloc_from_ptr(m_context, test_ptr));
  CHECK(test_shared_alloc != NULL);
  CHECK(test_shared_alloc);
  CHECK_EQUAL(test_shared_alloc->range.begin, test_ptr);
  CHECK_EQUAL(test_shared_alloc->alloc_flags,
              CL_MEM_ALLOC_WRITE_COMBINED_INTEL);

  // Test internal functions for success
  // Offset is less than size
  size_t offset = 7;
  acl_usm_allocation_t *test_shared_alloc2 = NULL;
  void *test_ptr_w_offset = (void *)((unsigned long long)test_ptr + offset);
  ACL_LOCKED(test_shared_alloc2 =
                 acl_get_usm_alloc_from_ptr(m_context, test_ptr_w_offset));
  CHECK(test_shared_alloc2 != NULL);
  CHECK_EQUAL(test_shared_alloc2, test_shared_alloc);

  // Test internal functions for failure
  // Offset is equal to size of allocation
  offset = 8;
  test_ptr_w_offset = (void *)((unsigned long long)test_ptr + offset);
  ACL_LOCKED(test_shared_alloc2 =
                 acl_get_usm_alloc_from_ptr(m_context, test_ptr_w_offset));
  CHECK(test_shared_alloc2 == NULL);

  // Done internal function test
  clMemFreeINTEL(m_context, test_ptr);
  CHECK(m_context->usm_allocation.empty());

  // Default works and defaults to 0
  test_ptr =
      clSharedMemAllocINTEL(m_context, m_device[0], NULL, 8, alignment, NULL);
  CHECK(test_ptr != NULL);
  CHECK(!m_context->usm_allocation.empty());
  ACL_LOCKED(test_shared_alloc =
                 acl_get_usm_alloc_from_ptr(m_context, test_ptr));
  CHECK(test_shared_alloc != NULL);
  CHECK(test_shared_alloc);
  CHECK_EQUAL(test_shared_alloc->range.begin, test_ptr);
  CHECK_EQUAL(test_shared_alloc->alloc_flags, 0);
  clMemFreeINTEL(m_context, test_ptr);
  CHECK(m_context->usm_allocation.empty());

  // bad size
  test_ptr =
      clSharedMemAllocINTEL(m_context, m_device[0], NULL, 0, alignment, NULL);
  CHECK(test_ptr == NULL);
  test_ptr = clSharedMemAllocINTEL(m_context, m_device[0], NULL, 0xffffffff,
                                   alignment, NULL);
  CHECK(test_ptr == NULL);

  cl_ulong max_alloc = 0;
  clGetDeviceInfo(m_device[0], CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(max_alloc),
                  &max_alloc, 0);
  test_ptr = clSharedMemAllocINTEL(m_context, m_device[0], NULL,
                                   (size_t)(max_alloc + 1), alignment, NULL);
  CHECK(test_ptr == NULL);

  // bad alignment: Not a power of 2
  test_ptr = clSharedMemAllocINTEL(m_context, m_device[0], NULL, 8, 3, NULL);
  CHECK(test_ptr == NULL);
  // bad alignment: Not supported by any device
  // All devices currently use ACL_MEM_ALIGN. Try ACL_MEM_ALIGN * 2.
  // If this ever changes (and therefore this test starts to fail) the test
  // should be updated to capture the new expected behaviour.
  test_ptr = clSharedMemAllocINTEL(m_context, m_device[0], NULL, 8,
                                   ACL_MEM_ALIGN * 2, NULL);
  CHECK(test_ptr == NULL);

  // Alignment 0 passes
  test_ptr = clSharedMemAllocINTEL(m_context, m_device[0], NULL, 8, 0, NULL);
  CHECK(test_ptr != NULL);
  clMemFreeINTEL(m_context, test_ptr);
  CHECK(m_context->usm_allocation.empty());

  // Multiple allocs & frees work
  test_ptr =
      clSharedMemAllocINTEL(m_context, m_device[0], NULL, 8, alignment, NULL);
  CHECK(test_ptr != NULL);
  void *test_ptr2 =
      clSharedMemAllocINTEL(m_context, m_device[0], NULL, 8, alignment, NULL);
  CHECK(test_ptr2 != NULL);
  ACL_LOCKED(test_shared_alloc =
                 acl_get_usm_alloc_from_ptr(m_context, test_ptr));
  CHECK(test_shared_alloc != NULL);
  CHECK(test_shared_alloc);
  CHECK_EQUAL(test_shared_alloc->range.begin, test_ptr);
  ACL_LOCKED(test_shared_alloc =
                 acl_get_usm_alloc_from_ptr(m_context, test_ptr2));
  CHECK(test_shared_alloc != NULL);
  CHECK(test_shared_alloc);
  CHECK_EQUAL(test_shared_alloc->range.begin, test_ptr2);
  clMemFreeINTEL(m_context, test_ptr);
  clMemFreeINTEL(m_context, test_ptr2);
  CHECK(m_context->usm_allocation.empty());

  ACL_LOCKED(acl_print_debug_msg("end alloc_and_free_shared_usm\n"));
}

MT_TEST(acl_usm, alloc_and_free_host_usm) {
  ACL_LOCKED(acl_print_debug_msg("begin alloc_and_free_host_usm\n"));
  const int alignment = 16;
  cl_int status;
  this->yeah = true;

  acl_usm_allocation_t *test_alloc;

  cl_mem_properties_intel bad_property1[3] = {
      CL_MEM_ALLOC_FLAGS_INTEL - 1, // Undefined property
      CL_MEM_ALLOC_WRITE_COMBINED_INTEL, 0};
  cl_mem_properties_intel bad_property2[5] = {
      CL_MEM_ALLOC_FLAGS_INTEL, CL_MEM_ALLOC_WRITE_COMBINED_INTEL,
      CL_MEM_ALLOC_FLAGS_INTEL - 1, // Undefined property
      CL_MEM_ALLOC_WRITE_COMBINED_INTEL, 0};
  cl_mem_properties_intel good_property1[3] = {
      CL_MEM_ALLOC_FLAGS_INTEL, CL_MEM_ALLOC_WRITE_COMBINED_INTEL, 0};

  cl_context context =
      clCreateContext(acl_test_context_prop_preloaded_binary_only(), 1,
                      &(m_device[0]), NULL, NULL, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

  // Alloc & free is error free
  void *test_ptr = clHostMemAllocINTEL(context, NULL, 8, alignment, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(test_ptr != NULL);
  CHECK(!context->usm_allocation.empty());
  ACL_LOCKED(
      CHECK_EQUAL(acl_usm_ptr_belongs_to_context(context, test_ptr), CL_TRUE));
  status = clMemFreeINTEL(context, test_ptr);
  CHECK_EQUAL(status, CL_SUCCESS);
  ACL_LOCKED(
      CHECK_EQUAL(acl_usm_ptr_belongs_to_context(context, test_ptr), CL_FALSE));
  CHECK(context->usm_allocation.empty());

  // Bad context
  test_ptr = clHostMemAllocINTEL(0, NULL, 8, alignment, &status);
  CHECK(test_ptr == NULL);
  CHECK(status != CL_SUCCESS);
  status = clMemFreeINTEL(0, 0);
  CHECK(status != CL_SUCCESS);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  test_ptr = clHostMemAllocINTEL(&fake_context, NULL, 8, alignment, &status);
  CHECK(test_ptr == NULL);
  CHECK(status != CL_SUCCESS);
  status = clMemFreeINTEL(&fake_context, 0);
  CHECK(status != CL_SUCCESS);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();

  // Invalid Property
  // Bad bits: First property is invalid
  test_ptr =
      clHostMemAllocINTEL(context, &(bad_property1[0]), 8, alignment, &status);
  CHECK(test_ptr == NULL);
  CHECK(status != CL_SUCCESS);
  // Bad bits: Second property is invalid
  test_ptr =
      clHostMemAllocINTEL(context, &(bad_property2[0]), 8, alignment, &status);
  CHECK(test_ptr == NULL);
  CHECK(status != CL_SUCCESS);

  // Good bits & produce the correct USM allocation in context
  test_ptr =
      clHostMemAllocINTEL(context, &(good_property1[0]), 8, alignment, &status);
  CHECK_EQUAL(status, CL_SUCCESS);
  CHECK(test_ptr != NULL);
  CHECK(!context->usm_allocation.empty());

  ACL_LOCKED(
      CHECK_EQUAL(acl_usm_ptr_belongs_to_context(context, test_ptr), CL_TRUE));
  ACL_LOCKED(test_alloc = acl_get_usm_alloc_from_ptr(context, test_ptr));
  CHECK(test_alloc != NULL);
  CHECK(test_alloc);
  CHECK_EQUAL(test_alloc->range.begin, test_ptr);
  CHECK_EQUAL(test_alloc->alloc_flags, CL_MEM_ALLOC_WRITE_COMBINED_INTEL);

  // Test internal functions for success
  // Offset is less than size
  size_t offset = 7;
  acl_usm_allocation_t *test_alloc2 = NULL;
  void *test_ptr_w_offset = (void *)((unsigned long long)test_ptr + offset);
  ACL_LOCKED(test_alloc2 =
                 acl_get_usm_alloc_from_ptr(context, test_ptr_w_offset));
  CHECK(test_alloc2 != NULL);
  CHECK_EQUAL(test_alloc2, test_alloc);

  // Test internal functions for failure
  // Offset is equal to size of allocation
  offset = 8;
  test_ptr_w_offset = (void *)((unsigned long long)test_ptr + offset);
  ACL_LOCKED(test_alloc2 =
                 acl_get_usm_alloc_from_ptr(context, test_ptr_w_offset));
  CHECK(test_alloc2 == NULL);

  // Done internal function test
  clMemFreeINTEL(context, test_ptr);
  CHECK(context->usm_allocation.empty());

  // Default works and defaults to 0
  test_ptr = clHostMemAllocINTEL(context, NULL, 8, alignment, NULL);
  CHECK(test_ptr != NULL);
  CHECK(!context->usm_allocation.empty());
  ACL_LOCKED(test_alloc = acl_get_usm_alloc_from_ptr(context, test_ptr));
  CHECK(test_alloc != NULL);
  CHECK(test_alloc);
  CHECK_EQUAL(test_alloc->range.begin, test_ptr);
  CHECK_EQUAL(test_alloc->alloc_flags, 0);
  clMemFreeINTEL(context, test_ptr);
  CHECK(context->usm_allocation.empty());

  // bad size
  test_ptr = clHostMemAllocINTEL(context, NULL, 0, alignment, NULL);
  CHECK(test_ptr == NULL);
  test_ptr = clHostMemAllocINTEL(context, NULL, 0xffffffff, alignment, NULL);
  CHECK(test_ptr == NULL);

  cl_ulong max_alloc = 0;
  clGetDeviceInfo(m_device[0], CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(max_alloc),
                  &max_alloc, 0);
  test_ptr = clHostMemAllocINTEL(context, NULL, (size_t)(max_alloc + 1),
                                 alignment, NULL);
  CHECK(test_ptr == NULL);

  // bad alignment: Not a power of 2
  test_ptr = clHostMemAllocINTEL(context, NULL, 8, 3, NULL);
  CHECK(test_ptr == NULL);
  // bad alignment: Not supported by any device
  // All devices currently use ACL_MEM_ALIGN. Try ACL_MEM_ALIGN * 2.
  // If this ever changes (and therefore this test starts to fail) the test
  // should be updated to capture the new expected behavior.
  test_ptr = clHostMemAllocINTEL(context, NULL, 8, ACL_MEM_ALIGN * 2, NULL);
  CHECK(test_ptr == NULL);

  // Alignment 0 passes
  test_ptr = clHostMemAllocINTEL(context, NULL, 8, 0, NULL);
  CHECK(test_ptr != NULL);
  clMemFreeINTEL(context, test_ptr);
  CHECK(context->usm_allocation.empty());

  // Multiple allocs & frees work
  test_ptr = clHostMemAllocINTEL(context, NULL, 8, alignment, NULL);
  CHECK(test_ptr != NULL);
  void *test_ptr2 = clHostMemAllocINTEL(context, NULL, 8, alignment, NULL);
  CHECK(test_ptr2 != NULL);
  ACL_LOCKED(test_alloc = acl_get_usm_alloc_from_ptr(context, test_ptr));
  CHECK(test_alloc != NULL);
  CHECK(test_alloc);
  CHECK_EQUAL(test_alloc->range.begin, test_ptr);
  ACL_LOCKED(test_alloc = acl_get_usm_alloc_from_ptr(context, test_ptr2));
  CHECK(test_alloc != NULL);
  CHECK(test_alloc);
  CHECK_EQUAL(test_alloc->range.begin, test_ptr2);
  clMemFreeINTEL(context, test_ptr);
  clMemFreeINTEL(context, test_ptr2);
  CHECK(context->usm_allocation.empty());

  clReleaseContext(context);

  // host_alloc not supported by device
  context = clCreateContext(acl_test_context_prop_preloaded_binary_only(), 1,
                            &(m_device[1]), NULL, NULL, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));
  test_ptr = clHostMemAllocINTEL(context, NULL, 8, alignment, &status);
  CHECK_EQUAL(CL_INVALID_OPERATION, status);
  clReleaseContext(context);

  ACL_LOCKED(acl_print_debug_msg("end alloc_and_free_shared_usm\n"));
}

MT_TEST(acl_usm, meminfo_usm) {
  ACL_LOCKED(acl_print_debug_msg("begin meminfo_usm\n"));
  cl_int status;
  this->yeah = true;
  int testsize = 10;

  // Allocation argument values
  cl_mem_properties_intel good_property[3] = {
      CL_MEM_ALLOC_FLAGS_INTEL, CL_MEM_ALLOC_WRITE_COMBINED_INTEL, 0};

  void *dev_ptr =
      clDeviceMemAllocINTEL(m_context, m_device[0], &(good_property[0]),
                            testsize * sizeof(int), 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(dev_ptr != NULL);

  cl_unified_shared_memory_type_intel type;
  size_t ret_size;

  // Bad context
  status = clGetMemAllocInfoINTEL(0, dev_ptr, CL_MEM_ALLOC_TYPE_INTEL,
                                  sizeof(type), &type, &ret_size);
  CHECK(status != CL_SUCCESS);

  // Invalid param name
  status = clGetMemAllocInfoINTEL(m_context, dev_ptr, 0, sizeof(type), &type,
                                  &ret_size);
  CHECK(status != CL_SUCCESS);

  // Invalid param value size
  status = clGetMemAllocInfoINTEL(m_context, dev_ptr, CL_MEM_ALLOC_TYPE_INTEL,
                                  0, &type, &ret_size);
  CHECK(status != CL_SUCCESS);

  // Invalid ptr results in success
  status = clGetMemAllocInfoINTEL(m_context, 0, CL_MEM_ALLOC_TYPE_INTEL,
                                  sizeof(type), &type, &ret_size);
  CHECK_EQUAL(status, CL_SUCCESS);
  CHECK_EQUAL(sizeof(cl_unified_shared_memory_type_intel), ret_size);
  CHECK_EQUAL(CL_MEM_TYPE_UNKNOWN_INTEL, type);

  // Pointer beyond allocation range
  status = clGetMemAllocInfoINTEL(
      m_context, (char *)dev_ptr + testsize * sizeof(int),
      CL_MEM_ALLOC_TYPE_INTEL, sizeof(type), &type, &ret_size);
  CHECK_EQUAL(status, CL_SUCCESS);
  CHECK_EQUAL(sizeof(cl_unified_shared_memory_type_intel), ret_size);
  CHECK_EQUAL(CL_MEM_TYPE_UNKNOWN_INTEL, type);

  // Allocation type
  status = clGetMemAllocInfoINTEL(m_context, dev_ptr, CL_MEM_ALLOC_TYPE_INTEL,
                                  sizeof(type), &type, &ret_size);

  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(sizeof(cl_unified_shared_memory_type_intel), ret_size);
  CHECK_EQUAL(CL_MEM_TYPE_DEVICE_INTEL, type);

  // Base ptr
  void *base_ptr;
  status =
      clGetMemAllocInfoINTEL(m_context, dev_ptr, CL_MEM_ALLOC_BASE_PTR_INTEL,
                             sizeof(base_ptr), &base_ptr, &ret_size);

  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(sizeof(void *), ret_size);
  CHECK_EQUAL(base_ptr, dev_ptr);

  // On USM ptr with offset
  status = clGetMemAllocInfoINTEL(
      m_context, (char *)dev_ptr + testsize * sizeof(int) - 1,
      CL_MEM_ALLOC_BASE_PTR_INTEL, sizeof(base_ptr), &base_ptr, &ret_size);

  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(sizeof(void *), ret_size);
  CHECK_EQUAL(base_ptr, dev_ptr);

  // Allocation size
  size_t alloc_size;
  status = clGetMemAllocInfoINTEL(m_context, dev_ptr, CL_MEM_ALLOC_SIZE_INTEL,
                                  sizeof(alloc_size), &alloc_size, &ret_size);

  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(sizeof(size_t), ret_size);
  CHECK_EQUAL(testsize * sizeof(int), alloc_size);

  // Allocation device
  cl_device_id alloc_device;
  status =
      clGetMemAllocInfoINTEL(m_context, dev_ptr, CL_MEM_ALLOC_DEVICE_INTEL,
                             sizeof(alloc_device), &alloc_device, &ret_size);

  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(sizeof(cl_device_id), ret_size);
  CHECK_EQUAL(m_device[0], alloc_device);

  // Allocation flags
  cl_mem_alloc_flags_intel alloc_flags;
  status = clGetMemAllocInfoINTEL(m_context, dev_ptr, CL_MEM_ALLOC_FLAGS_INTEL,
                                  sizeof(alloc_flags), &alloc_flags, &ret_size);

  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(sizeof(cl_mem_alloc_flags_intel), ret_size);
  CHECK_EQUAL(CL_MEM_ALLOC_WRITE_COMBINED_INTEL, alloc_flags);

  clMemFreeINTEL(m_context, dev_ptr);

  // Allocation without property
  dev_ptr = clDeviceMemAllocINTEL(m_context, m_device[0], NULL,
                                  testsize * sizeof(int), 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(dev_ptr != NULL);

  // Allocation flags should be 0
  status = clGetMemAllocInfoINTEL(m_context, dev_ptr, CL_MEM_ALLOC_FLAGS_INTEL,
                                  sizeof(alloc_flags), &alloc_flags, &ret_size);

  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(sizeof(cl_mem_alloc_flags_intel), ret_size);
  CHECK_EQUAL(0, alloc_flags);

  clMemFreeINTEL(m_context, dev_ptr);

  ACL_LOCKED(acl_print_debug_msg("end meminfo_usm\n"));
}

MT_TEST(acl_usm, memcpy_usm) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  ACL_LOCKED(acl_print_debug_msg("begin memcpy_usm\n"));
  this->yeah = true;
  int testsize = 10;

  cl_int status;
  cl_event event;

  ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

  int *system_ptr1 = (int *)acl_malloc(testsize * sizeof(int));
  int *system_ptr2 = (int *)acl_malloc(testsize * sizeof(int));
  void *dev_ptr1 = clDeviceMemAllocINTEL(m_context, m_device[0], NULL,
                                         testsize * sizeof(int), 0, NULL);
  void *dev_ptr2 = clDeviceMemAllocINTEL(m_context, m_device[0], NULL,
                                         testsize * sizeof(int), 0, NULL);

  CHECK(system_ptr1 != NULL);
  CHECK(system_ptr2 != NULL);
  CHECK(dev_ptr1 != NULL);
  CHECK(dev_ptr2 != NULL);

  for (int i = 0; i < testsize; ++i) {
    CHECK(system_ptr1);
    CHECK(system_ptr2);
    system_ptr1[i] = i;
    system_ptr2[i] = 0;
  }

  // Bad command queue
  status = clEnqueueMemcpyINTEL(0, CL_TRUE, dev_ptr1, system_ptr1,
                                testsize * sizeof(int), 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, status);

  // Invalid wait list
  status = clEnqueueMemcpyINTEL(m_cq, CL_TRUE, dev_ptr1, system_ptr1,
                                testsize * sizeof(int), 0, &event, NULL);
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST, status);
  status = clEnqueueMemcpyINTEL(m_cq, CL_TRUE, dev_ptr1, system_ptr1,
                                testsize * sizeof(int), 1, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST, status);

  // Invalid values
  status = clEnqueueMemcpyINTEL(m_cq, CL_TRUE, NULL, system_ptr1,
                                testsize * sizeof(int), 0, NULL, &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueMemcpyINTEL(m_cq, CL_TRUE, dev_ptr1, NULL,
                                testsize * sizeof(int), 0, NULL, &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueMemcpyINTEL(m_cq, CL_TRUE, dev_ptr1, system_ptr1, 0, 0,
                                NULL, &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Overlap
  status = clEnqueueMemcpyINTEL(m_cq, CL_TRUE, &(((int *)dev_ptr1)[0]),
                                &(((int *)dev_ptr1)[1]), testsize * sizeof(int),
                                0, NULL, &event);
  CHECK_EQUAL(CL_MEM_COPY_OVERLAP, status);

  // Successful operation host-to-dev
  status = clEnqueueMemcpyINTEL(m_cq, CL_TRUE, dev_ptr1, system_ptr1,
                                testsize * sizeof(int), 0, NULL, &event);
  CHECK_EQUAL(CL_SUCCESS, status);

  clFinish(m_cq);

  {
    // acltest_translate_device_address has to come after clEnqueueMemcpy of USM
    // ptr since emulated memory allocation happens under the hood on first HAL
    // copy API.
    const int *const dev_ptr1_hostmem =
        (const int *)acltest_translate_device_address(dev_ptr1, 0);

    for (int i = 0; i < testsize; ++i) {
      CHECK_EQUAL(system_ptr1[i], dev_ptr1_hostmem[i]);
      CHECK_EQUAL(i, dev_ptr1_hostmem[i]);
    }
  }

  cl_command_type type;
  size_t ret_size;
  status = clGetEventInfo(event, CL_EVENT_COMMAND_TYPE, sizeof(cl_command_type),
                          &type, &ret_size);

  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(ret_size, sizeof(cl_command_type));
  CHECK_EQUAL(CL_COMMAND_MEMCPY_INTEL, type);

  status = clReleaseEvent(event);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Successful operation dev-to-dev
  {
    int *const dev_ptr1_hostmem =
        (int *)acltest_translate_device_address(dev_ptr1, 0);

    for (int i = 0; i < testsize; ++i) {
      dev_ptr1_hostmem[i] = testsize - i;
    }
  }

  status = clEnqueueMemcpyINTEL(m_cq, CL_TRUE, dev_ptr2, dev_ptr1,
                                testsize * sizeof(int), 0, NULL, &event);
  CHECK_EQUAL(CL_SUCCESS, status);

  clFinish(m_cq);

  {
    // acltest_translate_device_address has to come after clEnqueueMemcpy of USM
    // ptr since emulated memory allocation happens under the hood on first HAL
    // copy API.
    const int *const dev_ptr1_hostmem =
        (const int *)acltest_translate_device_address(dev_ptr1, 0);
    const int *const dev_ptr2_hostmem =
        (const int *)acltest_translate_device_address(dev_ptr2, 0);

    for (int i = 0; i < testsize; ++i) {
      CHECK_EQUAL(dev_ptr1_hostmem[i], dev_ptr2_hostmem[i]);
      CHECK_EQUAL(testsize - i, dev_ptr2_hostmem[i]);
    }
  }

  status = clReleaseEvent(event);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Successful operation dev-to-host
  status = clEnqueueMemcpyINTEL(m_cq, CL_TRUE, system_ptr2, dev_ptr2,
                                testsize * sizeof(int), 0, NULL, &event);
  CHECK_EQUAL(CL_SUCCESS, status);

  clFinish(m_cq);

  {
    const int *const dev_ptr2_hostmem =
        (const int *)acltest_translate_device_address(dev_ptr2, 0);

    for (int i = 0; i < testsize; ++i) {
      CHECK_EQUAL(system_ptr2[i], dev_ptr2_hostmem[i]);
      CHECK_EQUAL(testsize - i, system_ptr2[i]);
    }
  }

  status = clReleaseEvent(event);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Successful operation host-to-host
  // Check system_ptr1 hasn't changed since being initialized
  for (int i = 0; i < testsize; ++i) {
    CHECK_EQUAL(i, system_ptr1[i]);
  }

  status = clEnqueueMemcpyINTEL(m_cq, CL_TRUE, system_ptr1, system_ptr2,
                                testsize * sizeof(int), 0, NULL, &event);
  CHECK_EQUAL(CL_SUCCESS, status);

  clFinish(m_cq);

  for (int i = 0; i < testsize; ++i) {
    CHECK_EQUAL(system_ptr2[i], system_ptr1[i]);
    CHECK_EQUAL(testsize - i, system_ptr1[i]);
  }

  status = clReleaseEvent(event);
  CHECK_EQUAL(CL_SUCCESS, status);

  clMemFreeINTEL(m_context, dev_ptr2);
  clMemFreeINTEL(m_context, dev_ptr1);
  acl_free(system_ptr2);
  acl_free(system_ptr1);

  ACL_LOCKED(acl_print_debug_msg("end memcpy_usm\n"));
}

MT_TEST(acl_usm, memfill_usm) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  ACL_LOCKED(acl_print_debug_msg("begin memfill_usm\n"));
  this->yeah = true;
  int pattern = 5;
  cl_int status;
  cl_event event;
  int testsize = 10;

  ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

  int *sys_ptr = (int *)acl_malloc(testsize * sizeof(int));
  void *dev_ptr = clDeviceMemAllocINTEL(m_context, m_device[0], NULL,
                                        testsize * sizeof(int), 0, NULL);

  CHECK(sys_ptr != NULL);
  CHECK(dev_ptr != NULL);

  for (int i = 0; i < testsize; ++i) {
    CHECK(sys_ptr);
    sys_ptr[i] = 0;
  }

  // Bad command queue
  status = clEnqueueMemFillINTEL(0, dev_ptr, &pattern, sizeof(int),
                                 testsize * sizeof(int), 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, status);

  // Invalid wait list
  cl_event event_wait_list[1];
  status =
      clEnqueueMemFillINTEL(m_cq, dev_ptr, &pattern, sizeof(int),
                            testsize * sizeof(int), 0, event_wait_list, NULL);
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST, status);
  status = clEnqueueMemFillINTEL(m_cq, dev_ptr, &pattern, sizeof(int),
                                 testsize * sizeof(int), 1, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST, status);

  // Invalid values
  status = clEnqueueMemFillINTEL(m_cq, NULL, &pattern, sizeof(int),
                                 testsize * sizeof(int), 0, NULL, &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueMemFillINTEL(m_cq, dev_ptr, NULL, sizeof(int),
                                 testsize * sizeof(int), 0, NULL, &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueMemFillINTEL(m_cq, dev_ptr, &pattern, 3,
                                 testsize * sizeof(int), 0, NULL, &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueMemFillINTEL(m_cq, dev_ptr, &pattern, sizeof(int), 0, 0,
                                 NULL, &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueMemFillINTEL(m_cq, dev_ptr, &pattern, sizeof(int), 7, 0,
                                 NULL, &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Successful fill to device mem
  status = clEnqueueMemFillINTEL(m_cq, dev_ptr, &pattern, sizeof(int),
                                 testsize * sizeof(int), 0, NULL, &event);
  CHECK_EQUAL(CL_SUCCESS, status);

  clFinish(m_cq);

  // acltest_translate_device_address has to come after clEnqueueMemcpy of USM
  // ptr since emulated memory allocation happens under the hood on first HAL
  // copy API.
  int *dev_ptr_hostmem = (int *)acltest_translate_device_address(dev_ptr, 0);

  for (int i = 0; i < testsize; ++i) {
    CHECK_EQUAL(pattern, dev_ptr_hostmem[i]);
  }

  cl_command_type type;
  size_t ret_size;
  status = clGetEventInfo(event, CL_EVENT_COMMAND_TYPE, sizeof(cl_command_type),
                          &type, &ret_size);

  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(ret_size, sizeof(cl_command_type));
  CHECK_EQUAL(CL_COMMAND_MEMFILL_INTEL, type);

  status = clReleaseEvent(event);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Successful fill to system mem
  status = clEnqueueMemFillINTEL(m_cq, sys_ptr, &pattern, sizeof(int),
                                 testsize * sizeof(int), 0, NULL, &event);
  CHECK_EQUAL(CL_SUCCESS, status);

  clFinish(m_cq);

  for (int i = 0; i < testsize; ++i) {
    CHECK_EQUAL(pattern, sys_ptr[i]);
  }
  status = clReleaseEvent(event);
  CHECK_EQUAL(CL_SUCCESS, status);

  clMemFreeINTEL(m_context, (void *)dev_ptr);
  acl_free(sys_ptr);

  ACL_LOCKED(acl_print_debug_msg("end memfill_usm\n"));
}

#endif // __arm__
