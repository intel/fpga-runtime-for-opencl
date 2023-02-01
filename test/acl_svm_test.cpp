// Copyright (C) 2014-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

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
#include <acl_svm.h>
#include <acl_types.h>
#include <acl_util.h>
#include <string.h>

#include "acl_hal_test.h"
#include "acl_test.h"

#include <cstdio>

static void CL_CALLBACK notify_me_print(const char *errinfo,
                                        const void *private_info, size_t cb,
                                        void *user_data);

MT_TEST_GROUP(acl_svm) {
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

    load_normal_context();

    // Just grab devices that are present.
    CHECK(m_device[0]);
    CHECK(m_device[0]->present);
    CHECK(m_device[1]);
    CHECK(m_device[1]->present);
    CHECK(m_device[2]);
    CHECK(m_device[2]->present);
    m_num_devices = 3;

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
  CppUTestGroupacl_svm *inst = (CppUTestGroupacl_svm *)user_data;
  if (inst) {
    if (inst->yeah)
      printf("Context error: %s\n", errinfo);
  }
  cb = cb;                     // avoid warning on windows
  private_info = private_info; // avoid warning on windows
}
#endif

MT_TEST(acl_svm, alloc_and_free_svm) {
  ACL_LOCKED(acl_print_debug_msg("begin alloc_and_free_svm\n"));
  const int alignment = ACL_MEM_ALIGN;
  this->yeah = true;
  void *test_ptr = NULL;
  void *test_ptr2 = NULL;
  acl_svm_entry_t *test_svm_entry;

  cl_svm_mem_flags bad_bits =
      (CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY |
       CL_MEM_SVM_FINE_GRAIN_BUFFER | CL_MEM_SVM_ATOMICS)
      << 1;

  ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

  // Alloc & free is error free
  test_ptr = clSVMAlloc(m_context, CL_MEM_READ_WRITE, 8, alignment);
  CHECK(test_ptr != NULL);
  CHECK(m_context->svm_list != NULL);
  ACL_LOCKED(CHECK_EQUAL(acl_ptr_is_exactly_in_context_svm(m_context, test_ptr),
                         CL_TRUE));
  clSVMFree(m_context, test_ptr);
  ACL_LOCKED(CHECK_EQUAL(acl_ptr_is_exactly_in_context_svm(m_context, test_ptr),
                         CL_FALSE));
  CHECK_EQUAL(m_context->svm_list, 0);

  // Bad context
  test_ptr = clSVMAlloc(0, CL_MEM_READ_WRITE, 8, alignment);
  CHECK_EQUAL(0, test_ptr);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  test_ptr = clSVMAlloc(&fake_context, CL_MEM_READ_WRITE, 8, alignment);
  CHECK_EQUAL(0, test_ptr);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();

  // Bad bits
  test_ptr = clSVMAlloc(m_context, bad_bits, 8, alignment);
  CHECK_EQUAL(0, test_ptr);
  // Bad bits: conflicts around r/w specs
  test_ptr =
      clSVMAlloc(m_context, CL_MEM_READ_WRITE | CL_MEM_READ_ONLY, 8, alignment);
  CHECK_EQUAL(0, test_ptr);
  test_ptr = clSVMAlloc(m_context, CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY, 8,
                        alignment);
  CHECK_EQUAL(0, test_ptr);
  test_ptr =
      clSVMAlloc(m_context, CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY, 8, alignment);
  CHECK_EQUAL(0, test_ptr);
  // Good bits work & product the correct SVM in context
  test_ptr = clSVMAlloc(m_context, CL_MEM_READ_WRITE, 8, alignment);
  CHECK(test_ptr != NULL);
  CHECK(m_context->svm_list != NULL);
  ACL_LOCKED(CHECK_EQUAL(acl_ptr_is_exactly_in_context_svm(m_context, test_ptr),
                         CL_TRUE));
  ACL_LOCKED(test_svm_entry = acl_get_svm_entry(m_context, test_ptr));
  CHECK(test_svm_entry != NULL);
  CHECK_EQUAL(test_svm_entry->ptr, test_ptr);
  CHECK_EQUAL(test_svm_entry->read_only, CL_FALSE);
  CHECK_EQUAL(test_svm_entry->write_only, CL_FALSE);
  clSVMFree(m_context, test_ptr);
  CHECK_EQUAL(m_context->svm_list, 0);
  test_ptr = clSVMAlloc(m_context, CL_MEM_READ_ONLY, 8, alignment);
  CHECK(test_ptr != NULL);
  CHECK(m_context->svm_list != NULL);
  ACL_LOCKED(CHECK_EQUAL(acl_ptr_is_exactly_in_context_svm(m_context, test_ptr),
                         CL_TRUE));
  ACL_LOCKED(test_svm_entry = acl_get_svm_entry(m_context, test_ptr));
  CHECK(test_svm_entry != NULL);
  CHECK_EQUAL(test_svm_entry->ptr, test_ptr);
  CHECK_EQUAL(test_svm_entry->read_only, CL_TRUE);
  CHECK_EQUAL(test_svm_entry->write_only, CL_FALSE);
  clSVMFree(m_context, test_ptr);
  CHECK_EQUAL(m_context->svm_list, 0);
  test_ptr = clSVMAlloc(m_context, CL_MEM_WRITE_ONLY, 8, alignment);
  CHECK(test_ptr != NULL);
  CHECK(m_context->svm_list != NULL);
  ACL_LOCKED(CHECK_EQUAL(acl_ptr_is_exactly_in_context_svm(m_context, test_ptr),
                         CL_TRUE));
  ACL_LOCKED(test_svm_entry = acl_get_svm_entry(m_context, test_ptr));
  CHECK(test_svm_entry != NULL);
  CHECK_EQUAL(test_svm_entry->ptr, test_ptr);
  CHECK_EQUAL(test_svm_entry->read_only, CL_FALSE);
  CHECK_EQUAL(test_svm_entry->write_only, CL_TRUE);
  clSVMFree(m_context, test_ptr);
  CHECK_EQUAL(m_context->svm_list, 0);
  // Default works and defaults to CL_MEM_READ_WRITE
  test_ptr = clSVMAlloc(m_context, 0, 8, alignment);
  CHECK(test_ptr != NULL);
  CHECK(m_context->svm_list != NULL);
  ACL_LOCKED(test_svm_entry = acl_get_svm_entry(m_context, test_ptr));
  CHECK(test_svm_entry != NULL);
  CHECK_EQUAL(test_svm_entry->ptr, test_ptr);
  CHECK_EQUAL(test_svm_entry->read_only, CL_FALSE);
  CHECK_EQUAL(test_svm_entry->write_only, CL_FALSE);
  clSVMFree(m_context, test_ptr);
  CHECK_EQUAL(m_context->svm_list, 0);
  // Bad bits: Cannot specify SVM atomics without fine grain
  test_ptr = clSVMAlloc(m_context, CL_MEM_READ_WRITE | CL_MEM_SVM_ATOMICS, 8,
                        alignment);
  CHECK_EQUAL(0, test_ptr);

  // Temporary bad bits: We don't support SVM atomics or fine grain
  // If this changes (and therefore this test starts to fail) then this test
  // should be updated to capture the new expected behaviour.
  test_ptr =
      clSVMAlloc(m_context, CL_MEM_READ_WRITE | CL_MEM_SVM_FINE_GRAIN_BUFFER, 8,
                 alignment);
  CHECK_EQUAL(0, test_ptr);
  test_ptr = clSVMAlloc(m_context,
                        CL_MEM_READ_WRITE | CL_MEM_SVM_FINE_GRAIN_BUFFER |
                            CL_MEM_SVM_ATOMICS,
                        8, alignment);
  CHECK_EQUAL(0, test_ptr);

  // bad size
  test_ptr = clSVMAlloc(m_context, CL_MEM_READ_WRITE, 0, alignment);
  CHECK_EQUAL(0, test_ptr);
  test_ptr = clSVMAlloc(m_context, CL_MEM_READ_WRITE, 0xffffffff, alignment);
  CHECK_EQUAL(0, test_ptr);
  test_ptr = clSVMAlloc(m_context, CL_MEM_READ_WRITE,
                        m_context->max_mem_alloc_size + 1, alignment);
  CHECK_EQUAL(0, test_ptr);

  // bad alignment: Not a power of 2
  test_ptr = clSVMAlloc(m_context, CL_MEM_READ_WRITE, 8, 3);
  CHECK_EQUAL(0, test_ptr);
  // bad alignment: Not supported by any device
  // All devices currently use ACL_MEM_ALIGN. Try ACL_MEM_ALIGN / 2.
  // If this ever changes (and therefore this test starts to fail) the test
  // should be updated to capture the new expected behaviour.
  test_ptr = clSVMAlloc(m_context, CL_MEM_READ_WRITE, 8, ACL_MEM_ALIGN / 2);
  CHECK_EQUAL(0, test_ptr);

  // Alignment 0 passes
  test_ptr = clSVMAlloc(m_context, CL_MEM_READ_WRITE, 8, 0);
  CHECK(test_ptr != NULL);
  clSVMFree(m_context, test_ptr);
  CHECK_EQUAL(m_context->svm_list, 0);

  // Multiple allocs & frees work
  test_ptr = clSVMAlloc(m_context, CL_MEM_READ_WRITE, 8, alignment);
  CHECK(test_ptr != NULL);
  test_ptr2 = clSVMAlloc(m_context, CL_MEM_READ_WRITE, 8, alignment);
  CHECK(test_ptr2 != NULL);
  ACL_LOCKED(test_svm_entry = acl_get_svm_entry(m_context, test_ptr));
  CHECK(test_svm_entry != NULL);
  CHECK_EQUAL(test_svm_entry->ptr, test_ptr);
  ACL_LOCKED(test_svm_entry = acl_get_svm_entry(m_context, test_ptr2));
  CHECK(test_svm_entry != NULL);
  CHECK_EQUAL(test_svm_entry->ptr, test_ptr2);
  clSVMFree(m_context, test_ptr);
  ACL_LOCKED(test_svm_entry = acl_get_svm_entry(m_context, test_ptr));
  CHECK(test_svm_entry == NULL);
  ACL_LOCKED(test_svm_entry = acl_get_svm_entry(m_context, test_ptr2));
  CHECK(test_svm_entry != NULL);
  CHECK_EQUAL(test_svm_entry->ptr, test_ptr2);
  clSVMFree(m_context, test_ptr2);
  CHECK_EQUAL(m_context->svm_list, 0);

  // Multiple allocs & frees work in reverse order
  test_ptr = clSVMAlloc(m_context, CL_MEM_READ_WRITE, 8, alignment);
  CHECK(test_ptr != NULL);
  test_ptr2 = clSVMAlloc(m_context, CL_MEM_READ_WRITE, 8, alignment);
  CHECK(test_ptr2 != NULL);
  clSVMFree(m_context, test_ptr2);
  ACL_LOCKED(test_svm_entry = acl_get_svm_entry(m_context, test_ptr2));
  CHECK(test_svm_entry == NULL);
  ACL_LOCKED(test_svm_entry = acl_get_svm_entry(m_context, test_ptr));
  CHECK(test_svm_entry != NULL);
  CHECK_EQUAL(test_svm_entry->ptr, test_ptr);
  clSVMFree(m_context, test_ptr);
  CHECK_EQUAL(m_context->svm_list, 0);

  ACL_LOCKED(acl_print_debug_msg("end alloc_and_free_svm\n"));
}

MT_TEST(acl_svm, memcpy_svm) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  ACL_LOCKED(acl_print_debug_msg("begin memcpy_svm\n"));
  this->yeah = true;
  int *src_ptr = NULL;
  int *dst_ptr = NULL;
  cl_int status;
  int i;
  cl_event event;

  ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

  src_ptr =
      (int *)clSVMAlloc(m_context, CL_MEM_READ_WRITE, 10 * sizeof(int), 0);
  dst_ptr =
      (int *)clSVMAlloc(m_context, CL_MEM_READ_WRITE, 10 * sizeof(int), 0);

  CHECK(src_ptr != NULL);
  CHECK(dst_ptr != NULL);

  for (i = 0; i < 10; ++i) {
    CHECK(src_ptr);
    CHECK(dst_ptr);
    src_ptr[i] = i;
    dst_ptr[i] = 0;
  }

  // Bad command queue
  status = clEnqueueSVMMemcpy(0, CL_TRUE, dst_ptr, src_ptr, 10 * sizeof(int), 0,
                              NULL, NULL);
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, status);

  // Invalid wait list
  status = clEnqueueSVMMemcpy(m_cq, CL_TRUE, dst_ptr, src_ptr, 10 * sizeof(int),
                              0, &event, NULL);
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST, status);
  status = clEnqueueSVMMemcpy(m_cq, CL_TRUE, dst_ptr, src_ptr, 10 * sizeof(int),
                              1, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST, status);

  // Invalid values
  status = clEnqueueSVMMemcpy(m_cq, CL_TRUE, NULL, src_ptr, 10 * sizeof(int), 0,
                              NULL, &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueSVMMemcpy(m_cq, CL_TRUE, dst_ptr, NULL, 10 * sizeof(int), 0,
                              NULL, &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status =
      clEnqueueSVMMemcpy(m_cq, CL_TRUE, dst_ptr, src_ptr, 0, 0, NULL, &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Overlap
  status = clEnqueueSVMMemcpy(m_cq, CL_TRUE, &(dst_ptr[0]), &(dst_ptr[1]),
                              10 * sizeof(int), 0, NULL, &event);
  CHECK_EQUAL(CL_MEM_COPY_OVERLAP, status);

  // Successful operation
  status = clEnqueueSVMMemcpy(m_cq, CL_TRUE, dst_ptr, src_ptr, 10 * sizeof(int),
                              0, NULL, &event);
  CHECK_EQUAL(CL_SUCCESS, status);

  clFinish(m_cq);

  for (i = 0; i < 10; ++i) {
    CHECK_EQUAL(src_ptr[i], dst_ptr[i]);
    CHECK_EQUAL(i, dst_ptr[i]);
  }

  clSVMFree(m_context, (void *)src_ptr);
  clSVMFree(m_context, (void *)dst_ptr);

  ACL_LOCKED(acl_print_debug_msg("end memcpy_svm\n"));
}

MT_TEST(acl_svm, memfill_svm) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  ACL_LOCKED(acl_print_debug_msg("begin memfill_svm\n"));
  this->yeah = true;
  int pattern = 5;
  int *dst_ptr = NULL;
  cl_int status;
  int i;
  cl_event event;

  ACL_LOCKED(CHECK(acl_context_is_valid(m_context)));

  dst_ptr =
      (int *)clSVMAlloc(m_context, CL_MEM_READ_WRITE, 10 * sizeof(int), 0);

  CHECK(dst_ptr != NULL);

  for (i = 0; i < 10; ++i) {
    CHECK(dst_ptr);
    dst_ptr[i] = 0;
  }

  // Bad command queue
  status = clEnqueueSVMMemFill(0, dst_ptr, &pattern, sizeof(int),
                               10 * sizeof(int), 0, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, status);

  // Invalid wait list
  cl_event event_wait_list[1];
  status = clEnqueueSVMMemFill(m_cq, dst_ptr, &pattern, sizeof(int),
                               10 * sizeof(int), 0, event_wait_list, NULL);
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST, status);
  status = clEnqueueSVMMemFill(m_cq, dst_ptr, &pattern, sizeof(int),
                               10 * sizeof(int), 1, NULL, NULL);
  CHECK_EQUAL(CL_INVALID_EVENT_WAIT_LIST, status);

  // Invalid values
  status = clEnqueueSVMMemFill(m_cq, NULL, &pattern, sizeof(int),
                               10 * sizeof(int), 0, NULL, &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueSVMMemFill(m_cq, dst_ptr, NULL, sizeof(int),
                               10 * sizeof(int), 0, NULL, &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueSVMMemFill(m_cq, dst_ptr, &pattern, 3, 10 * sizeof(int), 0,
                               NULL, &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueSVMMemFill(m_cq, dst_ptr, &pattern, sizeof(int), 0, 0, NULL,
                               &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = clEnqueueSVMMemFill(m_cq, dst_ptr, &pattern, sizeof(int), 7, 0, NULL,
                               &event);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  status = clEnqueueSVMMemFill(m_cq, dst_ptr, &pattern, sizeof(int),
                               10 * sizeof(int), 0, NULL, &event);
  CHECK_EQUAL(CL_SUCCESS, status);

  clFinish(m_cq);

  for (i = 0; i < 10; ++i) {
    CHECK_EQUAL(pattern, dst_ptr[i]);
  }

  clSVMFree(m_context, (void *)dst_ptr);

  ACL_LOCKED(acl_print_debug_msg("end memfill_svm\n"));
}
