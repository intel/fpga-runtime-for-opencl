// Copyright (C) 2010-2021 Intel Corporation
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
#include <acl_program.h>
#include <acl_support.h>
#include <acl_types.h>
#include <acl_util.h>

#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "acl_test.h"

MT_TEST_GROUP(Context) {
  enum { MAX_DEVICES = 100 };
  virtual void setup() {
    if (threadNum() == 0) {
      ACL_LOCKED(acl_test_setup_generic_system());
    }

    syncThreads();

    m_callback_count = 0;
    user_data_to_print = 0;
    m_callback_errinfo = 0;
    // Check retention counting.
    //
    // 1. unwrapped host mem.
    // 2. command queue
    // 3. command queue
    m_num_automatic = 3;

    this->load();
  }
  virtual void teardown() {
    m_callback_count = 0;
    user_data_to_print = 0;
    m_callback_errinfo = 0;

    syncThreads();

    if (threadNum() == 0) {
      ACL_LOCKED(acl_test_teardown_generic_system());
    }
    acl_test_run_standard_teardown_checks();
  }

  void load(void) {
    CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &m_platform, 0));
    ACL_LOCKED(CHECK(acl_platform_is_valid(m_platform)));
    CHECK_EQUAL(CL_SUCCESS,
                clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, MAX_DEVICES,
                               &m_device[0], &m_num_devices));
    CHECK(m_device[0]);
    CHECK(m_device[1]);
    CHECK(m_device[2]);
    m_num_devices = 3;

    CHECK(m_num_devices > 0);
  }

  void check_device_refcounts(void) {
    for (size_t i = 0; i < m_num_devices; i++) {
      CHECK_EQUAL(0, acl_ref_count(m_device[i]));
    }
  }

protected:
  cl_platform_id m_platform;
  cl_device_id m_device[MAX_DEVICES];
  cl_uint m_num_devices;

public:
  cl_ulong m_callback_count;
  int user_data_to_print;
  const char *m_callback_errinfo;
  unsigned m_num_automatic;
};

MT_TEST(Context, create_context) {
  cl_int status;
  cl_context valid_context;
  cl_context_properties empty_properties[] = {0};
  cl_context_properties valid_properties[] = {
      CL_CONTEXT_PLATFORM, (cl_context_properties)m_platform, 0};
  cl_context_properties invalid_properties0[] = {
      CL_CONTEXT_PLATFORM, (cl_context_properties)&status, 0};
  cl_context_properties invalid_properties1[] = {CL_CONTEXT_PLATFORM +
                                                     1, // invalid property name
                                                 (cl_context_properties)0, 0};
  cl_device_id bad_device = (cl_device_id)&status; // definitely invalid pointer

  // Bad cases

  // 0 num_devices
  status = CL_SUCCESS;
  (void)clCreateContext(valid_properties, 0, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // NULL devices ptr
  status = CL_SUCCESS;
  (void)clCreateContext(valid_properties, 1, 0, 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Bad device
  status = CL_SUCCESS;
  (void)clCreateContext(valid_properties, 1, &bad_device, 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_DEVICE, status);

  // Bad platform
  status = CL_SUCCESS;
  (void)clCreateContext(invalid_properties0, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_PLATFORM, status);

  // Bad platform name
  status = CL_SUCCESS;
  (void)clCreateContext(invalid_properties1, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Good case

  // Get a context.
  status = CL_INVALID_DEVICE;
  valid_context = clCreateContext(valid_properties, m_num_devices, &m_device[0],
                                  0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(valid_context)));
  ACL_LOCKED(CHECK(acl_is_valid_ptr(valid_context)));

  // Good case: Our devices are always occupied.
  // OpenCL 1.0 conformance needs this to succeed.
  status = CL_SUCCESS;
  {
    cl_context context2 = clCreateContext(valid_properties, m_num_devices,
                                          &m_device[0], 0, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_context_is_valid(context2)));

    // sync threads to ensure all tests are finished the clCreateContext()
    // section before releasing contexts below and checking if the ref count is
    // 0
    syncThreads();

    clReleaseContext(context2);
  }

  // Check retention counting.
  CHECK_EQUAL(1 + m_num_automatic, acl_ref_count(valid_context));
  CHECK_EQUAL(CL_SUCCESS, clRetainContext(valid_context));
  CHECK_EQUAL(2 + m_num_automatic, acl_ref_count(valid_context));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(valid_context));
  CHECK_EQUAL(1 + m_num_automatic, acl_ref_count(valid_context));
  CHECK_EQUAL(CL_SUCCESS, clRetainContext(valid_context));
  CHECK_EQUAL(CL_SUCCESS, clRetainContext(valid_context));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(valid_context));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(valid_context));
  CHECK_EQUAL(1 + m_num_automatic, acl_ref_count(valid_context));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(valid_context));

  syncThreads();
  CHECK_EQUAL(0, acl_get_num_alloc_cl_context());
  syncThreads();

  // Check retention and release on invalid context.
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  CHECK_EQUAL(CL_INVALID_CONTEXT, clRetainContext(&fake_context));
  CHECK_EQUAL(CL_INVALID_CONTEXT, clReleaseContext(&fake_context));
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();

  // Get a valid context by sending in NULL properties.
  status = CL_INVALID_DEVICE;
  valid_context =
      clCreateContext(0, m_num_devices, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(valid_context)));

  syncThreads();

  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(valid_context));

  syncThreads();

  // Get a valid context by sending in empty list of properties
  status = CL_INVALID_DEVICE;
  valid_context = clCreateContext(empty_properties, m_num_devices, &m_device[0],
                                  0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(valid_context)));

  syncThreads();

  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(valid_context));

  syncThreads();

  check_device_refcounts();
}

static void CL_CALLBACK
exception_notify_fn(CL_EXCEPTION_TYPE_INTEL exception_type,
                    const void *private_info, size_t cb, void *test_context) {
  CppUTestGroupContext *inst = (CppUTestGroupContext *)test_context;
  ScopedLock lock(inst->testMutex());

  if (inst) {
    inst->m_callback_count++;
  }

  cb = cb;                     // avoid warning on windows
  private_info = private_info; // avoid warning on windows

  // Printfs heavily rely on the input sent
  if (exception_type == CL_DEVICE_EXCEPTION_ECC_CORRECTABLE_INTEL)
    printf("Correctable error occured\n");
  if (exception_type == CL_DEVICE_EXCEPTION_ECC_NON_CORRECTABLE_INTEL)
    printf("Non-correctable error occured\n");
  if (inst)
    printf("User data (int): %i\n", inst->user_data_to_print);
  if (private_info)
    printf("Private info (int): %i\n", *(int *)private_info);
  if (private_info)
    printf("cb (size_t): %zu\n", cb);
}

static void CL_CALLBACK notify_me(const char *errinfo, const void *private_info,
                                  size_t cb, void *test_context) {
  CppUTestGroupContext *inst = (CppUTestGroupContext *)test_context;
  ScopedLock lock(inst->testMutex());

  if (inst) {
    inst->m_callback_count++;
    inst->m_callback_errinfo = errinfo;
  }
  cb = cb;                     // avoid warning on windows
  private_info = private_info; // avoid warning on windows
}

MT_TEST(Context, create_context_callback_set) {
  cl_context_properties valid_properties[] = {
      CL_CONTEXT_PLATFORM, (cl_context_properties)m_platform, 0};
  cl_int status = CL_SUCCESS;
  cl_context context = clCreateContext(valid_properties, m_num_devices,
                                       &m_device[0], notify_me, this, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  CHECK(notify_me == context->notify_fn);
  CHECK(this == context->notify_user_data);

  syncThreads();

  clReleaseContext(context);
}

MT_TEST(Context, create_context_callback_calling_badprop) {
  cl_context_properties invalid_properties[] = {CL_CONTEXT_PLATFORM,
                                                (cl_context_properties)1, 0};
  cl_int status = CL_SUCCESS;
  cl_context context =
      clCreateContext(invalid_properties, 0, 0, notify_me, this, &status);
  CHECK(CL_SUCCESS != status);
  ACL_LOCKED(CHECK(!acl_context_is_valid(context)));
  ScopedLock lock(testMutex());
  CHECK(this->m_callback_count > 0);
}

MT_TEST(Context, create_context_callback_calling_baddev) {
  cl_context_properties invalid_properties[] = {
      CL_CONTEXT_PLATFORM, (cl_context_properties)m_platform, 0};
  cl_int status = CL_SUCCESS;
  cl_context context =
      clCreateContext(invalid_properties, 0, 0, notify_me, this, &status);
  CHECK(CL_SUCCESS != status);
  ACL_LOCKED(CHECK(!acl_context_is_valid(context)));
  ScopedLock lock(testMutex());
  CHECK(this->m_callback_count > 0);
}

MT_TEST(Context, create_context_from_type_callback_set) {
  cl_context_properties valid_properties[] = {
      CL_CONTEXT_PLATFORM, (cl_context_properties)m_platform, 0};
  cl_int status = CL_SUCCESS;
  cl_context context = clCreateContextFromType(
      valid_properties, CL_DEVICE_TYPE_ALL, notify_me, this, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  CHECK(notify_me == context->notify_fn);
  CHECK(this == context->notify_user_data);

  syncThreads();

  clReleaseContext(context);
}

MT_TEST(Context, create_context_from_type_callback_calling_badprop) {
  cl_context_properties invalid_properties[] = {CL_CONTEXT_PLATFORM,
                                                (cl_context_properties)1, 0};
  cl_int status = CL_SUCCESS;
  cl_context context = clCreateContextFromType(
      invalid_properties, CL_DEVICE_TYPE_ALL, notify_me, this, &status);
  CHECK(CL_SUCCESS != status);
  ScopedLock lock(testMutex());
  CHECK(this->m_callback_count > 0);
  ACL_LOCKED(CHECK(!acl_context_is_valid(context)));
}

MT_TEST(Context, create_context_from_type) {
  cl_int status;
  cl_context invalid_context, valid_context;
  cl_context_properties empty_properties[] = {0};
  cl_context_properties valid_properties[] = {
      CL_CONTEXT_PLATFORM, (cl_context_properties)m_platform, 0};
  cl_context_properties invalid_properties0[] = {
      CL_CONTEXT_PLATFORM, (cl_context_properties)&status, 0};
  cl_context_properties invalid_properties1[] = {CL_CONTEXT_PLATFORM +
                                                     1, // invalid property name
                                                 (cl_context_properties)0, 0};

  // Bad type specs:
  status = CL_SUCCESS;
  invalid_context = clCreateContextFromType(valid_properties, 0, 0, 0, &status);
  CHECK_EQUAL(CL_DEVICE_NOT_FOUND, status);
  status = CL_SUCCESS;
  invalid_context = clCreateContextFromType(valid_properties,
                                            CL_DEVICE_TYPE_CPU, 0, 0, &status);
  CHECK_EQUAL(CL_DEVICE_NOT_FOUND, status);
  status = CL_SUCCESS;
  invalid_context = clCreateContextFromType(valid_properties,
                                            CL_DEVICE_TYPE_GPU, 0, 0, &status);
  CHECK_EQUAL(CL_DEVICE_NOT_FOUND, status);

  // Bad platform
  status = CL_SUCCESS;
  invalid_context = clCreateContextFromType(
      invalid_properties0, CL_DEVICE_TYPE_DEFAULT, 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_PLATFORM, status);

  // Bad platform name
  status = CL_SUCCESS;
  invalid_context = clCreateContextFromType(
      invalid_properties1, CL_DEVICE_TYPE_DEFAULT, 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Good case

  // Get a context.  Try all 3 valid type selectors
  status = CL_INVALID_DEVICE;
  valid_context = clCreateContextFromType(
      valid_properties, CL_DEVICE_TYPE_DEFAULT, 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  clReleaseContext(valid_context);
  status = CL_INVALID_DEVICE;
  valid_context = clCreateContextFromType(
      valid_properties, CL_DEVICE_TYPE_ACCELERATOR, 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  clReleaseContext(valid_context);
  status = CL_INVALID_DEVICE;
  valid_context = clCreateContextFromType(valid_properties, CL_DEVICE_TYPE_ALL,
                                          0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Good case: All devices are occupied.  But they still are considered
  // available by the conformance tests.
  status = CL_SUCCESS;
  invalid_context = clCreateContextFromType(
      valid_properties, CL_DEVICE_TYPE_DEFAULT, 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  syncThreads();

  clReleaseContext(invalid_context);

  ACL_LOCKED(CHECK(acl_context_is_valid(valid_context)));
  ACL_LOCKED(CHECK(acl_is_valid_ptr(valid_context)));

  // Check memory allocation size
  CHECK(0 < valid_context->max_mem_alloc_size);
  CHECK(valid_context->max_mem_alloc_size < 0xffffffff);

  CHECK_EQUAL(1 + m_num_automatic, acl_ref_count(valid_context));
  CHECK_EQUAL(CL_SUCCESS, clRetainContext(valid_context));
  CHECK_EQUAL(2 + m_num_automatic, acl_ref_count(valid_context));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(valid_context));
  CHECK_EQUAL(1 + m_num_automatic, acl_ref_count(valid_context));
  CHECK_EQUAL(CL_SUCCESS, clRetainContext(valid_context));
  CHECK_EQUAL(CL_SUCCESS, clRetainContext(valid_context));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(valid_context));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(valid_context));
  CHECK_EQUAL(1 + m_num_automatic, acl_ref_count(valid_context));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(valid_context));

  syncThreads();
  CHECK_EQUAL(0, acl_get_num_alloc_cl_context());
  syncThreads();

  // Check retention and release on invalid context.
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  CHECK_EQUAL(CL_INVALID_CONTEXT, clRetainContext(&fake_context));
  CHECK_EQUAL(CL_INVALID_CONTEXT, clReleaseContext(&fake_context));
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();

  // Get a valid context by sending in NULL properties.
  status = CL_INVALID_DEVICE;
  valid_context =
      clCreateContextFromType(0, CL_DEVICE_TYPE_DEFAULT, 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(valid_context)));

  syncThreads();

  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(valid_context));

  syncThreads();

  // Get a valid context by sending in empty properties.
  status = CL_INVALID_DEVICE;
  valid_context = clCreateContextFromType(
      empty_properties, CL_DEVICE_TYPE_DEFAULT, 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(valid_context)));

  syncThreads();

  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(valid_context));

  syncThreads();

  check_device_refcounts();
}

MT_TEST(Context, exhaust_memory) {
  cl_int status = CL_SUCCESS;
  cl_context context;

  syncThreads();
  if (threadNum() == 0) {
    acl_set_malloc_enable(0);
  }
  syncThreads();

  context = clCreateContext(0, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(0, context);
  CHECK_EQUAL(CL_OUT_OF_HOST_MEMORY, status);
  context = clCreateContextFromType(0, CL_DEVICE_TYPE_ALL, 0, 0, &status);
  CHECK_EQUAL(0, context);
  CHECK_EQUAL(CL_OUT_OF_HOST_MEMORY, status);

  syncThreads();
  if (threadNum() == 0) {
    acl_set_malloc_enable(1);
  }
  syncThreads();

  check_device_refcounts();
}

MT_TEST(Context, get_info) {
  char str[1024]; // must be big enough to hold biggest property
  const size_t str_size = sizeof(str) / sizeof(str[0]);
  cl_context_properties saved_props[10] = {-1, -1, -1, -1, -1, -1, -1};
  size_t size_ret;
  cl_context context;
  cl_context_info queries[] = {CL_CONTEXT_REFERENCE_COUNT, CL_CONTEXT_DEVICES,
                               CL_CONTEXT_PROPERTIES, CL_CONTEXT_PLATFORM,
                               CL_CONTEXT_NUM_DEVICES};
  cl_context_properties valid_properties[] = {
      CL_CONTEXT_PLATFORM, (cl_context_properties)m_platform, 0};

  cl_context_info unsupported_queries[] = {
      0, CL_QUEUE_CONTEXT, CL_DEVICE_DOUBLE_FP_CONFIG, CL_DEVICE_HALF_FP_CONFIG,
      CL_DEVICE_EXECUTION_CAPABILITIES};
  cl_int status = CL_INVALID_VALUE;

  // Bad contexts
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  CHECK_EQUAL(
      CL_INVALID_CONTEXT,
      clGetContextInfo(0, CL_CONTEXT_PROPERTIES, str_size, &str[0], &size_ret));
  CHECK_EQUAL(CL_INVALID_CONTEXT,
              clGetContextInfo(&fake_context, CL_CONTEXT_PROPERTIES, str_size,
                               &str[0], &size_ret));
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();

  // Get a context
  status = CL_INVALID_VALUE;
  context = clCreateContext(0, 1, &m_device[0], notify_me, this, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Good cases:
  // Ensure we can query all info enums, and in various combinations of
  // returning the values.
  for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); i++) {
    CHECK_EQUAL(CL_SUCCESS,
                clGetContextInfo(context, queries[i], 0, 0, &size_ret));
    CHECK(size_ret <
          str_size); // If this fails, just make str[] bigger for later tests.
    CHECK_EQUAL(CL_SUCCESS,
                clGetContextInfo(context, queries[i], str_size, &str[0], 0));
    size_ret = 0;
    CHECK_EQUAL(CL_SUCCESS, clGetContextInfo(context, queries[i], str_size,
                                             &str[0], &size_ret));
    if (queries[i] != CL_CONTEXT_PROPERTIES) {
      CHECK(size_ret > 0);
    }
  }

  // Bad query cases
  for (size_t i = 0;
       i < sizeof(unsupported_queries) / sizeof(unsupported_queries[0]); i++) {
    CHECK_EQUAL(
        CL_INVALID_VALUE,
        clGetContextInfo(context, unsupported_queries[i], 0, 0, &size_ret));
    size_ret = 0;
    CHECK_EQUAL(CL_INVALID_VALUE,
                clGetContextInfo(context, unsupported_queries[i], str_size,
                                 &str[0], &size_ret));
    CHECK_EQUAL(0, size_ret);
  }

  // Check retvalue args
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetContextInfo(context, queries[0], 0, &str[0], &size_ret));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetContextInfo(context, queries[0], 1, 0, &size_ret));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetContextInfo(context, queries[0], 1, &str[0],
                               &size_ret)); // too short to hold result.

  // That constraint is oddly missing from the API spec, even though other
  // query APIs have it.
  CHECK_EQUAL(CL_INVALID_VALUE, clGetContextInfo(context, queries[0], 0, 0, 0));

  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context));

  // Check properties
  // .. When passing in no properties array
  context = clCreateContext(0, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(status, CL_SUCCESS);

  syncThreads();

  CHECK(acl_ref_count(context));
  CHECK_EQUAL(CL_SUCCESS, clGetContextInfo(context, CL_CONTEXT_PROPERTIES,
                                           str_size, &str[0], &size_ret));
  CHECK_EQUAL(sizeof(cl_context_properties), size_ret); // size of terminating 0

  clReleaseContext(context);

  syncThreads();
  CHECK_EQUAL(0, acl_get_num_alloc_cl_context());
  syncThreads();

  // .. when passing in a filled-in properties array
  context = clCreateContext(valid_properties, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(status, CL_SUCCESS);

  syncThreads();

  CHECK(acl_ref_count(context));
  CHECK_EQUAL(CL_SUCCESS, clGetContextInfo(context, CL_CONTEXT_PROPERTIES,
                                           sizeof(saved_props), &saved_props[0],
                                           &size_ret));
  CHECK_EQUAL(3 * sizeof(cl_context_properties), size_ret);
  CHECK_EQUAL(valid_properties[0], saved_props[0]);
  CHECK_EQUAL(valid_properties[1], saved_props[1]);

  status = clReleaseContext(context);
  CHECK_EQUAL(CL_SUCCESS, status);

  syncThreads();
  CHECK_EQUAL(0, acl_get_num_alloc_cl_context());
  syncThreads();

  check_device_refcounts();
}

MT_TEST(Context, idle_update) {
  // Nothing is going on, so idle update should just return
  cl_int status = CL_INVALID_CONTEXT;
  cl_context context = clCreateContext(0, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  ACL_LOCKED(acl_idle_update(context));
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));

  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context));

  syncThreads();

  check_device_refcounts();
}

MT_TEST(Context, device_exception_processing) {
  cl_context_properties valid_properties[] = {
      CL_CONTEXT_PLATFORM, (cl_context_properties)m_platform, 0};
  cl_int status = CL_SUCCESS;
  int user_data1 = 1;
  int user_data2 = 2;

  int private_info1 = 3;
  int private_info2 = 4;
  cl_context context = clCreateContext(valid_properties, m_num_devices,
                                       &m_device[0], NULL, NULL, &status);

  CHECK_EQUAL(CL_SUCCESS, status);

  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  ACL_LOCKED(acl_idle_update(context));
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));

  // Set callbacks with different user_data for 2 devices
  this->user_data_to_print = user_data1;
  clSetDeviceExceptionCallbackIntelFPGA(
      1, &m_device[0],
      CL_DEVICE_EXCEPTION_ECC_NON_CORRECTABLE_INTEL |
          CL_DEVICE_EXCEPTION_ECC_CORRECTABLE_INTEL,
      &exception_notify_fn, this);
  this->user_data_to_print = user_data2;
  clSetDeviceExceptionCallbackIntelFPGA(
      1, &m_device[1], CL_DEVICE_EXCEPTION_ECC_NON_CORRECTABLE_INTEL,
      &exception_notify_fn, this);

  CHECK(m_platform->device_exception_platform_counter == 0);

  // Send non-correctable signal to the first device (0)
  acl_receive_device_exception(0, CL_DEVICE_EXCEPTION_ECC_NON_CORRECTABLE_INTEL,
                               &private_info2, sizeof(private_info2));

  CHECK(m_platform->device_exception_platform_counter == 1);
  CHECK(m_device[0]->device_exception_status ==
        CL_DEVICE_EXCEPTION_ECC_NON_CORRECTABLE_INTEL);
  CHECK(m_device[0]->exception_cb[1] == sizeof(private_info2));
  CHECK(*(int *)m_device[0]->exception_private_info[1] == private_info2);

  // Send correctable signal to the first device (0)
  acl_receive_device_exception(0, CL_DEVICE_EXCEPTION_ECC_CORRECTABLE_INTEL,
                               NULL, 0);

  CHECK(m_platform->device_exception_platform_counter == 1);
  CHECK(m_device[0]->device_exception_status ==
        (CL_DEVICE_EXCEPTION_ECC_NON_CORRECTABLE_INTEL |
         CL_DEVICE_EXCEPTION_ECC_CORRECTABLE_INTEL));
  CHECK(m_device[0]->exception_cb[0] == 0);
  CHECK(m_device[0]->exception_private_info[0] == NULL);

  // Send non-correctable signal to the first device (0) to overwrite
  // private_info
  acl_receive_device_exception(0, CL_DEVICE_EXCEPTION_ECC_NON_CORRECTABLE_INTEL,
                               &private_info1, sizeof(private_info1));

  CHECK(m_platform->device_exception_platform_counter == 1);
  CHECK(m_device[0]->device_exception_status ==
        (CL_DEVICE_EXCEPTION_ECC_NON_CORRECTABLE_INTEL |
         CL_DEVICE_EXCEPTION_ECC_CORRECTABLE_INTEL));
  CHECK(m_device[0]->exception_cb[1] == sizeof(private_info1));
  CHECK(*(int *)m_device[0]->exception_private_info[1] == private_info1);

  // Send correctable signal to the second device (1)
  acl_receive_device_exception(1, CL_DEVICE_EXCEPTION_ECC_CORRECTABLE_INTEL,
                               &private_info1, sizeof(private_info1));

  // Correctable signal for this device should be supressed by the listen_mask
  CHECK(m_platform->device_exception_platform_counter == 1);
  CHECK(m_device[1]->device_exception_status == 0);
  CHECK(m_device[1]->exception_cb[0] == 0);
  CHECK(m_device[1]->exception_private_info[0] == NULL);

  ACL_LOCKED(acl_idle_update(context));

  // acl_idle_update should process all the exceptions
  CHECK(m_platform->device_exception_platform_counter == 0);
  CHECK(m_device[0]->device_exception_status == 0);
  CHECK(m_device[1]->device_exception_status == 0);
  CHECK(m_device[0]->exception_cb[1] == 0);
  CHECK(m_device[0]->exception_private_info[1] == NULL);

  syncThreads();

  clReleaseContext(context);
}

MT_TEST(Context, context_uses_device) {
  for (cl_uint i = 1; i <= m_num_devices; i++) {
    cl_int status = CL_INVALID_CONTEXT;
    cl_context context;

    // Check when using initial segment of devices
    context = clCreateContext(0, i, &m_device[0], 0, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_context_is_valid(context)));

    ACL_LOCKED(CHECK(acl_command_queue_is_valid(context->auto_queue)));

    // Note: Every context has a DMA command queue, and it's always
    // attached to the first device passed into clCreateContext
    for (cl_uint j = 0; j < i; j++) {
      ACL_LOCKED(CHECK(acl_context_uses_device(context, m_device[j])));
    }
    for (cl_uint j = i; j < m_num_devices; j++) {
      ACL_LOCKED(CHECK(!acl_context_uses_device(context, m_device[j])));
    }

    CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context));

    // Check when using trailing segment of devices
    context =
        clCreateContext(0, i, &m_device[m_num_devices - i], 0, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_context_is_valid(context)));

    for (cl_uint j = 0; j < m_num_devices - i; j++) {
      ACL_LOCKED(CHECK(!acl_context_uses_device(context, m_device[j])));
    }
    for (cl_uint j = m_num_devices - i; j < m_num_devices; j++) {
      // Note: Every context has a DMA command queue, and it's always
      // attached to the first device passed into clCreateContext, which
      // in this case is m_device[ m_num_devices - i ]
      ACL_LOCKED(CHECK(acl_context_uses_device(context, m_device[j])));
    }

    CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context));
  }

  syncThreads();

  check_device_refcounts();
}

MT_TEST(Context, create_context_multi) {
  cl_int status;
  cl_context context0, context1;

  // Get a context.
  status = CL_INVALID_DEVICE;
  context0 = clCreateContext(0, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context0)));
  ACL_LOCKED(CHECK(acl_is_valid_ptr(context0)));

  ACL_LOCKED(acl_print_debug_msg(" newly created context0 <%lu>\n",
                                 acl_ref_count(context0)));

  status = CL_INVALID_DEVICE;
  context1 = clCreateContext(0, 1, &m_device[1], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context1)));
  ACL_LOCKED(CHECK(acl_is_valid_ptr(context1)));

  CHECK(context0 != context1);

  // Memory allocated from two different contexts should not overlap!
  cl_mem mem0 = clCreateBuffer(context0, CL_MEM_READ_WRITE, 8, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  cl_mem mem1 = clCreateBuffer(context1, CL_MEM_READ_WRITE, 8, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(mem0 != mem1);
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem0));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem1));

  syncThreads();

  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context0));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context1));

  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  CHECK_EQUAL(CL_INVALID_CONTEXT, clReleaseContext(&fake_context));
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();
}

MT_TEST(Context, create_context_mode_lock) {
  cl_context_properties prop_builtin[] = {
      CL_CONTEXT_COMPILER_MODE_INTELFPGA,
      CL_CONTEXT_COMPILER_MODE_PRELOADED_BINARY_ONLY_INTELFPGA, 0};

  cl_context context;
  cl_int status = CL_SUCCESS;

  // Context creation one after the other using the same device.
  // Mode builtin kernels.
  context = clCreateContext(prop_builtin, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  clReleaseContext(context);
  // Mode binary.
  context = clCreateContext(0, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  clReleaseContext(context);

  cl_context context2;

  // Context creation with different modes on same device.
  context = clCreateContext(prop_builtin, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  context2 = clCreateContext(0, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  clReleaseContext(context);

  context = clCreateContext(0, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  context2 = clCreateContext(prop_builtin, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  clReleaseContext(context);

  // Context creation with different modes on different devices.
  context = clCreateContext(prop_builtin, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  context2 = clCreateContext(0, 1, &m_device[1], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context2)));
  clReleaseContext(context2);

  // Create context using multiple devices where one of the devices already has
  // a mode lock (first device locked on mode builtin).
  context2 = clCreateContext(prop_builtin, 2, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context2)));
  clReleaseContext(context2);
  context2 = clCreateContext(0, 2, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  clReleaseContext(context);

  // Repeat above using the other mode lock.
  // Context creation with different modes on different devices.
  context = clCreateContext(0, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  context2 = clCreateContext(prop_builtin, 1, &m_device[1], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context2)));
  clReleaseContext(context2);

  // Create context using multiple devices where one of the devices already has
  // a mode lock (first device locked on mode binary)
  context2 = clCreateContext(0, 2, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context2)));
  clReleaseContext(context2);
  context2 = clCreateContext(prop_builtin, 2, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  clReleaseContext(context);

  // Create context when all devices are not opened yet
  context = clCreateContextFromType(0, CL_DEVICE_TYPE_ALL, 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  clReleaseContext(context);

  context =
      clCreateContextFromType(prop_builtin, CL_DEVICE_TYPE_ALL, 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  clReleaseContext(context);

  // Create context on mode builtin when a device is locked on mode binary and
  // vice versa
  context = clCreateContext(0, 1, &m_device[1], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  context2 =
      clCreateContextFromType(prop_builtin, CL_DEVICE_TYPE_ALL, 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context2)));
  clReleaseContext(context);
  clReleaseContext(context2);

  context = clCreateContext(prop_builtin, 1, &m_device[1], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  context2 = clCreateContextFromType(0, CL_DEVICE_TYPE_ALL, 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context2)));
  clReleaseContext(context);
  clReleaseContext(context2);

  // Create context on mode builtin when ALL devices are locked on mode binary
  // and vice versa
  context = clCreateContextFromType(0, CL_DEVICE_TYPE_ALL, 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  context2 = clCreateContext(prop_builtin, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  context2 =
      clCreateContextFromType(prop_builtin, CL_DEVICE_TYPE_ALL, 0, 0, &status);
  CHECK_EQUAL(CL_DEVICE_NOT_AVAILABLE, status);
  clReleaseContext(context);

  context =
      clCreateContextFromType(prop_builtin, CL_DEVICE_TYPE_ALL, 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));
  context2 = clCreateContext(0, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  context2 = clCreateContextFromType(0, CL_DEVICE_TYPE_ALL, 0, 0, &status);
  CHECK_EQUAL(CL_DEVICE_NOT_AVAILABLE, status);
  clReleaseContext(context);
}

MT_TEST(Context, compiler_mode) {
  cl_context_properties props[5]; // room enough to store two properties, their
                                  // values, and terminating NULL
  const char *env = "CL_CONTEXT_COMPILER_MODE_INTELFPGA";

  typedef struct {
    const char *str;
    bool used;
    bool valid;
  } envdef_t;
  envdef_t envvals[] = {
      {0, false, true},    {"", false, false},   {"1x", false, false},
      {"x", false, false}, {"x1", false, false}, {"-1", false, false},
      {"0", true, true},   {"1", true, true},    {"2", true, true},
      {"3", true, true},   {"4", true, true},    {"5", true, true},
      {"6", true, true},   {"7", true, false}};

  for (unsigned ienv = 0; ienv < sizeof(envvals) / sizeof(envvals[0]); ienv++) {
    // don't set env vars until all threads are ready to use it
    syncThreads();

    if (threadNum() == 0) {
      acl_test_unsetenv(env);
    }
    if (envvals[ienv].str) {
      if (threadNum() == 0) {
        acl_test_setenv(env, envvals[ienv].str);
      }
#ifdef _WIN32
      // when setting with empty string, that deletes the env var in windows
      if (strlen(envvals[ienv].str) == 0) {
        envvals[ienv].valid = true;
      }
#endif
    }

    // don't allow any thread to start tests until env var is set
    syncThreads();

    for (int apitype = 0; apitype < 2; apitype++) {
      for (int i = 0; i < 2; i++) { // without/with platform.
        for (int j = -1; j <= CL_CONTEXT_COMPILER_MODE_NUM_MODES_INTELFPGA;
             j++) {
          int idx = 0;
          if (i) {
            props[idx++] = CL_CONTEXT_PLATFORM;
            props[idx++] = (cl_context_properties)m_platform;
            ACL_LOCKED(acl_print_debug_msg(" set platform  %p\n", m_platform));
          }
          if (j >= 0) {
            props[idx++] = CL_CONTEXT_COMPILER_MODE_INTELFPGA;
            props[idx++] = (cl_context_properties)j;
          }
          props[idx++] = 0;

          ACL_LOCKED(
              acl_print_debug_msg(" create with env str '%s' i %d j %d\n",
                                  envvals[ienv].str, i, j));
          cl_int status = CL_INVALID_VALUE;
          cl_context context =
              apitype ? clCreateContextFromType(props, CL_DEVICE_TYPE_ALL, 0, 0,
                                                &status)
                      : clCreateContext(props, 1, m_device, notify_me, this,
                                        &status);
          if (j >= CL_CONTEXT_COMPILER_MODE_NUM_MODES_INTELFPGA ||
              !envvals[ienv].valid) {
            CHECK_EQUAL(CL_INVALID_VALUE, status);
            continue;
          }
          CHECK_EQUAL(CL_SUCCESS, status);
          ACL_LOCKED(CHECK(acl_context_is_valid(context)));

          cl_context_properties props_ret[20];
          size_t size_ret = 0;
          CHECK_EQUAL(CL_SUCCESS, clGetContextInfo(
                                      context, CL_CONTEXT_PROPERTIES,
                                      sizeof(props_ret), props_ret, &size_ret));
          CHECK_EQUAL(sizeof(cl_context_properties) * idx, size_ret);

          int idx_ret = 0;
          if (i) {
            CHECK_EQUAL(CL_CONTEXT_PLATFORM, props_ret[idx_ret++]);
            CHECK_EQUAL((cl_context_properties)m_platform,
                        props_ret[idx_ret++]);
          }
          if (j >= 0) {
            // if explicitly set, then this overrides env var.
            CHECK_EQUAL(CL_CONTEXT_COMPILER_MODE_INTELFPGA,
                        props_ret[idx_ret++]);
            CHECK_EQUAL((cl_context_properties)j, props_ret[idx_ret++]);
            CHECK_EQUAL((cl_context_properties)j,
                        (cl_context_properties)context->compiler_mode);
          } else if (envvals[ienv].used) {
            // We use the integer value of the environment variable.
            CHECK_EQUAL((cl_context_properties)atoi(envvals[ienv].str),
                        (cl_context_properties)context->compiler_mode);
          }
          CHECK_EQUAL(0, props_ret[idx_ret++]);
          CHECK_EQUAL(idx, idx_ret);

          syncThreads();
          clReleaseContext(context);
          syncThreads();
          CHECK_EQUAL(0, acl_get_num_alloc_cl_context());
          syncThreads();
        }
      }
    }
  }

  syncThreads();
  if (threadNum() == 0) {
    acl_test_unsetenv(env);
  }
}

MT_TEST(Context, create_overlapping_contexts) {
  cl_int status;
  cl_context_properties valid_properties[] = {
      CL_CONTEXT_PLATFORM, (cl_context_properties)m_platform, 0};

  // device #0 should be on dev op queue 0
  status = CL_SUCCESS;
  cl_context context0 = clCreateContext(valid_properties, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // devices #1 and #2 should be on dev op queue 1
  cl_context context1 = clCreateContext(valid_properties, 2, &m_device[1], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  syncThreads();
  CHECK       (acl_platform.physical_dev_id_to_doq_idx[0] != acl_platform.physical_dev_id_to_doq_idx[1]);
  CHECK_EQUAL (acl_platform.physical_dev_id_to_doq_idx[1], acl_platform.physical_dev_id_to_doq_idx[2]);

  // now devices #0, #1, and #2 should all be on dev op queue 0
  cl_context context2 = clCreateContext(valid_properties, 3, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  syncThreads();
  CHECK_EQUAL (acl_platform.physical_dev_id_to_doq_idx[0], acl_platform.physical_dev_id_to_doq_idx[1]);
  CHECK_EQUAL (acl_platform.physical_dev_id_to_doq_idx[0], acl_platform.physical_dev_id_to_doq_idx[2]);


  clReleaseContext(context0);
  clReleaseContext(context1);
  clReleaseContext(context2);
}

MT_TEST(Context, offline_device) {
  cl_context_properties props[5]; // room enough to store two properties, their
                                  // values, and terminating NULL
  const char *env = "CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA";

  typedef struct {
    const char *str;
    bool used;
    bool valid;
  } envdef_t;
  envdef_t envvals[] = {
      {0, false, true},
      // Use two different devices to test priority between env var and
      // explicit context property.
      // But since we use device type ALL, these need to have consistent
      // global mem configs.
      {ACLTEST_DEFAULT_BOARD, true, true},
      {"a10_ref_small", true, true}};

  for (unsigned ienv = 0; ienv < sizeof(envvals) / sizeof(envvals[0]); ienv++) {
    syncThreads();

    if (threadNum() == 0) {
      acl_test_unsetenv(env);
    }
    if (envvals[ienv].str) {
      if (threadNum() == 0) {
        char setting[100];
        sprintf(setting, "+%s", envvals[ienv].str);
        acl_test_setenv(env, setting);
      }
#ifdef _WIN32
      if (envvals[ienv].str && strlen(envvals[ienv].str) == 0) {
        // when setting with empty string, that deletes the env var in
        // windows.
        envvals[ienv].valid = true;
      }
#endif
    }

    if (threadNum() == 0) {
      // need to pick up env var again.
      acl_test_teardown_generic_system();
      acl_test_setup_generic_system();
    }

    syncThreads();

    for (int apitype = 0; apitype < 1; apitype++) { // just do test by type.
      for (int i = 0; i < 2; i++) {                 // without/with platform.
        int idx = 0;
        cl_context_properties fixed_val = 0;
        if (i) {
          props[idx++] = CL_CONTEXT_PLATFORM;
          props[idx++] = (cl_context_properties)m_platform;
          ACL_LOCKED(acl_print_debug_msg(" set platform  %p\n", m_platform));
        }
        props[idx++] = 0;

        ACL_LOCKED(acl_print_debug_msg(
            " create %s with env str '%s' ienv %d \n",
            (apitype ? "fromType" : "fromList"),
            (envvals[ienv].str ? envvals[ienv].str : "<nil>"), ienv));
        cl_int status = CL_INVALID_VALUE;
        cl_context context =
            apitype
                ? clCreateContextFromType(props, CL_DEVICE_TYPE_ALL, notify_me,
                                          this, &status)
                : clCreateContext(props, 1, m_device, notify_me, this, &status);
        ACL_LOCKED(acl_print_debug_msg(" fixed_val %d  valid %d\n",
                                       (int)fixed_val,
                                       (int)envvals[ienv].valid));
        if (fixed_val != 0) {
          if (!envvals[ienv].valid) {
            CHECK_EQUAL(CL_INVALID_VALUE, status);
            continue;
          }
          if (envvals[ienv].str != 0) {
            ACL_LOCKED(acl_print_debug_msg(" %s vs. %s \n", (char *)fixed_val,
                                           envvals[ienv].str));
            if (0 != strcmp((char *)fixed_val, envvals[ienv].str)) {
              CHECK_EQUAL(CL_DEVICE_NOT_AVAILABLE, status);
              continue;
            }
          }
        }

        CHECK_EQUAL(CL_SUCCESS, status);
        ACL_LOCKED(acl_print_debug_msg(" Got context %p\n", context));

        cl_context_properties props_ret[20];
        size_t size_ret = 0;
        CHECK_EQUAL(CL_SUCCESS,
                    clGetContextInfo(context, CL_CONTEXT_PROPERTIES,
                                     sizeof(props_ret), props_ret, &size_ret));
        CHECK_EQUAL(sizeof(cl_context_properties) * idx, size_ret);

        int idx_ret = 0;
        if (i) {
          CHECK_EQUAL(CL_CONTEXT_PLATFORM, props_ret[idx_ret++]);
          CHECK_EQUAL((cl_context_properties)m_platform, props_ret[idx_ret++]);
        }

        CHECK_EQUAL(0, props_ret[idx_ret++]);
        CHECK_EQUAL(idx, idx_ret);

        syncThreads();
        ACL_LOCKED(acl_print_debug_msg(" Releaseing context\n"));
        clReleaseContext(context);
        syncThreads();
        CHECK_EQUAL(0, acl_get_num_alloc_cl_context());
        syncThreads();
      }
    }
  }

  syncThreads();
  if (threadNum() == 0) {
    acl_test_unsetenv(env);
  }
}

TEST_GROUP(compiler_mode){
    void setup(){acl_test_setup_sample_default_board_system();
}
void teardown() {
  acl_test_teardown_sample_default_board_system();
  acl_test_run_standard_teardown_checks();
}
}
;

TEST(compiler_mode, compiler_mode_preloaded_binary) {
  // Preloaded binary is what most people will use.
  // It was the default prior to 13.0
  cl_context context = 0;
  cl_int status = 42;

  // Set things up so we believe that the device is for the default board
  // and has a device binary on it.
  cl_platform_id platform = 0;
  CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &platform, 0));
  cl_device_id device = 0;
  CHECK_EQUAL(CL_SUCCESS,
              clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 1, &device, 0));

  context = clCreateContext(acl_test_context_prop_preloaded_binary_only(), 1,
                            &device, 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(context);
  CHECK_EQUAL(CL_CONTEXT_COMPILER_MODE_PRELOADED_BINARY_ONLY_INTELFPGA,
              context->compiler_mode);

  ////////////
  // Check derived behaviours on a standard context.
  ////////////

  // 1. Does not pretend to have an online compiler.
  CHECK(!context->compiles_programs);

  // 2. Does not swap SOFs onto the device during host execution.
  CHECK(!context->programs_devices);

  // 3. Does not have backing store for all device-side buffers, and "map" does
  // not work for them.
  CHECK(!context->saves_and_restores_buffers_for_reprogramming);

  // 4. Set of kernel interfaces will not change during host execution.
  // Look for those interfaces in the acl_platform.
  CHECK(!context->uses_dynamic_sysdef);

  // 5. Does not use a program library to store or load program info based
  // on program source and build options.
  CHECK(!context->uses_program_library);

  // Can load a valid program
  cl_program program = 0;
  size_t bin_len = 0;
  const unsigned char *example_bin = acl_test_get_example_binary(&bin_len);
  cl_int bin_status = 42;
  program = clCreateProgramWithBinary(context, 1, &device, &bin_len,
                                      &example_bin, &bin_status, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(program);
  CHECK_EQUAL(CL_SUCCESS, bin_status);
  clReleaseProgram(program);

  // Can load to load an invalid binary.
  program = 0;
  std::string bad_bin("bad binary");
  size_t bad_bin_len = bad_bin.size();
  bin_status = 42;
  const unsigned char *bad_bin_c =
      reinterpret_cast<const unsigned char *>(bad_bin.c_str());
  program = clCreateProgramWithBinary(context, 1, &device, &bad_bin_len,
                                      &bad_bin_c, &bin_status, &status);
  CHECK_EQUAL(CL_SUCCESS, bin_status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(program);
  clReleaseProgram(program);

  // It's debatable, but for porting an application, we allow
  // clBuildProgram to succeed, but won't actually do anything to change
  // the cl_program or the device.
  // This is part of our extension spec, and yes it's not conformant.
  program = 0;
  std::string source("kernel void foo(global int*A) { *A = 42; }");
  size_t source_len = source.size();
  status = 42;
  const char *source_c = source.c_str();
  program =
      clCreateProgramWithSource(context, 1, &source_c, &source_len, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  status = clBuildProgram(program, 1, &device, "", 0, 0);
  CHECK_EQUAL(CL_SUCCESS, status);
  clReleaseProgram(program);

  clReleaseContext(context);
}

TEST(compiler_mode, compiler_mode_default) {
  cl_context context = 0;
  cl_int status = 42;

  // Set things up so we believe that the device is for the default board
  // and has a device binary on it.
  cl_platform_id platform = 0;
  CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &platform, 0));
  cl_device_id device = 0;
  CHECK_EQUAL(CL_SUCCESS,
              clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 1, &device, 0));

  context = clCreateContext(0 /* default ! */, 1, &device, 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(context);
  CHECK_EQUAL(CL_CONTEXT_COMPILER_MODE_OFFLINE_INTELFPGA,
              context->compiler_mode);

  ////////////
  // Check derived behaviours on a standard context.
  ////////////

  // 1. Does not pretend to have an online compiler.
  CHECK(!context->compiles_programs);

  // 2. Does swap SOFs onto the device during host execution.
  CHECK(context->programs_devices);

  // 3. Have backing store for all device-side buffers, and "map" works for
  // them.
  CHECK(context->saves_and_restores_buffers_for_reprogramming);

  // 4. Set of kernel interfaces changes during host execution.
  // Look for those interfaces in the dev_prog associated with a cl_program.
  CHECK(context->uses_dynamic_sysdef);

  // 5. Does not use a program library to store or load program info based
  // on program source and build options.
  CHECK(!context->uses_program_library);

  // Can load a valid program
  cl_program program = 0;
  size_t bin_len = 0;
  const unsigned char *example_bin = acl_test_get_example_binary(&bin_len);
  cl_int bin_status = 42;
  program = clCreateProgramWithBinary(context, 1, &device, &bin_len,
                                      &example_bin, &bin_status, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(program);
  CHECK_EQUAL(CL_SUCCESS, bin_status);
  status = clReleaseProgram(program);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Should fail to load an invalid binary.
  program = 0;
  std::string bad_bin("bad binary");
  size_t bad_bin_len = bad_bin.size();
  bin_status = 42;
  const unsigned char *bad_bin_c =
      reinterpret_cast<const unsigned char *>(bad_bin.c_str());
  program = clCreateProgramWithBinary(context, 1, &device, &bad_bin_len,
                                      &bad_bin_c, &bin_status, &status);
  CHECK_EQUAL(CL_INVALID_BINARY, bin_status);
  CHECK_EQUAL(CL_INVALID_BINARY, status);
  CHECK_EQUAL(0, program);

  // Should fail to compile a program, because we don't have an online
  // compiler.
  program = 0;
  std::string source("kernel void foo(global int*A) { *A = 42; }");
  size_t source_len = source.size();
  status = 42;
  const char *source_c = source.c_str();
  program =
      clCreateProgramWithSource(context, 1, &source_c, &source_len, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  status = clBuildProgram(program, 1, &device, "", 0, 0);
  CHECK_EQUAL(CL_COMPILER_NOT_AVAILABLE, status);
  status = clReleaseProgram(program);
  CHECK_EQUAL(CL_SUCCESS, status);

  clReleaseContext(context);
}

TEST_GROUP(Offline) {
  enum { MAX_DEVICES = 100 };
  virtual void setup() {
    m_env = "CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA";
    acl_test_setenv(m_env, "+" ACLTEST_DEFAULT_BOARD);
    acl_test_setup_generic_system();
    CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &m_platform, 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, MAX_DEVICES,
                               &(m_device[0]), &m_num_devices));
    CHECK(m_num_devices >= 2);
  }
  virtual void teardown() {
    acl_test_unsetenv(m_env);
    acl_test_teardown_generic_system();
    acl_test_run_standard_teardown_checks();
  }

protected:
  const char *m_env;
  cl_device_id m_device[MAX_DEVICES];
  cl_platform_id m_platform;
  cl_uint m_num_devices;
};

TEST(Offline, no_mix) {
  // should not be able to make a context with both (online and offline):
  // No consistent way to place device memory!
  cl_int status = CL_SUCCESS;
  cl_context context =
      clCreateContext(0, 2, &(m_device[m_num_devices - 2]), 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_DEVICE, status);
  CHECK_EQUAL(0, context);
}

TEST(Offline, emulate_device_mem) {
  // When creating a context an offline device, there is no memory to
  // access.  So "device global memory" should really be on the host.
  cl_int status = CL_INVALID_DEVICE;
  // change the device we use because now the offline device is at the end:
  cl_context context =
      clCreateContext(0, 1, &m_device[m_num_devices - 1], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));

  CHECK_EQUAL(0, m_device[m_num_devices - 1]->present);

  // Check that emulated memory has been set up.
  CHECK_EQUAL(0, context->emulated_global_mem.is_user_provided);
  CHECK_EQUAL(1, context->emulated_global_mem.is_host_accessible);
  CHECK_EQUAL(1, context->emulated_global_mem.is_device_accessible);
  CHECK_EQUAL(1, context->emulated_global_mem.uses_host_system_malloc);
  // We now put host allocations on the allocation chain.
  // So the head in the chain better be non-ACL_OPEN.
  acl_block_allocation_t *first_auto_mem_block =
      context->emulated_global_mem.first_block;
  CHECK(NULL == context->emulated_global_mem
                    .first_block); // no allocation when context is created
  CHECK_EQUAL(context->global_mem, &(context->emulated_global_mem));

  status = CL_INVALID_DEVICE;
  cl_mem mem = clCreateBuffer(context, CL_MEM_READ_ONLY, 1024, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(mem)));
  CHECK(NULL !=
        context->emulated_global_mem.first_block); // first block points to mem

  // Should be on the host.
  CHECK_EQUAL(0, mem->block_allocation->region->is_user_provided);
  CHECK_EQUAL(1, mem->block_allocation->region->is_host_accessible);
  CHECK_EQUAL(1, mem->block_allocation->region->is_device_accessible);
  CHECK_EQUAL(1, mem->block_allocation->region->uses_host_system_malloc);

  // Make sure the memory area is valid.
  CHECK(mem->host_mem.raw);
  CHECK(mem->host_mem.aligned_ptr);
  CHECK_EQUAL(mem->host_mem.aligned_ptr, mem->block_allocation->range.begin);

  // Host allocations do go in a linked list.
  CHECK_EQUAL(mem, context->global_mem->first_block->mem_obj);

  clReleaseMemObject(mem);

  CHECK_EQUAL(first_auto_mem_block, context->global_mem->first_block);

  clReleaseContext(context);
}
