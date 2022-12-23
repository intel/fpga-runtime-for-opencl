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
#include <acl_globals.h>
#include <acl_kernel.h>
#include <acl_platform.h>
#include <acl_support.h>
#include <acl_types.h>
#include <acl_util.h>

#include "acl_test.h"

MT_TEST_GROUP(acl_command_queue) {
  enum { MAX_DEVICES = 100 };
  void setup() {
    if (threadNum() == 0) {
      ACL_LOCKED(acl_test_setup_generic_system());
    }

    syncThreads();

    m_callback_count = 0;
    m_callback_errinfo = 0;
    this->load();
  }
  void teardown() {
    syncThreads();

    if (threadNum() == 0) {
      acl_test_teardown_generic_system();
    }

    acl_test_run_standard_teardown_checks();
  }

  void load(void) {
    CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &m_platform, 0));
    ACL_LOCKED(CHECK(acl_platform_is_valid(m_platform)));
    CHECK_EQUAL(CL_SUCCESS,
                clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, MAX_DEVICES,
                               &m_device[0], &m_num_devices));
    int offline_only = 0;
    CHECK_EQUAL(0, acl_get_offline_device_user_setting(&offline_only));
    CHECK_EQUAL(0, offline_only);
    CHECK(m_num_devices > 0);
  }

protected:
  cl_platform_id m_platform;
  cl_device_id m_device[MAX_DEVICES];
  cl_uint m_num_devices;

public:
  cl_ulong m_callback_count;
  const void *m_callback_errinfo;
};

static void CL_CALLBACK notify_me(const char *errinfo, const void *private_info,
                                  size_t cb, void *user_data) {
  CppUTestGroupacl_command_queue *inst =
      (CppUTestGroupacl_command_queue *)user_data;
  if (inst) {
    inst->m_callback_count++;
    inst->m_callback_errinfo = errinfo;
  }
  cb = cb;                     // avoid warning on windows
  private_info = private_info; // avoid warning on windows
}

MT_TEST(acl_command_queue, create) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  cl_int status;
  cl_command_queue cq;
  // Use a non-predefined device.
  // For example, the predefined DMA device always has an associated
  // command queue in every live context.
  cl_device_id device = m_device[0];
  cl_command_queue_properties valid_props[] = {
      0, CL_QUEUE_PROFILING_ENABLE, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE,
      CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_PROFILING_ENABLE};
  cl_command_queue_properties invalid_props =
      (CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_PROFILING_ENABLE)
      << 1 // all bits, shifted up
      ;
  cl_command_queue_properties unsupported_props[] = {
      CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_ON_DEVICE,
      CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_ON_DEVICE |
          CL_QUEUE_ON_DEVICE_DEFAULT,
      CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_ON_DEVICE |
          CL_QUEUE_ON_DEVICE_DEFAULT | CL_QUEUE_PROFILING_ENABLE};

  // Bad context
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  status = CL_SUCCESS;
  cq = clCreateCommandQueue(0, device, valid_props[0], &status);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);
  CHECK_EQUAL(0, cq);
  status = CL_SUCCESS;
  cq = clCreateCommandQueue(&fake_context, device, valid_props[0], &status);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);
  CHECK_EQUAL(0, cq);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();

  {
    CHECK(m_num_devices >= 2);
    // Context is valid, but for a different device
    cl_context context1 = clCreateContext(0, 1, m_device + 1, 0, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_context_is_valid(context1)));
    status = CL_SUCCESS;
    cq = clCreateCommandQueue(context1, device, valid_props[0], &status);
    CHECK_EQUAL(CL_INVALID_DEVICE, status);
    CHECK_EQUAL(0, cq);
    // release the invalid context, so we have room for the device0 context.
    CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context1));
  }

  // This context is used for the rest of the method

  cl_context context0 =
      clCreateContext(0, 1, &device, notify_me, this, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context0)));

  // bad device
  status = CL_SUCCESS;
  cq = clCreateCommandQueue(context0, 0, valid_props[0], &status);
  CHECK_EQUAL(CL_INVALID_DEVICE, status);
  CHECK_EQUAL(0, cq);
  status = CL_SUCCESS;
  cq = clCreateCommandQueue(context0, (cl_device_id)&status, valid_props[0],
                            &status);
  CHECK_EQUAL(CL_INVALID_DEVICE, status);
  CHECK_EQUAL(0, cq);

  // invalid properties
  status = CL_SUCCESS;
  cq = clCreateCommandQueue(context0, device, invalid_props, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  CHECK_EQUAL(0, cq);
  // unsupported properties
  for (size_t i = 0;
       i < sizeof(unsupported_props) / sizeof(unsupported_props[0]); i++) {
    status = CL_SUCCESS;
    cq = clCreateCommandQueue(context0, device, unsupported_props[i], &status);
    CHECK_EQUAL(CL_INVALID_QUEUE_PROPERTIES, status);
    CHECK_EQUAL(0, cq);
  }

  // Check error counts
  size_t callback_num =
      3 + sizeof(unsupported_props) / sizeof(cl_command_queue_properties);
  CHECK_EQUAL(callback_num, m_callback_count);

  // check if acl_command_queue_is_valid is working properly
  ACL_LOCKED(CHECK(!acl_command_queue_is_valid(0)));
  {
    cl_command_queue fake_cq = acl_alloc_cl_command_queue();
    assert(fake_cq);
    fake_cq->magic = 0xDEADBEEFDEADBEEF;
    ACL_LOCKED(CHECK(!acl_command_queue_is_valid(fake_cq)));
    acl_free_cl_command_queue(fake_cq);
  }

  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, clRetainCommandQueue(0));
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, clReleaseCommandQueue(0));

  // Create a good one, one for each properties type.
  for (size_t i = 0; i < sizeof(valid_props) / sizeof(valid_props[0]); i++) {
    status = CL_SUCCESS;
    cq = clCreateCommandQueue(context0, device, valid_props[i], &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_command_queue_is_valid(cq)));

    // wait until all threads have their command queue so that another
    // thread does not re-use ours after the final clReleaseCommandQueue below
    syncThreads();

    CHECK(cq);
    CHECK_EQUAL(1, acl_ref_count(cq));

    // Retain and release on a valid command queue:
    CHECK_EQUAL(CL_SUCCESS, clRetainCommandQueue(cq));
    CHECK_EQUAL(2, acl_ref_count(cq));
    CHECK_EQUAL(CL_SUCCESS, clRetainCommandQueue(cq));
    CHECK_EQUAL(3, acl_ref_count(cq));
    ACL_LOCKED(CHECK(acl_command_queue_is_valid(cq)));
    CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq));
    CHECK_EQUAL(2, acl_ref_count(cq));
    CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq));
    CHECK_EQUAL(1, acl_ref_count(cq));

    // check that that the memory for the command queue is freed
    CHECK_EQUAL(3, acl_get_num_alloc_cl_command_queue());
    CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq));
    CHECK_EQUAL(2, acl_get_num_alloc_cl_command_queue());

    // wait until all threads do their checks on the 0-ref-count command
    // queue before starting the next iteration of the loop and creating new
    // command queues
    syncThreads();
  }

  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context0));
  CHECK_EQUAL(0, acl_get_num_alloc_cl_command_queue());
}

MT_TEST(acl_command_queue, create_with_properties) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  cl_int status;
  cl_command_queue cq;
  // Use a non-predefined device.
  // For example, the predefined DMA device always has an associated
  // command queue in every live context.
  cl_device_id device = m_device[0];
  cl_command_queue_properties valid_props[] = {
      0, CL_QUEUE_PROFILING_ENABLE, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE};
  cl_command_queue_properties invalid_props[] = {
      (CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_PROFILING_ENABLE)
          << 1,           // all bits, shifted up
      CL_QUEUE_ON_DEVICE, // disobeying the dependency
      CL_QUEUE_ON_DEVICE_DEFAULT,
      CL_QUEUE_ON_DEVICE | CL_QUEUE_ON_DEVICE_DEFAULT,
      CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_ON_DEVICE_DEFAULT};
  cl_command_queue_properties unsupported_props[] = {
      CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_ON_DEVICE,
      CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_ON_DEVICE |
          CL_QUEUE_ON_DEVICE_DEFAULT,
      CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_ON_DEVICE |
          CL_QUEUE_ON_DEVICE_DEFAULT | CL_QUEUE_PROFILING_ENABLE};

  // Bad context
  {
    cl_queue_properties q_properties[] = {
        CL_QUEUE_PROPERTIES, (cl_command_queue_properties)valid_props[0], 0};
    syncThreads();
    acl_set_allow_invalid_type<cl_context>(1);
    syncThreads();
    struct _cl_context fake_context = {0};
    status = CL_SUCCESS;
    cq = clCreateCommandQueueWithProperties(0, device, q_properties, &status);
    CHECK_EQUAL(CL_INVALID_CONTEXT, status);
    CHECK_EQUAL(0, cq);
    status = CL_SUCCESS;
    cq = clCreateCommandQueueWithProperties(&fake_context, device, q_properties,
                                            &status);
    CHECK_EQUAL(CL_INVALID_CONTEXT, status);
    CHECK_EQUAL(0, cq);
    syncThreads();
    acl_set_allow_invalid_type<cl_context>(0);
    syncThreads();
  }

  {
    cl_queue_properties q_properties[] = {
        CL_QUEUE_PROPERTIES, (cl_command_queue_properties)valid_props[0], 0};
    CHECK(m_num_devices >= 2);
    // Context is valid, but for a different device
    cl_context context1 = clCreateContext(0, 1, m_device + 1, 0, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_context_is_valid(context1)));
    status = CL_SUCCESS;
    cq = clCreateCommandQueueWithProperties(context1, device, q_properties,
                                            &status);
    CHECK_EQUAL(CL_INVALID_DEVICE, status);
    CHECK_EQUAL(0, cq);
    // release the invalid context, so we have room for the device0 context.
    CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context1));
  }

  cl_context context0 =
      clCreateContext(0, 1, &device, notify_me, this, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context0)));

  // bad device
  {
    cl_queue_properties q_properties[] = {
        CL_QUEUE_PROPERTIES, (cl_command_queue_properties)valid_props[0], 0};
    status = CL_SUCCESS;
    cq = clCreateCommandQueueWithProperties(context0, 0, q_properties, &status);
    CHECK_EQUAL(CL_INVALID_DEVICE, status);
    CHECK_EQUAL(0, cq);
    status = CL_SUCCESS;
    cq = clCreateCommandQueueWithProperties(context0, (cl_device_id)&status,
                                            q_properties, &status);
    CHECK_EQUAL(CL_INVALID_DEVICE, status);
    CHECK_EQUAL(0, cq);
  }

  // invalid properties
  {
    cl_queue_properties q_properties2[] = {
        CL_QUEUE_PROPERTIES,
        (cl_command_queue_properties)valid_props[0],
        CL_QUEUE_SIZE,
        1,
        CL_QUEUE_SIZE,
        2,
        0}; // queue size given twice
    status = CL_SUCCESS;
    cq = clCreateCommandQueueWithProperties(context0, device, q_properties2,
                                            &status);
    CHECK_EQUAL(CL_INVALID_VALUE, status);
    CHECK_EQUAL(0, cq);
    for (size_t i = 0; i < sizeof(invalid_props) / sizeof(invalid_props[0]);
         i++) {
      cl_queue_properties q_properties[] = {
          CL_QUEUE_PROPERTIES, (cl_command_queue_properties)invalid_props[i],
          0};
      status = CL_SUCCESS;
      cq = clCreateCommandQueueWithProperties(context0, device, q_properties,
                                              &status);
      CHECK_EQUAL(CL_INVALID_VALUE, status);
      CHECK_EQUAL(0, cq);
    }
  }

  // unsupported properties
  {
    cl_queue_properties q_properties2[] = {
        CL_QUEUE_PROPERTIES, (cl_command_queue_properties)valid_props[0],
        CL_QUEUE_SIZE, 1, 0}; // queue size not supported.
    status = CL_SUCCESS;
    cq = clCreateCommandQueueWithProperties(context0, device, q_properties2,
                                            &status);
    CHECK_EQUAL(CL_INVALID_QUEUE_PROPERTIES, status);
    CHECK_EQUAL(0, cq);
    for (size_t i = 0;
         i < sizeof(unsupported_props) / sizeof(unsupported_props[0]); i++) {
      cl_queue_properties q_properties[] = {
          CL_QUEUE_PROPERTIES,
          (cl_command_queue_properties)unsupported_props[i], 0};
      status = CL_SUCCESS;
      cq = clCreateCommandQueueWithProperties(context0, device, q_properties,
                                              &status);
      CHECK_EQUAL(CL_INVALID_QUEUE_PROPERTIES, status);
      CHECK_EQUAL(0, cq);
    }
  }

  // Check error counts
  size_t callback_num =
      4 + sizeof(unsupported_props) / sizeof(cl_command_queue_properties) +
      sizeof(invalid_props) / sizeof(cl_command_queue_properties);
  CHECK_EQUAL(callback_num, m_callback_count);

  // check if acl_command_queue_is_valid is working properly
  ACL_LOCKED(CHECK(!acl_command_queue_is_valid(0)));
  {
    cl_command_queue fake_cq = acl_alloc_cl_command_queue();
    assert(fake_cq);
    fake_cq->magic = 0xDEADBEEFDEADBEEF;
    ACL_LOCKED(CHECK(!acl_command_queue_is_valid(fake_cq)));
    acl_free_cl_command_queue(fake_cq);
  }

  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, clRetainCommandQueue(0));
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, clReleaseCommandQueue(0));

  // Create a good one, one for each properties type.
  {
    // Null properties case (it is valid)
    status = CL_SUCCESS;
    cq = clCreateCommandQueueWithProperties(context0, device, NULL, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_command_queue_is_valid(cq)));

    // wait until all threads have their command queue so that another
    // thread does not re-use ours after the final clReleaseCommandQueue below
    syncThreads();

    CHECK_EQUAL(1, acl_ref_count(cq));

    // Retain and release on a valid command queue:
    CHECK_EQUAL(CL_SUCCESS, clRetainCommandQueue(cq));
    CHECK_EQUAL(2, acl_ref_count(cq));
    CHECK_EQUAL(CL_SUCCESS, clRetainCommandQueue(cq));
    CHECK_EQUAL(3, acl_ref_count(cq));
    ACL_LOCKED(CHECK(acl_command_queue_is_valid(cq)));
    CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq));
    CHECK_EQUAL(2, acl_ref_count(cq));
    CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq));
    CHECK_EQUAL(1, acl_ref_count(cq));
    CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq));

    // wait until all threads do their checks on the 0-ref-count command
    // queue before starting the next iteration of the loop and creating new
    // command queues
    syncThreads();
    // other valid cases.
    for (size_t i = 0; i < sizeof(valid_props) / sizeof(valid_props[0]); i++) {
      cl_queue_properties q_properties[] = {
          CL_QUEUE_PROPERTIES, (cl_command_queue_properties)valid_props[i], 0};
      status = CL_SUCCESS;
      cq = clCreateCommandQueueWithProperties(context0, device, q_properties,
                                              &status);
      CHECK_EQUAL(CL_SUCCESS, status);
      ACL_LOCKED(CHECK(acl_command_queue_is_valid(cq)));

      // wait until all threads have their command queue so that another
      // thread does not re-use ours after the final clReleaseCommandQueue below
      syncThreads();

      CHECK_EQUAL(1, acl_ref_count(cq));

      // Retain and release on a valid command queue:
      CHECK_EQUAL(CL_SUCCESS, clRetainCommandQueue(cq));
      CHECK_EQUAL(2, acl_ref_count(cq));
      CHECK_EQUAL(CL_SUCCESS, clRetainCommandQueue(cq));
      CHECK_EQUAL(3, acl_ref_count(cq));
      ACL_LOCKED(CHECK(acl_command_queue_is_valid(cq)));
      CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq));
      CHECK_EQUAL(2, acl_ref_count(cq));
      CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq));
      CHECK_EQUAL(1, acl_ref_count(cq));
      CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq));

      // wait until all threads do their checks on the 0-ref-count command
      // queue before starting the next iteration of the loop and creating new
      // command queues
      syncThreads();
    }
  }
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context0));
}

MT_TEST(acl_command_queue, disable_out_of_order_queues) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  cl_int status = CL_SUCCESS;
  // Use a non-predefined device.
  // For example, the predefined DMA device always has an associated
  // command queue in every live context.
  cl_device_id device = m_device[0];
  cl_context context0 =
      clCreateContext(0, 1, &device, notify_me, this, &status);
  cl_queue_properties q_properties[] = {
      CL_QUEUE_PROPERTIES, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, 0};

  acl_test_setenv("CL_CONTEXT_DISABLE_OOO_QUEUES_INTELFPGA", "1");

  cl_command_queue cq = clCreateCommandQueueWithProperties(
      context0, device, q_properties, &status);
  CHECK_EQUAL(CL_INVALID_QUEUE_PROPERTIES, status);
  CHECK_EQUAL(0, cq);

  acl_test_unsetenv("CL_CONTEXT_DISABLE_OOO_QUEUES_INTELFPGA");
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context0));
  acl_set_allow_invalid_type<cl_command_queue>(0);
}

MT_TEST(acl_command_queue, exceed_initial_command_queue_alloc) {
  cl_int status;
  cl_context contexts[2];
  for (unsigned n_context = 0; n_context < 2; n_context++) {
    contexts[n_context] =
        clCreateContext(0, 1, &m_device[n_context], 0, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_context_is_valid(contexts[n_context])));
  }

  static AtomicUInt global_num_cq;
  if (threadNum() == 0) {
    // reset global_num_cq to 0
    global_num_cq = AtomicUInt();
  }

  syncThreads();

  // Test on 2 contexts, each allocates (ACL_INIT_COMMAND_QUEUE_ALLOC + 1)
  // command queues
  int num_cq = 0;
  const unsigned num_cq_in_context = ACL_INIT_COMMAND_QUEUE_ALLOC + 1;
  const unsigned max_cq = 2 * num_cq_in_context;
  cl_command_queue cq[max_cq];
  for (unsigned n_context = 0; n_context < 2; n_context++) {
    for (unsigned i = 0; i < num_cq_in_context; i++) {
      status = CL_SUCCESS;
      cl_command_queue the_cq = clCreateCommandQueue(
          contexts[n_context], m_device[n_context], 0, &status);
      if (status == CL_SUCCESS) {
        ACL_LOCKED(CHECK(acl_command_queue_is_valid(the_cq)));
        CHECK(the_cq);
        CHECK_EQUAL(0, the_cq->num_commands);
        cq[num_cq] = the_cq;
        num_cq++;
        global_num_cq.fetchAndAdd(1);
      } else {
        CHECK_EQUAL(CL_OUT_OF_HOST_MEMORY, status);
        break;
      }
    }
  }
  syncThreads();
  if (status !=
      CL_OUT_OF_HOST_MEMORY) { // we successfully allocated all command queues.
    CHECK(global_num_cq == max_cq);
  }

  // Distinctness
  for (int i = 0; i < num_cq; i++) {
    for (int j = 0; j < i; j++) {
      CHECK(cq[i] != cq[j]);
    }
  }

  // Unwind
  for (int i = 0; i < num_cq; i++) {
    CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq[i]));
  }

  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(contexts[0]));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(contexts[1]));
}

#ifdef CL_USE_DEPRECATED_OPENCL_1_0_APIS
MT_TEST(acl_command_queue, set_prop) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  cl_int status;
  cl_context context =
      clCreateContext(0, m_num_devices, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));

  for (unsigned i = 0; i < m_num_devices; i++) {
    cl_command_queue_properties device_props = 0;
    CHECK_EQUAL(CL_SUCCESS,
                clGetDeviceInfo(m_device[i], CL_DEVICE_QUEUE_PROPERTIES,
                                sizeof(device_props), &device_props, 0));

    cl_command_queue cq =
        clCreateCommandQueue(context, m_device[i], 0, &status);

    // Check valid command queue
    struct _cl_command_queue fake_cq = {0};
    CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
                clSetCommandQueueProperty(0, 0, CL_TRUE, 0));
    CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
                clSetCommandQueueProperty(&fake_cq, 0, CL_TRUE, 0));

    const cl_command_queue_properties invalid_props =
        (CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_PROFILING_ENABLE)
        << 1 // all bits, shifted up
        ;
    CHECK_EQUAL(CL_INVALID_VALUE,
                clSetCommandQueueProperty(cq, invalid_props, CL_TRUE, 0));

    cl_command_queue_properties valid_props[] = {
        CL_QUEUE_PROFILING_ENABLE, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE};

    // Try setting all possible bits
    for (unsigned j = 0; j < sizeof(valid_props) / sizeof(valid_props[0]);
         j++) {
      cl_command_queue_properties old_props = invalid_props;
      cl_command_queue_properties new_props = invalid_props;
      // Get the old properties.
      CHECK_EQUAL(CL_SUCCESS,
                  clSetCommandQueueProperty(cq, 0, CL_TRUE, &old_props));

      // Try to set the new property
      status =
          clSetCommandQueueProperty(cq, valid_props[j], CL_TRUE, &old_props);
      // get the new value
      CHECK_EQUAL(CL_SUCCESS,
                  clSetCommandQueueProperty(cq, 0, CL_TRUE, &new_props));
      if (valid_props[j] & device_props) {
        CHECK_EQUAL(CL_SUCCESS, status);
        CHECK_EQUAL(old_props | valid_props[j], new_props);
      } else {
        CHECK_EQUAL(CL_INVALID_QUEUE_PROPERTIES, status);
        CHECK_EQUAL(old_props, new_props);
      }
    }

    // Try unsetting all possible bits
    for (unsigned j = 0; j < sizeof(valid_props) / sizeof(valid_props[0]);
         j++) {
      cl_command_queue_properties old_props = invalid_props;
      cl_command_queue_properties new_props = invalid_props;
      // Get the old properties.
      CHECK_EQUAL(CL_SUCCESS,
                  clSetCommandQueueProperty(cq, 0, CL_TRUE, &old_props));

      // Try to set the new property
      status =
          clSetCommandQueueProperty(cq, valid_props[j], CL_FALSE, &old_props);
      // get the new value
      CHECK_EQUAL(CL_SUCCESS,
                  clSetCommandQueueProperty(cq, 0, CL_TRUE, &new_props));
      if (valid_props[j] & device_props) {
        CHECK_EQUAL(CL_SUCCESS, status);
        CHECK_EQUAL(old_props & (~valid_props[j]), new_props);
      } else {
        CHECK_EQUAL(CL_INVALID_QUEUE_PROPERTIES, status);
        CHECK_EQUAL(old_props, new_props);
      }
    }

    cl_command_queue_properties final_props = invalid_props;
    CHECK_EQUAL(CL_SUCCESS,
                clSetCommandQueueProperty(cq, 0, CL_TRUE, &final_props));
    CHECK_EQUAL(0, final_props);

    // Can send NULL for old_props:
    CHECK_EQUAL(CL_SUCCESS, clSetCommandQueueProperty(cq, 0, CL_TRUE, 0));

    CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq));
  }

  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context));
}
#endif

MT_TEST(acl_command_queue, after_context_release) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  // OpenCL 1.0 conformance_test_multiples will release the command queues
  // after the underlying contexts are gone.

  cl_int status;
  cl_context context = clCreateContext(0, 1, &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_context_is_valid(context)));

  status = 42;
  cl_command_queue cq0 = clCreateCommandQueue(context, m_device[0], 0, &status);
  CHECK(cq0);
  CHECK_EQUAL(CL_SUCCESS, status);

  status = 95;
  cl_command_queue cq1 = clCreateCommandQueue(context, m_device[0], 0, &status);
  CHECK(cq1);
  CHECK_EQUAL(CL_SUCCESS, status);

  // wait until all threads have their context and command queues before
  // continuing so that releasing them below won't cause them to be
  // immediately reused for another thread
  syncThreads();

  // Early release of the context!
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context));

  CHECK(cq0);
  CHECK_EQUAL(1, acl_ref_count(cq0));
  CHECK(cq1);
  CHECK_EQUAL(1, acl_ref_count(cq1));

  // Should be able to clFinish if there are no commands enqueued.
  CHECK_EQUAL(CL_SUCCESS, clFinish(cq0));
  CHECK_EQUAL(CL_SUCCESS, clFinish(cq1));
  // Should be able to clFlush if there are no commands enqueued.
  CHECK_EQUAL(CL_SUCCESS, clFlush(cq0));
  CHECK_EQUAL(CL_SUCCESS, clFlush(cq1));

  // Should be able to retain....
  CHECK_EQUAL(CL_SUCCESS, clRetainCommandQueue(cq0));
  CHECK_EQUAL(CL_SUCCESS, clRetainCommandQueue(cq1));
  CHECK_EQUAL(2, acl_ref_count(cq0));
  CHECK_EQUAL(2, acl_ref_count(cq1));

  // Should be able to release...
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq0));
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq1));
  CHECK_EQUAL(1, acl_ref_count(cq0));
  CHECK_EQUAL(1, acl_ref_count(cq1));

  // Should be able to release all the way.
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq0));
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq1));
}

// Main Event is in an OOO queue. It has a dependent event in an in-order queue.
// Test that completing the main event, will unblock the dependent event.
MT_TEST(acl_command_queue, mixed_queue_dependencies_1) {
  // Infrastructure
  cl_int status;
  cl_context context =
      clCreateContext(acl_test_context_prop_preloaded_binary_only(), 1,
                      &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  const unsigned char *bin = (const unsigned char *)"0";
  size_t bin_length = 1;
  cl_int bin_status;
  cl_program program = clCreateProgramWithBinary(
      context, 1, &m_device[0], &bin_length, &bin, &bin_status, &status);
  CHECK_EQUAL(CL_SUCCESS, bin_status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(CL_SUCCESS, clBuildProgram(program, 0, 0, "", 0, 0));
  cl_kernel kernel = clCreateKernel(program, "kernel4_task_double", &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  cl_mem src_mem = clCreateBuffer(context, CL_MEM_READ_ONLY, 64, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));
  cl_command_queue oooq = clCreateCommandQueue(
      context, m_device[0], CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  cl_command_queue inoq =
      clCreateCommandQueue(context, m_device[0], 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Check expectations of what the command queue looks like.
  CHECK(oooq);
  CHECK_EQUAL(0, oooq->num_commands);
  CHECK_EQUAL(0, oooq->num_commands_submitted);
  CHECK(inoq);
  CHECK_EQUAL(0, inoq->num_commands);
  CHECK_EQUAL(0, inoq->num_commands_submitted);

  // Set up the event dependencies
  cl_event e1 = nullptr, e2 = nullptr;
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(oooq, kernel, 0, 0, &e1));
  CHECK(e1);
  CHECK_EQUAL(CL_SUBMITTED, e1->execution_status);
  CHECK_EQUAL(1, oooq->num_commands);
  CHECK_EQUAL(1, oooq->num_commands_submitted);
  CHECK_EQUAL(1, oooq->commands.size());
  CHECK(oooq->completed_commands.empty());
  CHECK_EQUAL(0, inoq->num_commands);

  CHECK_EQUAL(CL_SUCCESS, clEnqueueMarkerWithWaitList(inoq, 1, &e1, &e2));
  CHECK(e2);
  CHECK_EQUAL(CL_SUBMITTED, e1->execution_status);
  CHECK_EQUAL(CL_QUEUED, e2->execution_status);
  CHECK_EQUAL(1, oooq->num_commands);
  CHECK_EQUAL(1, inoq->num_commands);
  CHECK_EQUAL(0, inoq->num_commands_submitted);

  // Finish e1, see if e2 gets submitted
  CHECK_EQUAL(e1->last_device_op, e1->current_device_op);
  ACL_LOCKED(acl_receive_kernel_update(e1->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(context));
  ACL_LOCKED(acl_receive_kernel_update(e1->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(context));

  CHECK_EQUAL(CL_COMPLETE, e2->execution_status); // The important check
  CHECK_EQUAL(0, oooq->num_commands);
  CHECK_EQUAL(0, inoq->num_commands);

  // Cleanup
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(e1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(e2));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(src_mem));
  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(kernel));
  CHECK_EQUAL(CL_SUCCESS, clReleaseProgram(program));
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(oooq));
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(inoq));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context));
}

// Main Event is in an in-order queue. It has a dependent event in an OOO queue.
// Test that completing the main event, will unblock the dependent event.
MT_TEST(acl_command_queue, mixed_queue_dependencies_2) {
  // Infrastructure
  cl_int status;
  cl_context context =
      clCreateContext(acl_test_context_prop_preloaded_binary_only(), 1,
                      &m_device[0], 0, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  const unsigned char *bin = (const unsigned char *)"0";
  size_t bin_length = 1;
  cl_int bin_status;
  cl_program program = clCreateProgramWithBinary(
      context, 1, &m_device[0], &bin_length, &bin, &bin_status, &status);
  CHECK_EQUAL(CL_SUCCESS, bin_status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(CL_SUCCESS, clBuildProgram(program, 0, 0, "", 0, 0));
  cl_kernel kernel = clCreateKernel(program, "kernel4_task_double", &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  cl_mem src_mem = clCreateBuffer(context, CL_MEM_READ_ONLY, 64, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));
  cl_command_queue oooq = clCreateCommandQueue(
      context, m_device[0], CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  cl_command_queue inoq =
      clCreateCommandQueue(context, m_device[0], 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Check expectations of what the command queue looks like.
  CHECK(oooq);
  CHECK_EQUAL(0, oooq->num_commands);
  CHECK_EQUAL(0, oooq->num_commands_submitted);
  CHECK(inoq);
  CHECK_EQUAL(0, inoq->num_commands);
  CHECK_EQUAL(0, inoq->num_commands_submitted);

  // Set up the event dependencies
  cl_event e1 = nullptr, e2 = nullptr;
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(inoq, kernel, 0, 0, &e1));
  CHECK(e1);
  CHECK_EQUAL(CL_SUBMITTED, e1->execution_status);
  CHECK_EQUAL(1, inoq->num_commands);
  CHECK_EQUAL(1, inoq->num_commands_submitted);
  CHECK_EQUAL(0, oooq->num_commands);

  CHECK_EQUAL(CL_SUCCESS, clEnqueueMarkerWithWaitList(oooq, 1, &e1, &e2));
  CHECK(e2);
  CHECK_EQUAL(CL_SUBMITTED, e1->execution_status);
  CHECK_EQUAL(CL_QUEUED, e2->execution_status);
  CHECK_EQUAL(1, inoq->num_commands);
  CHECK_EQUAL(1, oooq->num_commands);
  CHECK_EQUAL(0, oooq->num_commands_submitted);

  // Finish e1, see if e2 gets submitted
  CHECK_EQUAL(e1->last_device_op, e1->current_device_op);
  ACL_LOCKED(acl_receive_kernel_update(e1->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(context));
  ACL_LOCKED(acl_receive_kernel_update(e1->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(context));

  CHECK_EQUAL(CL_COMPLETE, e2->execution_status); // The important check
  CHECK_EQUAL(0, oooq->num_commands);
  CHECK_EQUAL(0, inoq->num_commands);

  // Cleanup
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(e1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(e2));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(src_mem));
  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(kernel));
  CHECK_EQUAL(CL_SUCCESS, clReleaseProgram(program));
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(oooq));
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(inoq));
  CHECK_EQUAL(CL_SUCCESS, clReleaseContext(context));
}
