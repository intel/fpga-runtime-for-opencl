// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter
#define NOMINMAX                // required to use std::numeric_limits<>::max()
#endif
#include <CppUTest/TestHarness.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <CL/opencl.h>

#include <acl.h>
#include <acl_command_queue.h>
#include <acl_context.h>
#include <acl_device_op.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_kernel.h>
#include <acl_mem.h>
#include <acl_printf.h>
#include <acl_program.h>
#include <acl_support.h>
#include <acl_types.h>
#include <acl_util.h>
#include <unref.h>

#include <assert.h>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdlib.h>
#include <string.h>

#include "acl_device_op_test.h"
#include "acl_hal_test.h"
#include "acl_test.h"

static void CL_CALLBACK test_debug_print(const char *errinfo,
                                         const void *private_info, size_t cb,
                                         void *user_data);

static const acl_device_op_t *l_find_op(int id) {
  const acl_device_op_queue_t *doq = &acl_platform.device_op_queue;
  if (id >= 0 && id < doq->max_ops) {
    return doq->op + id;
  }
  return 0;
}

// A default empty program binary
#define EXAMPLE_BINARY                                                         \
  const unsigned char *l_bin[m_num_devices_in_context];                        \
  size_t l_bin_lengths[m_num_devices_in_context];                              \
  cl_int l_bin_status[m_num_devices_in_context];                               \
  for (cl_uint i = 0; i < m_num_devices_in_context; i++) {                     \
    l_bin[i] = (const unsigned char *)"0";                                     \
    l_bin_lengths[i] = 1;                                                      \
  }

MT_TEST_GROUP(acl_kernel) {
  // Preloads a standard platform, and finds all devices.
  enum { MAX_DEVICES = 100, m_num_devices_in_context = 3 };

  void setup() {
    if (threadNum() == 0) {
      acl_test_setup_generic_system();
      acl_dot_push(&m_devlog, &acl_platform.device_op_queue);
    }
    syncThreads();

    this->load();
    m_program = this->load_program();
    this->build(m_program);

    // See acl_globals_test.cpp
    m_sample_kernel_name = "kernel0_copy_vecin_vecout";
    m_sample_kernel_accel_id = 0;
    m_sample_task_name = "kernel4_task_double";
    m_sample_task_accel_id = 3; // Yep, it's #3 in "fpga0".
    m_sample_kernel_num_args = 5;
    // This is for testing pointer-to-local arguments
    m_sample_locals_name = "kernel3_locals";
    m_sample_locals_num_args = 4;

    m_sample_kernel_workgroup_invariant_name = "kernel5_double";
    m_sample_kernel_workgroup_invariant_accel_id = 5;

    m_sample_kernel_mgwd_zero_name = "kernel7_emptyprofiletest";
    m_sample_kernel_mgwd_zero_accel_id = 7;

    m_sample_kernel_mgwd_one_name = "kernel8_svm_args";
    m_sample_kernel_mgwd_one_accel_id = 8;

    m_sample_kernel_mgwd_two_name = "kernel6_profiletest";
    m_sample_kernel_mgwd_two_accel_id = 9;

    m_sample_kernel_workitem_invariant_name = "kernel11_task_double";
    m_sample_kernel_workitem_invariant_accel_id = 10;

    // same kernel as regular task, just different attributes
    m_sample_task_with_fast_relaunch_name = "kernel12_task_double";
    m_sample_task_with_fast_relaunch_accel_id = 11;

    // Kernel CRA uses 32 bits for pointer-to-local.
    CHECK(m_cq);
    m_dev_global_ptr_size_in_cra = ((unsigned)m_cq->device->address_bits) >> 3;
    m_dev_local_ptr_size_in_cra = 4; // should stay this way.
  }
  void teardown() {
    syncThreads();

    // Revert to set device to use all memory types
    acl_test_hal_set_svm_memory_support(
        (int)(CL_DEVICE_SVM_COARSE_GRAIN_BUFFER |
              CL_DEVICE_SVM_FINE_GRAIN_BUFFER |
              CL_DEVICE_SVM_FINE_GRAIN_SYSTEM));
    acl_test_hal_set_physical_memory_support(true);

    acl_set_allow_invalid_type<cl_kernel>(1);
    acl_set_allow_invalid_type<cl_program>(1);
    this->unload_program(m_program);
    this->unload();
    acl_set_allow_invalid_type<cl_kernel>(0);
    acl_set_allow_invalid_type<cl_program>(0);

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
    CHECK(m_num_devices > 0);

    cl_int status;
    CHECK(m_num_devices_in_context < m_num_devices);
    m_context =
        clCreateContext(acl_test_context_prop_preloaded_binary_only(),
                        m_num_devices_in_context, &m_device[0], 0, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_context);

    status = CL_INVALID_VALUE;
    m_context2 =
        clCreateContext(acl_test_context_prop_preloaded_binary_only(), 1,
                        &m_device[m_num_devices_in_context], 0, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_context2);

    status = CL_INVALID_VALUE;
    m_context_verbose = clCreateContext(
        acl_test_context_prop_preloaded_binary_only(), m_num_devices_in_context,
        &m_device[0], test_debug_print, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_context_verbose);

    m_cq = clCreateCommandQueue(m_context, m_device[0],
                                CL_QUEUE_PROFILING_ENABLE, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_cq);

    m_cq_verbose = clCreateCommandQueue(m_context_verbose, m_device[0],
                                        CL_QUEUE_PROFILING_ENABLE, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_cq_verbose);

    syncThreads();
  }

  void unload(void) {
    int cq_is_valid, cq_verbose_is_valid;
    int context_is_valid, context2_is_valid, context_verbose_is_valid;
    ACL_LOCKED(cq_is_valid = acl_command_queue_is_valid(m_cq));
    ACL_LOCKED(cq_verbose_is_valid = acl_command_queue_is_valid(m_cq_verbose));
    ACL_LOCKED(context_is_valid = acl_context_is_valid(m_context));
    ACL_LOCKED(context2_is_valid = acl_context_is_valid(m_context2));
    ACL_LOCKED(context_verbose_is_valid =
                   acl_context_is_valid(m_context_verbose));

    if (cq_is_valid) {
      clReleaseCommandQueue(m_cq);
    }
    if (cq_verbose_is_valid) {
      clReleaseCommandQueue(m_cq_verbose);
    }
    if (context_is_valid) {
      clReleaseContext(m_context);
    }
    if (context2_is_valid) {
      clReleaseContext(m_context2);
    }
    if (context_verbose_is_valid) {
      clReleaseContext(m_context_verbose);
    }
  }

  cl_program load_program(cl_uint num_devices = m_num_devices_in_context,
                          int idev = 0) {
    cl_int status;
    cl_program program;
    EXAMPLE_BINARY;

    {
      cl_uint i;
      ACL_LOCKED(acl_print_debug_msg("Loading program for devices:"));
      for (i = 0; i < num_devices; i++) {
        ACL_LOCKED(acl_print_debug_msg(
            " %d:%s", m_device[idev + i]->id,
            m_device[idev + i]->def.autodiscovery_def.name.c_str()));
      }
      ACL_LOCKED(acl_print_debug_msg("\n"));
    }
    status = CL_INVALID_VALUE;
    program = clCreateProgramWithBinary(m_context, num_devices, &m_device[idev],
                                        l_bin_lengths, l_bin, &l_bin_status[0],
                                        &status);
    {
      cl_uint i;
      for (i = 0; i < num_devices; i++) {
        CHECK_EQUAL(CL_SUCCESS, l_bin_status[i]);
      }
    }
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(program);
    ACL_LOCKED(CHECK(acl_program_is_valid(program)));
    return program;
  }
  cl_program load_program(cl_context context,
                          cl_uint num_devices = m_num_devices_in_context,
                          int idev = 0) {
    cl_int status;
    cl_program program;
    EXAMPLE_BINARY;

    status = CL_INVALID_VALUE;
    program = clCreateProgramWithBinary(context, num_devices, &m_device[idev],
                                        l_bin_lengths, l_bin, &l_bin_status[0],
                                        &status); // program with custom context
    {
      cl_uint i;
      for (i = 0; i < num_devices; i++) {
        CHECK_EQUAL(CL_SUCCESS, l_bin_status[i]);
      }
    }
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(program);
    ACL_LOCKED(CHECK(acl_program_is_valid(program)));
    return program;
  }
  void unload_program(cl_program program) {
    int program_is_valid;
    ACL_LOCKED(program_is_valid = acl_program_is_valid(program));
    if (program_is_valid) {
      cl_int status;
      while (program->num_kernels) {
        status = clReleaseKernel(program->kernel_list->kernel);
        CHECK_EQUAL(CL_SUCCESS, status);
      }
      status = clReleaseProgram(program);
      CHECK_EQUAL(CL_SUCCESS, status);
    }
  }
  void build(cl_program program) {
    CHECK_EQUAL(CL_SUCCESS, clBuildProgram(program, 0, 0, "", 0, 0));
  }

  void load_times(cl_event event, cl_ulong times[4]) {
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_QUEUED,
                                        sizeof(times[0]), &times[0], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_SUBMIT,
                                        sizeof(times[1]), &times[1], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START,
                                        sizeof(times[2]), &times[2], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END,
                                        sizeof(times[3]), &times[3], 0));
  }

  size_t adjust_for_alignment(size_t x) {
    // Round up a block size to mem alignment boundary.
    size_t fuzz = x & (ACL_MEM_ALIGN - 1);
    if (fuzz)
      x += (ACL_MEM_ALIGN - fuzz);
    return x;
  }

  size_t get_static_allocation_for_sample_kernel() {
    size_t expected = 0;
    size_t demands[] = {
        16768, 2048}; // the sample kernel has two buffers, of these sizes.
    for (unsigned int i = 0; i < sizeof(demands) / sizeof(demands[0]); i++) {
      // Accumulate space for each statically defined local variable.
      // But round up to granularity of mem buffer alignment.
      expected += demands[i];
      if (demands[i] % ACL_MEM_ALIGN)
        expected += ACL_MEM_ALIGN - (demands[i] % ACL_MEM_ALIGN);
    }
    return expected;
  }
  cl_event get_user_event(void) {
    cl_int status;
    cl_event event = clCreateUserEvent(m_context, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_event_is_valid(event)));
    CHECK_EQUAL(1, acl_ref_count(event));
    ACL_LOCKED(CHECK(acl_event_is_live(event)));
    return event;
  }

  void complete_command_queue(cl_command_queue command_queue) {
    // this method completes all the commands left in the queue, and frees them
    // at the end of one test
    int done_updates = 0, total_updates = command_queue->num_commands;
    for (auto it = command_queue->inorder_commands.rbegin();
         it != command_queue->inorder_commands.rend(); ++it) {
      ACL_LOCKED(acl_set_execution_status(*it, CL_COMPLETE));
    }
    // update the queue since all the commands are complete
    ACL_LOCKED(done_updates = acl_update_queue(command_queue));
    assert(done_updates == total_updates);
  }

protected:
  cl_platform_id m_platform;
  cl_device_id m_device[MAX_DEVICES];
  cl_uint m_num_devices;
  cl_context m_context, m_context2, m_context_verbose;
  cl_program m_program;
  const char *m_sample_kernel_name;
  const char *m_sample_task_name;
  const char *m_sample_task_with_fast_relaunch_name;
  const char *m_sample_locals_name;
  const char *m_sample_kernel_workgroup_invariant_name;
  const char *m_sample_kernel_mgwd_zero_name;
  const char *m_sample_kernel_mgwd_one_name;
  const char *m_sample_kernel_mgwd_two_name;
  const char *m_sample_kernel_workitem_invariant_name;
  cl_uint m_sample_kernel_accel_id;
  cl_uint m_sample_task_accel_id;
  cl_uint m_sample_task_with_fast_relaunch_accel_id;
  cl_uint m_sample_kernel_workgroup_invariant_accel_id;
  cl_uint m_sample_kernel_mgwd_zero_accel_id;
  cl_uint m_sample_kernel_mgwd_one_accel_id;
  cl_uint m_sample_kernel_mgwd_two_accel_id;
  cl_uint m_sample_kernel_workitem_invariant_accel_id;
  cl_uint m_sample_kernel_num_args;
  cl_uint m_sample_locals_num_args;
  cl_command_queue m_cq, m_cq_verbose;
  static acl_device_op_test_ctx_t m_devlog;

  unsigned int m_dev_global_ptr_size_in_cra; // in bytes.
  unsigned int m_dev_local_ptr_size_in_cra;  // in bytes.
};

static void CL_CALLBACK test_debug_print(const char *errinfo,
                                         const void *private_info, size_t cb,
                                         void *user_data) {
  std::cout << errinfo;
  user_data = user_data; // avoid warning on windows
  cb = cb;
  private_info = private_info;
}

TEST_GROUP_STATIC(acl_kernel, acl_device_op_test_ctx_t, m_devlog);

MT_TEST(acl_kernel, link_kernel) {
  ACL_LOCKED(acl_print_debug_msg("link_kernel\n"));
  clRetainKernel(0);
  clReleaseKernel(0);
  clCreateKernel(0, 0, 0);
  clCreateKernelsInProgram(0, 0, 0, 0);
  clSetKernelArg(0, 0, 0, 0);
  clGetKernelInfo(0, 0, 0, 0, 0);
  clGetKernelWorkGroupInfo(0, 0, 0, 0, 0, 0);

  ACL_LOCKED(acl_kernel_is_valid_ptr(0));
  ACL_LOCKED(acl_kernel_is_valid(0));
  ACL_LOCKED(acl_reset_kernel(0));
}

MT_TEST(acl_kernel, ptr_check) {
  cl_kernel kernel;
  ACL_LOCKED(acl_print_debug_msg("ptr_check\n"));
  acl_set_allow_invalid_type<cl_kernel>(1);
  ACL_LOCKED(CHECK(!acl_kernel_is_valid_ptr(0)));
  ACL_LOCKED(CHECK(!acl_kernel_is_valid_ptr(
      (cl_kernel)m_program))); // avoid non-trivial segfault.
  ACL_LOCKED(kernel = acl_program_alloc_kernel(m_program));
  ACL_LOCKED(acl_program_forget_kernel(m_program, kernel));
  acl_set_allow_invalid_type<cl_kernel>(0);
}

MT_TEST(acl_kernel, create_bad) {
  ACL_LOCKED(acl_print_debug_msg("create_bad\n"));
  cl_int status;

  // Bad program
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(1);
  syncThreads();
  struct _cl_program fake_program = {0};
  status = CL_SUCCESS;
  CHECK_EQUAL(0, clCreateKernel(0, "foo", &status));
  CHECK_EQUAL(CL_INVALID_PROGRAM, status);
  status = CL_SUCCESS;
  CHECK_EQUAL(0, clCreateKernel(&fake_program, "foo", &status));
  CHECK_EQUAL(CL_INVALID_PROGRAM, status);
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(0);
  syncThreads();

  cl_program unbuilt = this->load_program();
  status = CL_INVALID_PROGRAM;
  CHECK(0 != clCreateKernel(unbuilt, m_sample_kernel_name, &status));
  CHECK_EQUAL(CL_SUCCESS, status);
  this->unload_program(unbuilt);

  // Bad name
  status = CL_SUCCESS;
  CHECK_EQUAL(0, clCreateKernel(m_program, 0, &status));
  CHECK_EQUAL(CL_INVALID_VALUE, status);
  status = CL_SUCCESS;
  CHECK_EQUAL(0, clCreateKernel(m_program, "", &status));
  CHECK_EQUAL(CL_INVALID_KERNEL_NAME, status);
  status = CL_SUCCESS;
  CHECK_EQUAL(0, clCreateKernel(m_program, "does_not_exist", &status));
  CHECK_EQUAL(CL_INVALID_KERNEL_NAME, status);

  CHECK_EQUAL(0, m_program->num_kernels);
}

MT_TEST(acl_kernel, get_info_bad_kernel) {
  ACL_LOCKED(acl_print_debug_msg("get_info_bad_kernel\n"));
  acl_set_allow_invalid_type<cl_kernel>(1);
  CHECK_EQUAL(CL_INVALID_KERNEL, clGetKernelInfo(0, 0, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_KERNEL,
              clGetKernelInfo((cl_kernel)m_program, 0, 0, 0, 0));
  acl_set_allow_invalid_type<cl_kernel>(0);
}

MT_TEST(acl_kernel, create) {
  ACL_LOCKED(acl_print_debug_msg("create\n"));
  cl_int status;
  cl_kernel kernel;

  status = CL_INVALID_VALUE;
  kernel = clCreateKernel(m_program, m_sample_kernel_name, &status);

  // Since we're testing ref counts going to 0 below, we need to make sure
  // another thread doesn't take a kernel object when this thread's object's
  // ref count goes to 0. This syncThreads() makes that happen by forcing all
  // threads to wait until all other threads have also called clCreateKernel().
  syncThreads();

  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(kernel != 0);
  ACL_LOCKED(CHECK(acl_kernel_is_valid_ptr(kernel)));
  ACL_LOCKED(CHECK(acl_kernel_is_valid(kernel)));

  // Check device program
  ACL_LOCKED(CHECK(acl_program_is_valid(kernel->program)));
  CHECK(kernel->program);
  CHECK(kernel->dev_bin);
  CHECK_EQUAL(kernel->dev_bin->get_dev_prog()->program, kernel->program);
  CHECK_EQUAL(CL_BUILD_SUCCESS, kernel->dev_bin->get_dev_prog()->build_status);
  // And the program is built for this device.... not checked.
  // And the device has this kernel in it... not checked.

  // Check Retain and Release
  acl_set_allow_invalid_type<cl_kernel>(1);
  CHECK_EQUAL(CL_INVALID_KERNEL, clRetainKernel(0));
  CHECK_EQUAL(CL_INVALID_KERNEL, clRetainKernel((cl_kernel)m_program));
  CHECK_EQUAL(CL_INVALID_KERNEL, clReleaseKernel(0));
  CHECK_EQUAL(CL_INVALID_KERNEL, clReleaseKernel((cl_kernel)m_program));
  acl_set_allow_invalid_type<cl_kernel>(0);

  CHECK_EQUAL(1, acl_ref_count(kernel));
  CHECK_EQUAL(CL_SUCCESS, clRetainKernel(kernel));
  CHECK_EQUAL(2, acl_ref_count(kernel));
  ACL_LOCKED(CHECK(acl_kernel_is_valid(kernel)));
  CHECK_EQUAL(CL_SUCCESS, clRetainKernel(kernel));
  CHECK_EQUAL(3, acl_ref_count(kernel));
  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(kernel));
  CHECK_EQUAL(2, acl_ref_count(kernel));
  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(kernel));
  CHECK_EQUAL(1, acl_ref_count(kernel));
  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(kernel));
  CHECK_EQUAL(0, acl_ref_count(kernel));
  ACL_LOCKED(CHECK(!acl_kernel_is_valid(kernel)));
  ACL_LOCKED(CHECK(acl_kernel_is_valid_ptr(kernel)));
  CHECK_EQUAL(0, m_program->num_kernels);
}

MT_TEST(acl_kernel, get_info) {
  ACL_LOCKED(acl_print_debug_msg("get_info\n"));
  cl_int status;
  cl_kernel kernel;

  status = CL_INVALID_VALUE;
  kernel = clCreateKernel(m_program, m_sample_kernel_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Test clGetKernelInfo, where we have a valid kernel.
  // Bad param name:
  CHECK_EQUAL(CL_INVALID_VALUE, clGetKernelInfo(kernel, 0, 0, 0, 0));
  // Return buf size too small:
  cl_uint num_args;
  size_t size_ret;
  CHECK_EQUAL(CL_INVALID_VALUE, clGetKernelInfo(kernel, CL_KERNEL_NUM_ARGS, 1,
                                                &num_args, &size_ret));
  // Param size zero, ptr non-zero:
  CHECK_EQUAL(CL_INVALID_VALUE, clGetKernelInfo(kernel, CL_KERNEL_NUM_ARGS, 0,
                                                &num_args, &size_ret));
  // Param size non-zero, ptr zero:
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetKernelInfo(kernel, CL_KERNEL_NUM_ARGS, sizeof(num_args), 0,
                              &size_ret));

  // Query only return size.
  CHECK_EQUAL(CL_SUCCESS,
              clGetKernelInfo(kernel, CL_KERNEL_NUM_ARGS, 0, 0, &size_ret));
  CHECK_EQUAL(sizeof(cl_uint), size_ret);

  // Test values returned from get info:
  char str[1000];
  CHECK_EQUAL(CL_SUCCESS,
              clGetKernelInfo(kernel, CL_KERNEL_FUNCTION_NAME,
                              sizeof(str) / sizeof(str[0]), str, &size_ret));
  CHECK_EQUAL(strlen(m_sample_kernel_name) + 1, size_ret); // remember NUL
  CHECK_EQUAL(0, strcmp(m_sample_kernel_name, str));

  CHECK_EQUAL(CL_SUCCESS,
              clGetKernelInfo(kernel, CL_KERNEL_NUM_ARGS, sizeof(num_args),
                              &num_args, &size_ret));
  CHECK_EQUAL(sizeof(num_args), size_ret);
  CHECK_EQUAL(m_sample_kernel_num_args, num_args);

  cl_uint refcnt;
  CHECK_EQUAL(CL_SUCCESS, clGetKernelInfo(kernel, CL_KERNEL_REFERENCE_COUNT,
                                          sizeof(refcnt), &refcnt, &size_ret));
  CHECK_EQUAL(sizeof(refcnt), size_ret);
  CHECK_EQUAL(1, refcnt);
  CHECK_EQUAL(acl_ref_count(kernel), refcnt);
  CHECK_EQUAL(CL_SUCCESS, clRetainKernel(kernel));
  CHECK_EQUAL(CL_SUCCESS, clGetKernelInfo(kernel, CL_KERNEL_REFERENCE_COUNT,
                                          sizeof(refcnt), &refcnt, &size_ret));
  CHECK_EQUAL(2, refcnt);
  CHECK_EQUAL(acl_ref_count(kernel), refcnt);
  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(kernel));

  cl_context context;
  CHECK_EQUAL(CL_SUCCESS,
              clGetKernelInfo(kernel, CL_KERNEL_CONTEXT, sizeof(context),
                              &context, &size_ret));
  CHECK_EQUAL(sizeof(context), size_ret);
  CHECK_EQUAL(m_context, context);

  cl_program program;
  CHECK_EQUAL(CL_SUCCESS,
              clGetKernelInfo(kernel, CL_KERNEL_PROGRAM, sizeof(program),
                              &program, &size_ret));
  CHECK_EQUAL(sizeof(program), size_ret);
  CHECK_EQUAL(m_program, program);

  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(kernel));

  // This syncThreads() is required because now that we've done a
  // clReleaseKernel() on our kernel object, it has a ref count of 0, and so
  // another thread could end up using the smae kernel object when it calls
  // clCreateKernel(). Therefore, the clReleaseKernel() below may not actually
  // return CL_INVALID_KERNEL, because it's now a pointer to a valid kernel
  // used by the other thread, and moreover, calling clReleaseKernel() on it
  // on our thread screws up the other thread. To avoid this problem, we wait
  // until all threads are finished the test up until this point so that we
  // know no other thread will be calling clCreateKernel() and therefore using
  // this kernel object.
  syncThreads();

  acl_set_allow_invalid_type<cl_kernel>(1);
  CHECK_EQUAL(CL_INVALID_KERNEL, clReleaseKernel(kernel));
  acl_set_allow_invalid_type<cl_kernel>(0);

  CHECK_EQUAL(0, m_program->num_kernels);
}

// Test clGetKernelWorkGroupInfo.
MT_TEST(acl_kernel, get_work_group_info) {
  ACL_LOCKED(acl_print_debug_msg("get wg info\n"));
  cl_int status;
  cl_kernel kernel;

  status = CL_INVALID_VALUE;
  kernel = clCreateKernel(m_program, m_sample_kernel_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Bad kernel.
  acl_set_allow_invalid_type<cl_kernel>(1);
  CHECK_EQUAL(CL_INVALID_KERNEL, clGetKernelWorkGroupInfo(0, 0, 0, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_KERNEL,
              clGetKernelWorkGroupInfo((cl_kernel)m_program, 0, 0, 0, 0, 0));
  acl_set_allow_invalid_type<cl_kernel>(0);

  // Bad device.
  CHECK_EQUAL(
      CL_INVALID_DEVICE,
      clGetKernelWorkGroupInfo(
          kernel, 0, 0, 0, 0,
          0)); // there are more than one device associated with the kernel
  CHECK_EQUAL(CL_INVALID_DEVICE,
              clGetKernelWorkGroupInfo(kernel, (cl_device_id)1, 0, 0, 0, 0));
  CHECK_EQUAL(
      CL_INVALID_DEVICE,
      clGetKernelWorkGroupInfo(kernel, (cl_device_id)&status, 0, 0, 0, 0));
  {
    // Try a valid device that is not associated with the kernel (since
    // it's not in the context.)
    cl_device_id all_devices[MAX_DEVICES];
    cl_uint num_dev;
    CHECK_EQUAL(CL_SUCCESS,
                clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, MAX_DEVICES,
                               &all_devices[0], &num_dev));
    if (num_dev > m_num_devices_in_context) {
      CHECK_EQUAL(
          CL_INVALID_DEVICE,
          clGetKernelWorkGroupInfo(
              kernel, all_devices[m_num_devices_in_context], 0, 0, 0, 0));
    }
  }

  // Bad return args
  size_t wg_size = 0;
  size_t ret_size = 0;
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetKernelWorkGroupInfo(kernel, m_device[0],
                                       CL_KERNEL_WORK_GROUP_SIZE, 1, &wg_size,
                                       0)); // too small
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetKernelWorkGroupInfo(kernel, m_device[0],
                                       CL_KERNEL_WORK_GROUP_SIZE,
                                       sizeof(wg_size), 0, 0));
  CHECK_EQUAL(CL_INVALID_VALUE, clGetKernelWorkGroupInfo(
                                    kernel, m_device[0],
                                    CL_KERNEL_WORK_GROUP_SIZE, 0, &wg_size, 0));

  // Bad param names
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetKernelWorkGroupInfo(kernel, m_device[0], 0, sizeof(wg_size),
                                       &wg_size, 0));

  // Query CL_KERNEL_GLOBAL_WORK_SIZE on a non-custom device and non-built-in
  // kernel.
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetKernelWorkGroupInfo(kernel, m_device[0],
                                       CL_KERNEL_GLOBAL_WORK_SIZE, 0, NULL,
                                       &ret_size));

  // Good param names
  CHECK_EQUAL(CL_SUCCESS, clGetKernelWorkGroupInfo(
                              kernel, m_device[0], CL_KERNEL_WORK_GROUP_SIZE,
                              sizeof(wg_size), &wg_size, &ret_size));
  CHECK_EQUAL(sizeof(wg_size), ret_size);
  CHECK(0 < wg_size);
  CHECK(32768 ==
        wg_size); // Set in the static auto_config data in acl_globals_test

  size_t compile_wg_size[3];
  CHECK_EQUAL(CL_SUCCESS,
              clGetKernelWorkGroupInfo(
                  kernel, m_device[0], CL_KERNEL_COMPILE_WORK_GROUP_SIZE,
                  sizeof(compile_wg_size), &compile_wg_size[0], &ret_size));
  CHECK_EQUAL(sizeof(compile_wg_size), ret_size);
  CHECK_EQUAL(4, compile_wg_size[0]);
  CHECK_EQUAL(2, compile_wg_size[1]);
  CHECK_EQUAL(4, compile_wg_size[2]);

  size_t local_mem_size = 0;
  CHECK_EQUAL(CL_SUCCESS, clGetKernelWorkGroupInfo(kernel, m_device[0],
                                                   CL_KERNEL_LOCAL_MEM_SIZE,
                                                   sizeof(local_mem_size),
                                                   &local_mem_size, &ret_size));
  CHECK_EQUAL(sizeof(local_mem_size), ret_size);

  CHECK_EQUAL(get_static_allocation_for_sample_kernel(), local_mem_size);

  size_t preferred_wg_size_multiple = 0;
  CHECK_EQUAL(CL_SUCCESS, clGetKernelWorkGroupInfo(
                              kernel, m_device[0],
                              CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE,
                              sizeof(preferred_wg_size_multiple),
                              &preferred_wg_size_multiple, &ret_size));
  CHECK_EQUAL(sizeof(preferred_wg_size_multiple), ret_size);
  CHECK_EQUAL(1, preferred_wg_size_multiple);

  clReleaseKernel(kernel);

  CHECK_EQUAL(0, m_program->num_kernels);
}

MT_TEST(acl_kernel, create_kernels_in_program_bad) {
  ACL_LOCKED(acl_print_debug_msg("create kernels in program  bad\n"));
  cl_uint num_kernels_ret;

  CHECK_EQUAL(0, m_program->num_kernels);

  // Bad program
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(1);
  syncThreads();
  struct _cl_program fake_program = {0};
  CHECK_EQUAL(CL_INVALID_PROGRAM, clCreateKernelsInProgram(0, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_PROGRAM,
              clCreateKernelsInProgram(&fake_program, 0, 0, 0));
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(0);
  syncThreads();

  // Unbuilt progogram case
  cl_program unbuilt = this->load_program();
  ACL_LOCKED(CHECK(acl_program_is_valid(unbuilt)));
  CHECK_EQUAL(CL_SUCCESS, clCreateKernelsInProgram(unbuilt, 0, 0, 0));
  this->unload_program(unbuilt);

  // Bad return kernels buffer allocation
  enum { MAX_KERNELS = 10 };
  cl_kernel kernels[MAX_KERNELS];
  CHECK_EQUAL(CL_INVALID_VALUE, clCreateKernelsInProgram(m_program, 0, kernels,
                                                         &num_kernels_ret));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clCreateKernelsInProgram(m_program, 1, 0, &num_kernels_ret));
  // Determine number kernels available
  CHECK_EQUAL(CL_SUCCESS,
              clCreateKernelsInProgram(m_program, 0, 0, &num_kernels_ret));
  CHECK(1 < num_kernels_ret);
  // Result buffer too small
  CHECK_EQUAL(CL_INVALID_VALUE, clCreateKernelsInProgram(m_program, 1, kernels,
                                                         &num_kernels_ret));

  // num_kernels_ret can be NULL
  CHECK_EQUAL(CL_SUCCESS, clCreateKernelsInProgram(m_program, 0, 0, 0));

  CHECK_EQUAL(0, m_program->num_kernels);
}

MT_TEST(acl_kernel, create_kernels_in_program) {
  ACL_LOCKED(acl_print_debug_msg("create kernels in program \n"));

  cl_uint num_kernels_ret;
  enum { MAX_KERNELS = 10 };
  cl_kernel kernels[MAX_KERNELS];

  CHECK_EQUAL(0, m_program->num_kernels);

  CHECK_EQUAL(CL_SUCCESS, clCreateKernelsInProgram(m_program, MAX_KERNELS,
                                                   kernels, &num_kernels_ret));
  // Only two have the same name and interface across all 4 devices in the
  // complex system case.  See acl_global_test.cpp
  for (cl_uint i = 0; i < num_kernels_ret; i++) {
    ACL_LOCKED(acl_print_debug_msg("Got kernel[%p] %s \n", kernels[i],
                                   kernels[i]->accel_def->iface.name.c_str()));
  }
  CHECK_EQUAL(3, num_kernels_ret);

  for (cl_uint i = 0; i < num_kernels_ret; i++) {
    clReleaseKernel(kernels[i]);
  }
  CHECK_EQUAL(0, m_program->num_kernels);
}

MT_TEST(acl_kernel, create_kernel_exhaustion) {
  cl_kernel kernel[3];
  cl_int status;

  ACL_LOCKED(acl_print_debug_msg("create kernel exhaustion\n"));

  syncThreads();

  CHECK_EQUAL(0, m_program->num_kernels);

  // First, verify that we can create 2 different kernels
  status = CL_INVALID_VALUE;
  kernel[0] = clCreateKernel(m_program, m_sample_kernel_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(kernel[0] != 0);
  ACL_LOCKED(CHECK(acl_kernel_is_valid(kernel[0])));
  kernel[1] = clCreateKernel(m_program, m_sample_kernel_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(kernel[1] != 0);
  ACL_LOCKED(CHECK(acl_kernel_is_valid(kernel[1])));
  CHECK(kernel[0] != kernel[1]);
  CHECK_EQUAL(2, m_program->num_kernels);

  // Now disable allocations to cause an out-of-memory failure
  acl_set_malloc_enable(0);
  kernel[2] = clCreateKernel(m_program, m_sample_kernel_name, &status);
  CHECK_EQUAL(CL_OUT_OF_HOST_MEMORY, status);
  CHECK_EQUAL(0, kernel[2]);

  // Now turn back on allocations and free the existing kernels
  acl_set_malloc_enable(1);
  status = clReleaseKernel(kernel[0]);
  CHECK_EQUAL(CL_SUCCESS, status);
  status = clReleaseKernel(kernel[1]);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(0, m_program->num_kernels);
}

MT_TEST(acl_kernel, create_kernels_in_program_exhaustion) {
  // Assume that there are no more than 10 unique kernels in the program
  const cl_uint max_kernel_batch = 10;
  cl_kernel kernel[2][max_kernel_batch];
  cl_uint num_kernels[2];
  cl_int status;
  unsigned int i;

  ACL_LOCKED(acl_print_debug_msg("kernels in prog exhaustion\n"));

  syncThreads();

  CHECK_EQUAL(0, m_program->num_kernels);

  // First, verify that we can create a set of kernels
  status = CL_INVALID_VALUE;
  status = clCreateKernelsInProgram(m_program, max_kernel_batch,
                                    &(kernel[0][0]), &(num_kernels[0]));
  CHECK_EQUAL(CL_SUCCESS, status);
  // Assume there is more than one kernel in the program
  CHECK(num_kernels[0] > 1);
  for (i = 0; i < num_kernels[0]; ++i) {
    CHECK(kernel[0][i] != 0);
    ACL_LOCKED(CHECK(acl_kernel_is_valid(kernel[0][i])));
  }
  CHECK_EQUAL(m_program->num_kernels, num_kernels[0]);

  // Now disable allocations to cause an out-of-memory failure
  acl_set_malloc_enable(0);
  status = clCreateKernelsInProgram(m_program, max_kernel_batch,
                                    &(kernel[1][0]), &(num_kernels[1]));
  CHECK_EQUAL(CL_OUT_OF_HOST_MEMORY, status);

  // Now turn back on allocations and free the existing kernels
  acl_set_malloc_enable(1);
  for (i = 0; i < num_kernels[0]; ++i) {
    status = clReleaseKernel(kernel[0][i]);
    CHECK_EQUAL(CL_SUCCESS, status);
  }
  CHECK_EQUAL(0, m_program->num_kernels);
}

MT_TEST(acl_kernel, set_kernel_arg) {
  ACL_LOCKED(acl_print_debug_msg("set_kernel_arg\n"));
  cl_int status;
  cl_kernel kernel;

  CHECK_EQUAL(0, m_program->num_kernels);

  // Set up mem args.
  cl_mem src_mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE, 64, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(src_mem != 0);
  cl_mem dest_mem =
      clCreateBuffer(m_context, CL_MEM_READ_WRITE, 64, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(dest_mem != 0);
  CHECK(src_mem != dest_mem);
  cl_mem pipe =
      clCreatePipe(m_context, CL_MEM_READ_WRITE, 5, 10, NULL, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(pipe != 0);
  cl_image_format image_format = {CL_R, CL_FLOAT};
  cl_image_desc image_desc = {
      CL_MEM_OBJECT_IMAGE3D, 5, 6, 7, 0, 0, 0, 0, 0, {NULL}};
  cl_mem image = clCreateImage(m_context, CL_MEM_READ_WRITE, &image_format,
                               &image_desc, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(image != 0);

  status = CL_INVALID_VALUE;
  kernel = clCreateKernel(m_program, m_sample_kernel_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Bad kernel
  acl_set_allow_invalid_type<cl_kernel>(1);
  CHECK_EQUAL(CL_INVALID_KERNEL, clSetKernelArg(0, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_KERNEL, clSetKernelArg((cl_kernel)m_program, 0, 0, 0));
  acl_set_allow_invalid_type<cl_kernel>(0);

  // Invalid index
  CHECK_EQUAL(CL_INVALID_ARG_INDEX, clSetKernelArg(kernel, 1000, 0, 0));

  // Bad by-value arg.
  cl_uint int_arg = 12;
  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArg(kernel, 1, sizeof(cl_uint), 0 /*bad*/));
  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArg(kernel, 3, sizeof(cl_uint),
                             &status /*bad*/)); // local, must have 0 arg value

  // Bad sizes
  // Arg 3 is __local
  // Arg 0 is __global
  // Arg 4 is __constant
  CHECK_EQUAL(CL_INVALID_ARG_SIZE,
              clSetKernelArg(kernel, 3, 0, 0)); // local, must have size > 0
  CHECK_EQUAL(
      CL_INVALID_ARG_SIZE,
      clSetKernelArg(kernel, 0, 0, &src_mem)); // bad size for __global cl_mem
  CHECK_EQUAL(CL_INVALID_ARG_SIZE,
              clSetKernelArg(kernel, 0, 1 + sizeof(cl_mem),
                             &src_mem)); // bad size for __global cl_mem
  CHECK_EQUAL(
      CL_INVALID_ARG_SIZE,
      clSetKernelArg(kernel, 4, 0, &src_mem)); // bad size for __constant cl_mem
  CHECK_EQUAL(CL_INVALID_ARG_SIZE,
              clSetKernelArg(kernel, 4, 1 + sizeof(cl_mem),
                             &src_mem)); // bad size for __constant cl_mem

  CHECK_EQUAL(CL_INVALID_ARG_SIZE,
              clSetKernelArg(kernel, 1, 0, &int_arg)); // bad size
  CHECK_EQUAL(CL_INVALID_ARG_SIZE,
              clSetKernelArg(kernel, 1, 1, &int_arg)); // bad size
  CHECK_EQUAL(
      CL_INVALID_ARG_SIZE,
      clSetKernelArg(kernel, 1, 1 + sizeof(int_arg), &int_arg)); // bad size

  // We now support kernel args that are host-accessible only.
  cl_mem host_mem = clCreateBuffer(
      m_context, CL_MEM_ALLOC_HOST_PTR | CL_MEM_READ_WRITE, 64, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &host_mem));

  ACL_LOCKED(CHECK_EQUAL(1, acl_num_non_null_mem_args(kernel)));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), 0));
  ACL_LOCKED(CHECK_EQUAL(0, acl_num_non_null_mem_args(kernel)));
  clReleaseMemObject(host_mem);

  // We now support kernel args that are host-accessible only.
  cl_mem host_use_mem =
      clCreateBuffer(m_context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, 64,
                     (void *)1024, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArg(kernel, 0, sizeof(cl_mem), &host_use_mem));

  ACL_LOCKED(CHECK_EQUAL(1, acl_num_non_null_mem_args(kernel)));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), 0));
  ACL_LOCKED(CHECK_EQUAL(0, acl_num_non_null_mem_args(kernel)));
  clReleaseMemObject(host_use_mem);

  // Check local arg.
  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArg(kernel, 3, sizeof(cl_mem),
                             &src_mem)); // Local arg value must be null

  // Valid args
  ACL_LOCKED(CHECK_EQUAL(0, acl_num_non_null_mem_args(kernel)));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem),
                                         0)); // can pass NULL arg
  cl_mem null_mem = 0;
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArg(kernel, 0, sizeof(cl_mem),
                             &null_mem)); // can pass pointer to NULL buffer
  ACL_LOCKED(CHECK_EQUAL(
      0, acl_num_non_null_mem_args(kernel))); // check NULL counting case.
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem),
                                         &src_mem)); // can pass non-null ARG

  ACL_LOCKED(CHECK_EQUAL(
      1, acl_num_non_null_mem_args(kernel))); // check non-NULL countin case.
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 1, sizeof(int_arg), &int_arg));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 2, sizeof(cl_mem),
                                         &dest_mem)); // can pass non-null ARG
  size_t local_size = 92;
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 3, local_size, 0));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 4, sizeof(cl_mem),
                                         0)); // can pass NULL to __constant arg
  CHECK_EQUAL(
      CL_SUCCESS,
      clSetKernelArg(kernel, 4, sizeof(cl_mem),
                     &null_mem)); // can pass pointer to NULL to __constant arg
  CHECK_EQUAL(
      CL_SUCCESS,
      clSetKernelArg(kernel, 4, sizeof(cl_mem),
                     &src_mem)); // can pass mem object to __constant arg

  // Invalid null arg.
  CHECK_EQUAL(
      CL_INVALID_ARG_VALUE,
      clSetKernelArg(kernel, 1, sizeof(cl_mem),
                     nullptr)); // cannot pass NULL ARG as a non-memory type

  // Check that we can set a pipe argument
  // At the moment we don't do anything with the pipe argument, so we
  // just want to make sure that if we create a pipe and pass it to a
  // cl_mem argument that the runtime doesn't die
  // Set what happens when a pipe object gets assigned to both a memory type and
  // non memory type
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArg(kernel, 1, sizeof(cl_mem),
                             &pipe)); // can pass pipe ARG as a non-memory type
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArg(kernel, 2, sizeof(cl_mem),
                             &pipe)); // can pass pipe ARG as a memory type

  // Check that we can set an image argument
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 2, sizeof(cl_mem),
                                         &image)); // can pass pipe ARG

  clReleaseMemObject(src_mem);
  clReleaseMemObject(dest_mem);
  clReleaseMemObject(pipe);
  clReleaseMemObject(image);
  clReleaseKernel(kernel);

  CHECK_EQUAL(0, m_program->num_kernels);
}

MT_TEST(acl_kernel, local_mem_size_kernel_arg) {
  ACL_LOCKED(acl_print_debug_msg("local_mem_size_kernel_arg\n"));
  cl_int status;
  cl_kernel kernel;

  CHECK_EQUAL(0, m_program->num_kernels);

  status = CL_INVALID_VALUE;
  kernel = clCreateKernel(m_program, m_sample_kernel_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Test interaction between clSetKernelArg for a __local arg, and the
  // reported workgroup local mem size.
  size_t ret_size = 0;
  size_t base_local_mem_size = 0;
  CHECK_EQUAL(CL_SUCCESS, clGetKernelWorkGroupInfo(
                              kernel, m_device[0], CL_KERNEL_LOCAL_MEM_SIZE,
                              sizeof(base_local_mem_size), &base_local_mem_size,
                              &ret_size));
  CHECK_EQUAL(sizeof(base_local_mem_size), ret_size);

  CHECK_EQUAL(get_static_allocation_for_sample_kernel(), base_local_mem_size);

  size_t sample_size[] = {92, 117, 220};
  for (size_t i = 0; i < sizeof(sample_size) / sizeof(sample_size[0]); i++) {
    // Arg 3 is __local
    CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 3, sample_size[i], 0));

    // The workgroup reported local mem size must reflect the local mem arg
    // settings.
    size_t adjusted_local_mem_size = 0;
    CHECK_EQUAL(CL_SUCCESS, clGetKernelWorkGroupInfo(
                                kernel, m_device[0], CL_KERNEL_LOCAL_MEM_SIZE,
                                sizeof(adjusted_local_mem_size),
                                &adjusted_local_mem_size, &ret_size));
    CHECK_EQUAL(sizeof(adjusted_local_mem_size), ret_size);
    CHECK_EQUAL(base_local_mem_size + adjust_for_alignment(sample_size[i]),
                adjusted_local_mem_size);
  }

  clReleaseKernel(kernel);
  CHECK_EQUAL(0, m_program->num_kernels);
}

MT_TEST(acl_kernel, link_enqueue) {
  ACL_LOCKED(acl_print_debug_msg("link_enqueue\n"));
  clEnqueueTask(0, 0, 0, 0, 0);
  clEnqueueNDRangeKernel(0, 0, 0, 0, 0, 0, 0, 0, 0);
  clEnqueueNativeKernel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

MT_TEST(acl_kernel, enqueue_native) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  ACL_LOCKED(acl_print_debug_msg("enqueue native\n"));

  // Bad queue
  struct _cl_command_queue fake_cq = {0};
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
              clEnqueueNativeKernel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
              clEnqueueNativeKernel(&fake_cq, 0, 0, 0, 0, 0, 0, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
              clEnqueueNativeKernel(&fake_cq, 0, 0, 0, 0, 0, 0, 0, 0, 0));

  // Bad kernel. We don't support native kernels at all.
  CHECK_EQUAL(CL_INVALID_OPERATION,
              clEnqueueNativeKernel(m_cq, 0, 0, 0, 0, 0, 0, 0, 0, 0));
}

TEST(acl_kernel, enqueue_ndrange) {
  acl_set_allow_invalid_type<cl_command_queue>(1);
  ACL_LOCKED(acl_print_debug_msg("enqueue ndrange\n"));
  cl_int status;

  CHECK_EQUAL(0, m_program->num_kernels);

  // Bad queue
  struct _cl_command_queue fake_cq = {0};
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
              clEnqueueNDRangeKernel(0, 0, 0, 0, 0, 0, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
              clEnqueueNDRangeKernel(&fake_cq, 0, 0, 0, 0, 0, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE,
              clEnqueueNDRangeKernel(&fake_cq, 0, 0, 0, 0, 0, 0, 0, 0));

  // Bad kernel
  acl_set_allow_invalid_type<cl_kernel>(1);
  CHECK_EQUAL(CL_INVALID_KERNEL,
              clEnqueueNDRangeKernel(m_cq, 0, 0, 0, 0, 0, 0, 0, 0));
  CHECK_EQUAL(
      CL_INVALID_KERNEL,
      clEnqueueNDRangeKernel(m_cq, (cl_kernel)m_program, 0, 0, 0, 0, 0, 0, 0));
  acl_set_allow_invalid_type<cl_kernel>(0);

  status = CL_INVALID_VALUE;
  cl_kernel kernel = clCreateKernel(m_program, m_sample_kernel_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // BEGIN **** Kernel args not set
  CHECK_EQUAL(CL_INVALID_KERNEL_ARGS,
              clEnqueueNDRangeKernel(m_cq, kernel, 0, 0, 0, 0, 0, 0, 0));

  // Set up kernel args
  size_t mem_size = 64;
  cl_mem src_mem =
      clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  cl_mem dest_mem =
      clCreateBuffer(m_context, CL_MEM_WRITE_ONLY, mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  cl_mem const_mem =
      clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(src_mem != dest_mem);
  CHECK(src_mem != const_mem);
  CHECK(dest_mem != const_mem);

  // Now set the arguments.
  // Arg 0 is __global
  // Arg 1 is cl_uint value
  // Arg 2 is __global
  // Arg 3 is __local
  // Arg 4 is __constant
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));
  CHECK_EQUAL(CL_INVALID_KERNEL_ARGS,
              clEnqueueNDRangeKernel(m_cq, kernel, 0, 0, 0, 0, 0, 0, 0));
  cl_uint int_arg = 42;
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 1, sizeof(int_arg), &int_arg));
  CHECK_EQUAL(CL_INVALID_KERNEL_ARGS,
              clEnqueueNDRangeKernel(m_cq, kernel, 0, 0, 0, 0, 0, 0, 0));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 2, sizeof(cl_mem), &dest_mem));
  CHECK_EQUAL(CL_INVALID_KERNEL_ARGS,
              clEnqueueNDRangeKernel(m_cq, kernel, 0, 0, 0, 0, 0, 0, 0));
  size_t local_size = 92;
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 3, local_size, 0));
  CHECK_EQUAL(CL_INVALID_KERNEL_ARGS,
              clEnqueueNDRangeKernel(m_cq, kernel, 0, 0, 0, 0, 0, 0, 0));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArg(kernel, 4, sizeof(cl_mem), &const_mem));
  // END **** Kernel args not set

  // BEGIN *** CL_INVALID_PROGRAM_EXECUTABLE cases.
  // Hard to test "unbuilt program for the wrong device" case because you
  // can't create a kernel for an unbuilt program.
  cl_program program1 = this->load_program(1, 1); // compile for device 1 only
  // Test a built program for the wrong device.
  this->build(program1);
  cl_kernel kernel1 = clCreateKernel(program1, m_sample_kernel_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK_EQUAL(0, acl_num_non_null_mem_args(kernel1)));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel1, 0, sizeof(cl_mem), &src_mem));
  ACL_LOCKED(CHECK_EQUAL(1, acl_num_non_null_mem_args(kernel1)));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArg(kernel1, 1, sizeof(int_arg), &int_arg));
  ACL_LOCKED(CHECK_EQUAL(1, acl_num_non_null_mem_args(kernel1)));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArg(kernel1, 2, sizeof(cl_mem), &dest_mem));
  ACL_LOCKED(CHECK_EQUAL(2, acl_num_non_null_mem_args(kernel1)));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel1, 3, local_size, 0));
  ACL_LOCKED(CHECK_EQUAL(2, acl_num_non_null_mem_args(kernel1)));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArg(kernel1, 4, sizeof(cl_mem), &const_mem));
  ACL_LOCKED(CHECK_EQUAL(3, acl_num_non_null_mem_args(kernel1)));
  CHECK_EQUAL(CL_INVALID_PROGRAM_EXECUTABLE,
              clEnqueueNDRangeKernel(m_cq, kernel1, 0, 0, 0, 0, 0, 0, 0));
  clReleaseKernel(kernel1);
  this->unload_program(program1);

  // Test an unbuilt program for the right device.
  cl_program program2 = this->load_program();
  this->build(program2);
  // Now I can create the kernel
  cl_kernel kernel2 = clCreateKernel(program2, m_sample_kernel_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel2, 0, sizeof(cl_mem), &src_mem));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArg(kernel2, 1, sizeof(int_arg), &int_arg));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArg(kernel2, 2, sizeof(cl_mem), &dest_mem));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel2, 3, local_size, 0));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArg(kernel2, 4, sizeof(cl_mem), &const_mem));
  // Need a back door to easily test the case where we have the kernel in
  // hand, but the program is not built.
  ACL_LOCKED(acl_program_invalidate_builds(program2));
  CHECK_EQUAL(CL_INVALID_PROGRAM_EXECUTABLE,
              clEnqueueNDRangeKernel(m_cq, kernel2, 0, 0, 0, 0, 0, 0, 0));
  clReleaseKernel(kernel2);
  CHECK_EQUAL(0, program2->num_kernels);
  this->unload_program(program2);

  // END *** CL_INVALID_PROGRAM_EXECUTABLE cases.

  CHECK_EQUAL(CL_INVALID_WORK_DIMENSION,
              clEnqueueNDRangeKernel(m_cq, kernel, 0, 0, 0, 0, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_WORK_DIMENSION,
              clEnqueueNDRangeKernel(m_cq, kernel, 4, 0, 0, 0, 0, 0, 0));

  const cl_uint work_dim = 3;
  {
    // Check for overflow with global_work_offset.
    size_t global_offset[] = {std::numeric_limits<std::size_t>::max(), 1, 1};
    size_t global_size[] = {1, 1, 1};
    CHECK_EQUAL(CL_INVALID_GLOBAL_OFFSET,
                clEnqueueNDRangeKernel(m_cq, kernel, work_dim, global_offset,
                                       global_size, 0, 0, 0, 0));
  }

  // Check global sizes.
  size_t global_size[work_dim] = {192, 192, 64}; // 192 = 3 * 64
  if (sizeof(size_t) > 4) {
    // When host is on 32-bit platform, the host lib can't check, and so
    // it does not return the errors we expect below...
    size_t bigbig = ((cl_ulong)1 << 16);
    bigbig = bigbig << 16; // make a big constant, and avoid GCC warning
    ACL_LOCKED(acl_print_debug_msg(" bigbig is %zu\n", bigbig));
    size_t bad_global_size[work_dim] = {192, 192, 64}; // 192 = 3 * 64
    bad_global_size[0] = bigbig;
    CHECK_EQUAL(CL_OUT_OF_RESOURCES,
                clEnqueueNDRangeKernel(m_cq, kernel, 3, 0, bad_global_size, 0,
                                       0, 0, 0));
    bad_global_size[0] = global_size[0];
    bad_global_size[1] = bigbig;
    CHECK_EQUAL(CL_OUT_OF_RESOURCES,
                clEnqueueNDRangeKernel(m_cq, kernel, 3, 0, bad_global_size, 0,
                                       0, 0, 0));
    bad_global_size[1] = global_size[0];
    bad_global_size[2] = bigbig;
    CHECK_EQUAL(CL_OUT_OF_RESOURCES,
                clEnqueueNDRangeKernel(m_cq, kernel, 3, 0, bad_global_size, 0,
                                       0, 0, 0));
  }

  // Check NULL global work size
  CHECK_EQUAL(CL_INVALID_GLOBAL_WORK_SIZE,
              clEnqueueNDRangeKernel(m_cq, kernel, 3, 0, 0, 0, 0, 0, 0));

  {
    // Check zeroes in global work size
    size_t bad_global_size[work_dim] = {192, 192, 64}; // 192 = 3 * 64
    bad_global_size[0] = 0;
    CHECK_EQUAL(CL_INVALID_GLOBAL_WORK_SIZE,
                clEnqueueNDRangeKernel(m_cq, kernel, 3, 0, bad_global_size, 0,
                                       0, 0, 0));
  }

  // Case: local work specified, not match spec in kernel source
  // Case: local work specified, not divide evenly into global size
  // Case: local work unspecified, kernel-spec'd work group size not divide
  // global size evenly

  {
    // Case: local work specified, but has zero value for some of its indices
    // Check zeroes in global work size
    size_t bad_local_size[work_dim] = {192, 192, 64}; // 192 = 3 * 64
    bad_local_size[0] = 0;
    CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
                clEnqueueNDRangeKernel(m_cq, kernel, 3, 0, global_size,
                                       bad_local_size, 0, 0, 0));
  }

  // Case: local work specified, but some dim exceeds
  // CL_DEVICE_MAX_WORK_ITEM_SIZES Case: local work specified, but total work
  // items exceeds CL_DEVICE_MAX_WORK_GROUP_SIZE

  // This time it should launch.
  cl_event event;
  CHECK_EQUAL(1, acl_ref_count(kernel));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueNDRangeKernel(m_cq, kernel, 1, 0,
                                                 global_size, 0, 0, 0, &event));
  CHECK_EQUAL(2, acl_ref_count(kernel));
  // Check some internal data.
  CHECK_EQUAL(CL_COMMAND_NDRANGE_KERNEL, event->cmd.type);
  CHECK_EQUAL(kernel, event->cmd.info.ndrange_kernel.kernel);

  acl_dev_kernel_invocation_image_t *invocation =
      event->cmd.info.ndrange_kernel.invocation_wrapper->image;
  CHECK_EQUAL(1, invocation->work_dim);
  CHECK_EQUAL(global_size[0] / kernel->accel_def->num_vector_lanes,
              invocation->global_work_size[0]); // global_size is divided by
                                                // num_vector_lanes value
  CHECK_EQUAL(
      4 / kernel->accel_def->num_vector_lanes,
      invocation
          ->local_work_size[0]); // This kernel has required work group size 4,
                                 // but num_vector_lanes is also 4.
  CHECK_EQUAL(invocation->num_groups[0] * invocation->local_work_size[0],
              invocation->global_work_size[0]);
  CHECK_EQUAL(m_sample_kernel_accel_id, invocation->accel_id);

  {
    // This kernel has 5 arguments:
    //       global*, cl_uint, global*, local*, constant*
    const unsigned int arg_bytes = 3 * m_dev_global_ptr_size_in_cra +
                                   m_dev_local_ptr_size_in_cra +
                                   (unsigned)sizeof(cl_uint);
    const unsigned int fixed_config_regs_bytes =
        (unsigned int)((char *)(&(invocation->arg_value)) -
                       (char *)(&(invocation->work_dim)));
    CHECK_EQUAL(fixed_config_regs_bytes,
                4 * (1   // work_dim
                     + 1 // workgroup size
                     + 3 // global sizes
                     + 3 // num groups
                     + 3 // local sizes
                     + 1 // padding
                     ) +
                    8 * (3 // global offsets
                         ));
    CHECK_EQUAL(arg_bytes, invocation->arg_value_size);
  }

  // Unused dimensions should have values set to 1.
  CHECK_EQUAL(1, invocation->global_work_size[1]);
  CHECK_EQUAL(1, invocation->global_work_size[2]);
  CHECK_EQUAL(1, invocation->num_groups[1]);
  CHECK_EQUAL(1, invocation->num_groups[2]);
  CHECK_EQUAL(1, invocation->local_work_size[1]);
  CHECK_EQUAL(1, invocation->local_work_size[2]);

  // enqueued with NULL global_work_offset so these should all be 0.
  CHECK_EQUAL(0, invocation->global_work_offset[0]);
  CHECK_EQUAL(0, invocation->global_work_offset[1]);
  CHECK_EQUAL(0, invocation->global_work_offset[2]);

  // Check profiling results.
  cl_ulong times[4];
  this->load_times(event, times);
  CHECK(0 < times[0]); // has been queued
  CHECK(
      times[0] <
      times[1]); // has been submitted, becase we were not waiting on anything.
  CHECK_EQUAL(0, times[2]); // kernel not running yet, as not signal recieved
  CHECK_EQUAL(0, times[3]); // not yet complete

  // But the active operation is kernel launch, and it's only in submitted mode.
  acl_device_op_t *active_op;
  active_op = event->current_device_op;
  CHECK(active_op);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, active_op->info.type);
  CHECK_EQUAL(CL_SUBMITTED, active_op->status);
  // not complete yet.

  // The commit to the devcie op queue should have set the activation_id.
  CHECK_EQUAL(active_op->id, invocation->activation_id);

  ACL_LOCKED(acl_receive_kernel_update(active_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));
  this->load_times(event, times);
  CHECK(times[1] < times[2]);

  active_op = event->current_device_op;
  CHECK(active_op);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, active_op->info.type);
  CHECK_EQUAL(CL_RUNNING, active_op->status);
  // not complete yet.
  CHECK_EQUAL(0, times[3]);

  // The reference count on the kernel is still 2 while the operation is in
  // progress.
  CHECK_EQUAL(2, acl_ref_count(kernel));

  ACL_LOCKED(acl_receive_kernel_update(active_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  this->load_times(event, times);
  CHECK(times[2] < times[3]);

  // And now we have a single reference count.
  CHECK_EQUAL(1, acl_ref_count(kernel));

  CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(1, &event));

  // This test case has kernel attribute with specified work group size
  // over 3 dims.
  // So the task launch is not correct.
  CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
              clEnqueueTask(m_cq, kernel, 0, 0, &event));

  // Check valid memory/pointer types
  // Pass in SVM pointer to device that only supports physical memory
  acl_test_hal_set_svm_memory_support(0);
  int *int_ptr =
      (int *)clSVMAllocIntelFPGA(m_context, CL_MEM_READ_WRITE, sizeof(int), 0);
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgSVMPointerIntelFPGA(kernel, 0, int_ptr));
  CHECK_EQUAL(CL_INVALID_OPERATION, clEnqueueTask(m_cq, kernel, 0, 0, &event));
  clSVMFreeIntelFPGA(m_context, int_ptr);

  // Pass in buffer to device that only supports SVM
  acl_test_hal_set_svm_memory_support((int)(CL_DEVICE_SVM_COARSE_GRAIN_BUFFER |
                                            CL_DEVICE_SVM_FINE_GRAIN_BUFFER |
                                            CL_DEVICE_SVM_FINE_GRAIN_SYSTEM));
  acl_test_hal_set_physical_memory_support(false);
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));
  CHECK_EQUAL(CL_INVALID_OPERATION, clEnqueueTask(m_cq, kernel, 0, 0, &event));

  // Revert to set device to use all memory types
  acl_test_hal_set_svm_memory_support((int)(CL_DEVICE_SVM_COARSE_GRAIN_BUFFER |
                                            CL_DEVICE_SVM_FINE_GRAIN_BUFFER |
                                            CL_DEVICE_SVM_FINE_GRAIN_SYSTEM));
  acl_test_hal_set_physical_memory_support(true);

  clReleaseMemObject(src_mem);
  clReleaseMemObject(dest_mem);
  clReleaseMemObject(const_mem);
  clReleaseKernel(kernel);
  clReleaseEvent(event);

  CHECK_EQUAL(0, m_program->num_kernels);
}

MT_TEST(acl_kernel, test_local_work_group_size_debug_msg) {
  ACL_LOCKED(acl_print_debug_msg("test local wg size debug msg\n"));

  // Create buffer for debug msg
  std::stringstream msg_buf;
  std::streambuf *cout_ptr = std::cout.rdbuf(msg_buf.rdbuf());

  cl_int status;
  size_t mem_size = 64;
  const unsigned int max_msg_size = 150;

  // Create a verbose program
  cl_program program_verbose = this->load_program(m_context_verbose);
  this->build(program_verbose);

  // Create mem objects with verbose context
  cl_mem src_mem =
      clCreateBuffer(m_context_verbose, CL_MEM_READ_ONLY, mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  cl_mem dest_mem = clCreateBuffer(m_context_verbose, CL_MEM_WRITE_ONLY,
                                   mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  cl_mem const_mem =
      clCreateBuffer(m_context_verbose, CL_MEM_READ_ONLY, mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(src_mem != dest_mem);
  CHECK(src_mem != const_mem);
  CHECK(dest_mem != const_mem);

  // Case: local work group size does not match reqd_work_group_size kernel
  // attribute
  {
    // Need to use kernel with attribute reqd_work_group_size set: kernel 0
    // Create kernel
    status = CL_INVALID_VALUE;
    cl_kernel kernel0 =
        clCreateKernel(program_verbose, m_sample_kernel_name, &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    // Set kernel arguments.
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel0, 0, sizeof(cl_mem), &src_mem));
    cl_uint int_arg = 42;
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel0, 1, sizeof(int_arg), &int_arg));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel0, 2, sizeof(cl_mem), &dest_mem));
    size_t local_size = 92;
    CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel0, 3, local_size, 0));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel0, 4, sizeof(cl_mem), &const_mem));

    size_t reqd_work_group_size =
        kernel0->accel_def->compile_work_group_size[0];

    const cl_uint work_dim = 3;
    size_t global_size[work_dim] = {192, 192, 64};
    size_t unmatched_local_size[work_dim] = {192, 192, 64};

    char err_msg[max_msg_size];
    snprintf(err_msg, sizeof(err_msg),
             "Specified work group size %zu does not match "
             "reqd_work_group_size kernel attribute %zu.",
             unmatched_local_size[0], reqd_work_group_size);

    CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
                clEnqueueNDRangeKernel(m_cq_verbose, kernel0, work_dim, 0,
                                       global_size, unmatched_local_size, 0, 0,
                                       0));
    CHECK_EQUAL(0, strcmp(err_msg, msg_buf.str().c_str()));

    msg_buf.str(""); // Flush msg
    clReleaseKernel(kernel0);
  }

  // Case: Invalid local work size for kernel with max_global_work_dim attribute
  // set to 0.
  //       All elements of local_work_size must be 1.
  {
    // Need to use kernel with attribute max_global_work_dim set to 0: kernel 7
    status = CL_INVALID_VALUE;
    cl_kernel kernel7 = clCreateKernel(program_verbose,
                                       m_sample_kernel_mgwd_zero_name, &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel7, 0, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel7, 1, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel7, 2, sizeof(cl_mem), &src_mem));

    const cl_uint work_dim = 1;
    size_t global_size[3] = {1, 192, 64};
    size_t local_size[3] = {192, 192, 64};

    const char *err_msg =
        "Invalid local work size for kernel with max_global_work_dim attribute "
        "set to 0. All elements of local_work_size must be 1.";

    CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
                clEnqueueNDRangeKernel(m_cq_verbose, kernel7, work_dim, 0,
                                       global_size, local_size, 0, 0, 0));
    CHECK_EQUAL(0, strcmp(err_msg, msg_buf.str().c_str()));

    msg_buf.str("");
    clReleaseKernel(kernel7);
  }

  // Case: Invalid local work size for kernel with max_global_work_dim attribute
  // set to <max_dim>.
  //       local_work_size[dim] must be 1.
  {
    // Need to use kernel with attribute max_global_work_dim set to 1 or 2:
    // kernel 6
    status = CL_INVALID_VALUE;
    cl_kernel kernel6 =
        clCreateKernel(program_verbose, m_sample_kernel_mgwd_two_name, &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel6, 0, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel6, 1, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel6, 2, sizeof(cl_mem), &src_mem));

    const cl_uint work_dim = 3;
    size_t global_size[work_dim] = {192, 192, 1};
    size_t local_size[work_dim] = {192, 192, 64};

    const char *err_msg =
        "Invalid local work size for kernel with max_global_work_dim attribute "
        "set to 2. local_work_size[2] must be 1.";

    CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
                clEnqueueNDRangeKernel(m_cq_verbose, kernel6, work_dim, 0,
                                       global_size, local_size, 0, 0, 0));
    CHECK_EQUAL(0, strcmp(err_msg, msg_buf.str().c_str()))

    msg_buf.str("");
    clReleaseKernel(kernel6);
  }

  // Case: Work group size is zero in one or more dimensions
  // Case: Work group size exceeds kernel-specific limit
  // Case: Total work group size exceeds device limit specified in
  // CL_DEVICE_MAX_WORK_GROUP_SIZE Case: Total work group size exceeds
  // kernel-specific limit Case: Work group size does not divide evenly into
  // global work size
  {
    char err_msg[max_msg_size];

    status = CL_INVALID_VALUE;
    cl_kernel kernel4 =
        clCreateKernel(program_verbose, "kernel4_task_double", &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel4, 0, sizeof(cl_mem), &src_mem));

    size_t kernel_work_group_limit = kernel4->accel_def->max_work_group_size;

    const cl_uint work_dim = 3;
    size_t global_size[work_dim] = {192, 192, 64};
    size_t local_size[work_dim] = {0, 1, 1};

    snprintf(err_msg, sizeof(err_msg),
             "Work group size is zero in one or more dimensions");

    CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
                clEnqueueNDRangeKernel(m_cq_verbose, kernel4, work_dim, 0,
                                       global_size, local_size, 0, 0, 0));
    CHECK_EQUAL(0, strcmp(err_msg, msg_buf.str().c_str()));

    msg_buf.str("");

    local_size[0] = kernel_work_group_limit + 1;
    global_size[0] = local_size[0];

    snprintf(err_msg, sizeof(err_msg),
             "Work group size %zu exceeds kernel-specific limit %zu.",
             local_size[0], kernel_work_group_limit);

    CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
                clEnqueueNDRangeKernel(m_cq_verbose, kernel4, work_dim, 0,
                                       global_size, local_size, 0, 0, 0));
    CHECK_EQUAL(0, strcmp(err_msg, msg_buf.str().c_str()));

    msg_buf.str("");

    local_size[0] = 128 + 1;
    global_size[0] = local_size[0];
    local_size[1] = 128;
    global_size[1] = local_size[1];
    local_size[2] = 2;
    global_size[2] = local_size[2];
    size_t total_work_size = local_size[0] * local_size[1] * local_size[2];

    snprintf(err_msg, sizeof(err_msg),
             "Total work group size %zu exceeds kernel-specific limit %zu.",
             total_work_size, kernel_work_group_limit);

    CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
                clEnqueueNDRangeKernel(m_cq_verbose, kernel4, work_dim, 0,
                                       global_size, local_size, 0, 0, 0));
    CHECK_EQUAL(0, strcmp(err_msg, msg_buf.str().c_str()));

    msg_buf.str("");

    size_t device_max_work_size;
    clGetDeviceInfo(m_device[0], CL_DEVICE_MAX_WORK_GROUP_SIZE,
                    sizeof(device_max_work_size), &device_max_work_size, 0);

    local_size[0] = 16384;
    global_size[0] = local_size[0];
    local_size[1] = 16384;
    global_size[1] = local_size[1];
    local_size[2] = 8;
    global_size[2] = local_size[2];
    total_work_size = local_size[0] * local_size[1] * local_size[2];

    snprintf(err_msg, sizeof(err_msg),
             "Total work group size %zu exceeds device limit specified in "
             "CL_DEVICE_MAX_WORK_GROUP_SIZE %zu.",
             total_work_size, device_max_work_size);

    CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
                clEnqueueNDRangeKernel(m_cq_verbose, kernel4, work_dim, 0,
                                       global_size, local_size, 0, 0, 0));
    CHECK_EQUAL(0, strcmp(err_msg, msg_buf.str().c_str()));

    msg_buf.str("");

    local_size[0] = 127;
    global_size[0] = 128;
    local_size[1] = 128;
    global_size[1] = local_size[1];
    local_size[2] = 2;
    global_size[2] = local_size[2];

    snprintf(
        err_msg, sizeof(err_msg),
        "Work group size %zu does not divide evenly into global work size %zu.",
        local_size[0], global_size[0]);

    CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
                clEnqueueNDRangeKernel(m_cq_verbose, kernel4, work_dim, 0,
                                       global_size, local_size, 0, 0, 0));
    CHECK_EQUAL(0, strcmp(err_msg, msg_buf.str().c_str()));

    msg_buf.str("");
    clReleaseKernel(kernel4);
  }

  // Case: Kernel vector lane parameter does not evenly divide into work group
  // size in dimension 0
  {
    status = CL_INVALID_VALUE;
    cl_kernel kernel13 =
        clCreateKernel(program_verbose, "kernel13_multi_vec_lane", &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel13, 0, sizeof(cl_mem), &src_mem));

    const cl_uint work_dim = 3;
    size_t global_size[work_dim] = {192, 192, 64};
    size_t local_size[work_dim] = {64, 64, 8};

    const char *err_msg = "Kernel vector lane parameter does not evenly divide "
                          "into work group size in dimension 0";

    CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
                clEnqueueNDRangeKernel(m_cq_verbose, kernel13, work_dim, 0,
                                       global_size, local_size, 0, 0, 0));
    CHECK_EQUAL(0, strcmp(err_msg, msg_buf.str().c_str()));

    msg_buf.str("");
    clReleaseKernel(kernel13);
  }

  // clean up
  clReleaseMemObject(src_mem);
  clReleaseMemObject(dest_mem);
  clReleaseMemObject(const_mem);
  this->unload_program(program_verbose);

  // restore cout buf
  cout_ptr = std::cout.rdbuf(cout_ptr);
}

// Test the max_work_group_size kernel argument
// For one, two, and three-dimensional workgroup sizes, checks a size
// slightly smaller than, and slightly larger than the max_wg_size.
MT_TEST(acl_kernel, test_max_work_group_size_attribute) {
  ACL_LOCKED(acl_print_debug_msg("wg size attr \n"));
  cl_int status;
  const cl_uint work_dim = 3;
  enum { MAX_KERNELS = 10 };
  size_t global_size[work_dim] = {192, 192, 64};
  cl_event event;

  // Use a kernel with only one argument, and without a
  // reqd_work_group_size attribute!
  cl_kernel kernel = clCreateKernel(m_program, "kernel4_task_double", &status);

  // Set up kernel args
  size_t mem_size = 64;
  cl_mem src_mem =
      clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(src_mem)));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));

  {
    // Test the max_work_group_size attribute
    size_t local_size[3] = {32768, 1, 1};
    global_size[0] = local_size[0]; // To divide evenly, as required
    CHECK_EQUAL(CL_SUCCESS,
                clEnqueueNDRangeKernel(m_cq, kernel, 1, 0, global_size,
                                       local_size, 0, 0, &event));

    local_size[0] = 32768 + 1;
    global_size[0] = local_size[0];
    CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
                clEnqueueNDRangeKernel(m_cq, kernel, 1, 0, global_size,
                                       local_size, 0, 0, &event));

    local_size[0] = 128;
    global_size[0] = local_size[0];
    local_size[1] = 256;
    global_size[1] = local_size[1];
    CHECK_EQUAL(CL_SUCCESS,
                clEnqueueNDRangeKernel(m_cq, kernel, 2, 0, global_size,
                                       local_size, 0, 0, &event));

    local_size[0] = 128;
    global_size[0] = local_size[0];
    local_size[1] = 256 + 1;
    global_size[1] = local_size[1];
    CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
                clEnqueueNDRangeKernel(m_cq, kernel, 2, 0, global_size,
                                       local_size, 0, 0, &event));

    local_size[0] = 128;
    global_size[0] = local_size[0];
    local_size[1] = 128;
    global_size[1] = local_size[1];
    local_size[2] = 2;
    global_size[2] = local_size[2];
    CHECK_EQUAL(CL_SUCCESS,
                clEnqueueNDRangeKernel(m_cq, kernel, 3, 0, global_size,
                                       local_size, 0, 0, &event));

    local_size[0] = 128 + 1;
    global_size[0] = local_size[0];
    local_size[1] = 128;
    global_size[1] = local_size[1];
    local_size[2] = 2;
    global_size[2] = local_size[2];
    CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
                clEnqueueNDRangeKernel(m_cq, kernel, 3, 0, global_size,
                                       local_size, 0, 0, &event));
  }
  // set the all the commands in the queue to complete so that they can be
  // popped and deleted
  complete_command_queue(m_cq);

  // Clean up after the test
  clReleaseKernel(kernel);
  clReleaseMemObject(src_mem);
  clReleaseEvent(event);
}

TEST(acl_kernel, enqueue_ndrange_workgroup_invariant_kernel) {
  ACL_LOCKED(acl_print_debug_msg("wginvariant \n"));
  cl_int status;

  status = CL_INVALID_VALUE;
  cl_kernel kernel = clCreateKernel(
      m_program, m_sample_kernel_workgroup_invariant_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(acl_print_debug_msg("here 0\n"));

  // Set up kernel args
  cl_uint int_arg = 42;
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, 4, &int_arg));

  const cl_uint work_dim = 3;

  ACL_LOCKED(acl_print_debug_msg("here 4\n"));
  // Check global sizes.
  size_t global_size[work_dim] = {192, 192, 64}; // 192 = 3 * 64
  size_t local_size[work_dim] = {64, 64, 8};

  // This should launch.
  cl_event event;
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));
  // Check some internal data.
  CHECK_EQUAL(CL_COMMAND_NDRANGE_KERNEL, event->cmd.type);
  CHECK_EQUAL(kernel, event->cmd.info.ndrange_kernel.kernel);

  CHECK(event->cmd.info.ndrange_kernel.serialization_wrapper == NULL);
  acl_dev_kernel_invocation_image_t *invocation =
      event->cmd.info.ndrange_kernel.invocation_wrapper->image;
  CHECK_EQUAL(m_sample_kernel_workgroup_invariant_accel_id,
              invocation->accel_id);
  CHECK_EQUAL(work_dim, invocation->work_dim);
  CHECK_EQUAL(global_size[0], invocation->global_work_size[0]);
  CHECK_EQUAL(global_size[1], invocation->global_work_size[1]);
  CHECK_EQUAL(global_size[2], invocation->global_work_size[2]);
  // Here's the trick.  Since this kernel is workgroup_invariant, the
  // local sizes are forced to global sizes.
  CHECK_EQUAL(global_size[0], invocation->local_work_size[0]);
  CHECK_EQUAL(global_size[1], invocation->local_work_size[1]);
  CHECK_EQUAL(global_size[2], invocation->local_work_size[2]);
  CHECK_EQUAL(1, invocation->num_groups[0]);
  CHECK_EQUAL(1, invocation->num_groups[1]);
  CHECK_EQUAL(1, invocation->num_groups[2]);

  // Fake completion of the task.
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));

  CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(1, &event));

  ACL_LOCKED(acl_print_debug_msg("here 7\n"));

  clReleaseKernel(kernel);
  clReleaseEvent(event);

  CHECK_EQUAL(0, m_program->num_kernels);
}

TEST(acl_kernel, enqueue_ndrange_workitem_invariant_kernel) {
  ACL_LOCKED(acl_print_debug_msg("wiinvariant \n"));
  cl_int status;

  status = CL_INVALID_VALUE;
  cl_kernel kernel = clCreateKernel(
      m_program, m_sample_kernel_workitem_invariant_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(acl_print_debug_msg("here 0\n"));

  // Set up kernel args
  cl_uint int_arg = 42;
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, 4, &int_arg));

  const cl_uint work_dim = 3;

  ACL_LOCKED(acl_print_debug_msg("here 4\n"));
  // Check global sizes.
  size_t global_size[work_dim] = {2, 3, 4};
  size_t local_size[work_dim] = {2, 1, 1};

  // This should launch.
  cl_event event;
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));
  // Check some internal data.
  CHECK_EQUAL(CL_COMMAND_NDRANGE_KERNEL, event->cmd.type);
  CHECK_EQUAL(kernel, event->cmd.info.ndrange_kernel.kernel);

  CHECK(event->cmd.info.ndrange_kernel.serialization_wrapper != NULL);
  // The actual ndrange size info should be in the serialization wrapper image
  {
    acl_dev_kernel_invocation_image_t *invocation =
        event->cmd.info.ndrange_kernel.serialization_wrapper->image;
    CHECK_EQUAL(m_sample_kernel_workitem_invariant_accel_id,
                invocation->accel_id);
    CHECK_EQUAL(work_dim, invocation->work_dim);
    CHECK_EQUAL(global_size[0], invocation->global_work_size[0]);
    CHECK_EQUAL(global_size[1], invocation->global_work_size[1]);
    CHECK_EQUAL(global_size[2], invocation->global_work_size[2]);
    // Since this kernel is workgroup_invariant, the
    // local sizes are forced to global sizes.
    CHECK_EQUAL(global_size[0], invocation->local_work_size[0]);
    CHECK_EQUAL(global_size[1], invocation->local_work_size[1]);
    CHECK_EQUAL(global_size[2], invocation->local_work_size[2]);
    CHECK_EQUAL(1, invocation->num_groups[0]);
    CHECK_EQUAL(1, invocation->num_groups[1]);
    CHECK_EQUAL(1, invocation->num_groups[2]);
  }

  //////////////
  // Since the ndrange is serialized, the invocation wrapper images should see a
  // global size of 1,1,1.
  {
    acl_dev_kernel_invocation_image_t *invocation =
        event->cmd.info.ndrange_kernel.invocation_wrapper->image;
    CHECK_EQUAL(m_sample_kernel_workitem_invariant_accel_id,
                invocation->accel_id);
    CHECK_EQUAL(1, invocation->work_dim);
    CHECK_EQUAL(1, invocation->global_work_size[0]);
    CHECK_EQUAL(1, invocation->global_work_size[1]);
    CHECK_EQUAL(1, invocation->global_work_size[2]);
    CHECK_EQUAL(1, invocation->local_work_size[0]);
    CHECK_EQUAL(1, invocation->local_work_size[1]);
    CHECK_EQUAL(1, invocation->local_work_size[2]);
    CHECK_EQUAL(1, invocation->num_groups[0]);
    CHECK_EQUAL(1, invocation->num_groups[1]);
    CHECK_EQUAL(1, invocation->num_groups[2]);
  }

  // Fake completion of the task.
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));

  // Since the ndrange is serialized to "global_size" tasks, the first
  // "global_size-1" completions should put the state back to running, and not
  // trigger any event updates.
  for (size_t i = 0; i < global_size[0] * global_size[1] * global_size[2] - 1;
       i++) {
    int num_updates;
    ACL_LOCKED(acl_print_debug_msg("serilialized event:%d\n", (int)i));
    ACL_LOCKED(
        acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));
    ACL_LOCKED(num_updates = acl_update_queue(m_cq));
    CHECK_EQUAL(0, num_updates);
  }

  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));
  CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(1, &event));

  ACL_LOCKED(acl_print_debug_msg("here 7\n"));

  clReleaseKernel(kernel);
  clReleaseEvent(event);

  CHECK_EQUAL(0, m_program->num_kernels);
}

TEST(acl_kernel, enqueue_task) {
  ACL_LOCKED(acl_print_debug_msg("enqueue task\n"));
  // Launch a simple task kernel
  cl_int status;

  size_t mem_size = 64;
  cl_mem src_mem =
      clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(src_mem)));

  cl_kernel kernel = clCreateKernel(m_program, m_sample_task_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));
  cl_event event;
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, kernel, 0, 0, &event));

  // Check some internal data.
  CHECK_EQUAL(CL_COMMAND_TASK, event->cmd.type);
  CHECK_EQUAL(kernel, event->cmd.info.ndrange_kernel.kernel);

  acl_dev_kernel_invocation_image_t *invocation =
      event->cmd.info.ndrange_kernel.invocation_wrapper->image;

  CHECK_EQUAL(m_sample_task_accel_id, invocation->accel_id);

  CHECK_EQUAL(1, invocation->work_dim);
  CHECK_EQUAL(1, invocation->global_work_size[0]);
  CHECK_EQUAL(1, invocation->num_groups[0]);
  CHECK_EQUAL(1, invocation->local_work_size[0]);

  // Unused dimensions should have values set to 1.
  // This is expected by the wrapper generated by LegUp++
  CHECK_EQUAL(1, invocation->global_work_size[1]);
  CHECK_EQUAL(1, invocation->global_work_size[2]);
  CHECK_EQUAL(1, invocation->num_groups[1]);
  CHECK_EQUAL(1, invocation->num_groups[2]);
  CHECK_EQUAL(1, invocation->local_work_size[1]);
  CHECK_EQUAL(1, invocation->local_work_size[2]);

  // Check profiling results.
  cl_ulong times[4];
  this->load_times(event, times);
  ACL_LOCKED(acl_print_debug_msg(" ttt %d %d %d %d\n", (int)times[0],
                                 (int)times[1], (int)times[2], (int)times[3]));
  CHECK(0 < times[0]); // has been queued
  CHECK(
      times[0] <
      times[1]); // has been submitted, becase we were not waiting on anything.

  // Waiting for response from device to say kernel has started running.
  // But the active op is in submitted state until we hear back from the HAL.
  acl_device_op_t *active_op;
  active_op = event->current_device_op;
  CHECK(active_op);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, active_op->info.type);
  CHECK_EQUAL(CL_SUBMITTED, active_op->status);
  // not complete yet.
  CHECK_EQUAL(0, times[3]);

  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));
  this->load_times(event, times);
  CHECK(times[1] < times[2]);
  active_op = event->current_device_op;
  CHECK(active_op);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, active_op->info.type);
  CHECK_EQUAL(CL_RUNNING, active_op->status);

  // not complete yet.
  CHECK_EQUAL(0, times[3]);
  ACL_LOCKED(acl_receive_kernel_update(active_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  this->load_times(event, times);
  CHECK(times[2] < times[3]);

  CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(1, &event));

  clReleaseMemObject(src_mem);
  clReleaseEvent(event);
  clReleaseKernel(kernel);
  ACL_LOCKED(acl_print_debug_msg("DONE!\n"));
}

MT_TEST(acl_kernel, cant_build_program_if_kernel_attached) {
  ACL_LOCKED(acl_print_debug_msg("cant_build_if_kern_attached\n"));
  cl_int status;

  cl_program program = this->load_program();
  this->build(program);
  // Now I can create the kernel
  cl_kernel kernel = clCreateKernel(program, m_sample_kernel_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Should not be able to build the program, because a kernel is attached
  CHECK_EQUAL(CL_INVALID_OPERATION, clBuildProgram(program, 0, 0, "", 0, 0));

  clReleaseKernel(kernel);
  this->unload_program(program);
}

TEST(acl_kernel, local_arg_alloc) {
  acl_print_debug_msg("local_arg_alloc\n");
  // Check the bump allocation for pointer-to-local arguments.
  // We will check the address delta between two consecutive
  // pointer-to-local arguments.

  cl_int status;

  // Two sizes we'll try.
  size_t local_sizes[] = {128, 512};

  cl_kernel kernel = clCreateKernel(m_program, m_sample_locals_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Send in NULL for the global pointer args.  Kind of a boundary check.
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), 0));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 3, sizeof(cl_mem), 0));

  for (size_t itry = 0; itry < sizeof(local_sizes) / sizeof(local_sizes[0]);
       ++itry) {
    const int second_local_size = 1; // doesn't matter, as long as it's positive
    acl_print_debug_msg("%d ", local_sizes[itry]);
    CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 1, local_sizes[itry], 0));
    CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 2, second_local_size, 0));

    cl_event event;
    CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, kernel, 0, 0, &event));

    // Check some internal data.
    CHECK_EQUAL(CL_COMMAND_TASK, event->cmd.type);
    CHECK_EQUAL(kernel, event->cmd.info.ndrange_kernel.kernel);

    acl_dev_kernel_invocation_image_t *invocation =
        event->cmd.info.ndrange_kernel.invocation_wrapper->image;

    CHECK(m_dev_global_ptr_size_in_cra == 4 ||
          m_dev_global_ptr_size_in_cra == 8);
    CHECK(m_dev_local_ptr_size_in_cra == 4);

    // Check the invocation arguments.
    // Ignoring endian-ness differences here!
    if (m_dev_global_ptr_size_in_cra == 4) {
      const int num_args = 4;
      cl_uint *arg_ptr;
      cl_uint arg[num_args];
      for (int i = 0; i < num_args; i++) {
        arg_ptr = ((cl_uint *)&(
            invocation->arg_value[m_dev_global_ptr_size_in_cra * i]));
        arg[i] = *arg_ptr;
      }
      CHECK_EQUAL(0, arg[0]);
      CHECK_EQUAL(0, arg[3]);
      acl_print_debug_msg(" local args ( %d, %d ) -> %x, %x -> delta %d \n",
                          local_sizes[itry], second_local_size, arg[1], arg[2],
                          arg[2] - arg[1]);
      CHECK(arg[1] != arg[2]);

      // Check the bump allocator for pointer-to-local arguments.
      // Blocks are separated at least by the memory alignment
      // requirement.
      size_t expected_delta = local_sizes[itry];
      if (expected_delta < ACL_MEM_ALIGN)
        expected_delta = ACL_MEM_ALIGN;

      CHECK_EQUAL(expected_delta,
                  arg[2] - arg[1]); // Check offset. Verifies bump allocator.
      acl_print_debug_msg("exp delta pass\n");
    } else if (m_dev_global_ptr_size_in_cra == 8) {
      // Just too confusing to do in a loop.
      // Just walk through the array, incrementing by arg sizes.
      // For this kernel it's global local local global.
      const char *p = (char *)invocation->arg_value;
      cl_ulong arg[4];
      arg[0] = *(cl_ulong *)p;
      p += m_dev_global_ptr_size_in_cra;
      arg[1] = *(cl_uint *)p;
      p += m_dev_local_ptr_size_in_cra;
      arg[2] = *(cl_uint *)p;
      p += m_dev_local_ptr_size_in_cra;
      arg[3] = *(cl_ulong *)p;
      p += m_dev_global_ptr_size_in_cra;
      CHECK_EQUAL(0, arg[0]);
      CHECK_EQUAL(0, arg[3]);
      CHECK(arg[1] != arg[2]);

      size_t expected_delta = local_sizes[itry];
      if (expected_delta < ACL_MEM_ALIGN)
        expected_delta = ACL_MEM_ALIGN;
      CHECK_EQUAL(expected_delta,
                  arg[2] - arg[1]); // Check offset. Verifies bump allocator.
    }

    // Fake completion of the task.
    acl_print_debug_msg(" set running\n");
    ACL_LOCKED(
        acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));
    acl_print_debug_msg(" set complete\n");
    ACL_LOCKED(
        acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));

    acl_print_debug_msg("wait for events\n");
    CHECK_EQUAL(CL_SUCCESS, clWaitForEvents(1, &event));
    acl_print_debug_msg("wait for events done\n");

    clReleaseEvent(event);
  }
  clReleaseKernel(kernel);
}

TEST(acl_kernel, fast_launch_with_dependencies_ooo) {
  // Out-of-order queue
  // make sure that kernels get pushed onto the device_op_queue
  // once all of their dependencies get resolved
  // makes sure that fast kernel relaunch(FKR) can still happen even if parent
  // has a dependency that cannot be FKR-ed

  cl_int status;
  cl_command_queue cq2 = clCreateCommandQueue(
      m_context, m_device[0],
      CL_QUEUE_PROFILING_ENABLE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE,
      &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  cl_kernel FastKernelRelaunch_kernel =
      clCreateKernel(m_program, m_sample_task_with_fast_relaunch_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(1, FastKernelRelaunch_kernel->accel_def->fast_launch_depth);
  cl_kernel reg_kernel = clCreateKernel(
      m_program, m_sample_kernel_workitem_invariant_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Set up kernel args.
  size_t mem_size = 64;
  cl_mem src_mem =
      clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(src_mem)));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(FastKernelRelaunch_kernel, 0,
                                         sizeof(cl_mem), &src_mem));
  cl_uint int_arg = 42;
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArg(reg_kernel, 0, sizeof(cl_uint), &int_arg));

  cl_event event[3];
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueTask(cq2, FastKernelRelaunch_kernel, 0, 0, &event[0]));
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueTask(cq2, reg_kernel, 1, &event[0], &event[1]));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(cq2, FastKernelRelaunch_kernel, 1,
                                        &event[0], &event[2]));

  // Start test
  ACL_LOCKED(acl_idle_update(m_context));

  CHECK_EQUAL(CL_SUBMITTED, event[0]->execution_status);
  CHECK_EQUAL(CL_QUEUED,
              event[1]->execution_status); // Success! Didn't FKR because it is
                                           // different than parent
  CHECK_EQUAL(CL_SUBMITTED,
              event[2]->execution_status); // Success! Submitted before the
                                           // previous is finished

  ACL_LOCKED(
      acl_receive_kernel_update(event[0]->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));

  CHECK_EQUAL(CL_RUNNING, event[0]->execution_status);
  CHECK_EQUAL(CL_QUEUED, event[1]->execution_status);
  CHECK_EQUAL(CL_SUBMITTED, event[2]->execution_status); // buffered on device

  ACL_LOCKED(
      acl_receive_kernel_update(event[0]->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  CHECK_EQUAL(CL_SUBMITTED, event[1]->execution_status);
  CHECK_EQUAL(CL_SUBMITTED, event[2]->execution_status);
  ACL_LOCKED(
      acl_receive_kernel_update(event[1]->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(
      acl_receive_kernel_update(event[2]->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));

  CHECK_EQUAL(CL_COMPLETE, event[0]->execution_status);
  CHECK_EQUAL(CL_RUNNING, event[1]->execution_status);
  CHECK_EQUAL(CL_RUNNING, event[2]->execution_status);

  ACL_LOCKED(
      acl_receive_kernel_update(event[1]->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(
      acl_receive_kernel_update(event[2]->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));

  clReleaseEvent(event[0]);
  clReleaseEvent(event[1]);
  clReleaseEvent(event[2]);
  clReleaseMemObject(src_mem);
  clReleaseKernel(FastKernelRelaunch_kernel);
  clReleaseKernel(reg_kernel);
  clReleaseCommandQueue(cq2);
}

TEST(acl_kernel, fast_launch_with_dependencies) {
  // In-order queue
  // make sure that kernels get pushed onto the device_op_queue
  // once all of their dependencies get resolved

  cl_int status;
  cl_command_queue cq2 = clCreateCommandQueue(
      m_context, m_device[0], CL_QUEUE_PROFILING_ENABLE, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  cl_kernel kernel =
      clCreateKernel(m_program, m_sample_task_with_fast_relaunch_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(1, kernel->accel_def->fast_launch_depth);

  // Set up kernel args.
  size_t mem_size = 64;
  cl_mem src_mem =
      clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(src_mem)));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));

  // Enqueue on two separate queues.
  cl_event event[3];
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, kernel, 0, 0, &event[0]));
  // redundant dependency for in-order queues, but the user can still do it
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, kernel, 1, &event[0], &event[1]));
  // cross queue dependency
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(cq2, kernel, 2, &event[0], &event[2]));

  // nudge the queues
  ACL_LOCKED(acl_idle_update(m_context));

  CHECK_EQUAL(CL_SUBMITTED, event[0]->execution_status);
  CHECK_EQUAL(CL_SUBMITTED, event[1]->execution_status); // buffered on device
  CHECK_EQUAL(CL_SUBMITTED,
              event[2]->execution_status); // stalled in the device_op_queue

  ACL_LOCKED(
      acl_receive_kernel_update(event[0]->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));

  CHECK_EQUAL(CL_RUNNING, event[0]->execution_status);
  CHECK_EQUAL(CL_SUBMITTED, event[1]->execution_status); // buffered on device
  CHECK_EQUAL(CL_SUBMITTED,
              event[2]->execution_status); // stalled in the device_op_queue

  ACL_LOCKED(
      acl_receive_kernel_update(event[0]->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  ACL_LOCKED(
      acl_receive_kernel_update(event[1]->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));

  CHECK_EQUAL(CL_COMPLETE, event[0]->execution_status);
  CHECK_EQUAL(CL_RUNNING, event[1]->execution_status);
  CHECK_EQUAL(CL_SUBMITTED, event[2]->execution_status); // buffered on device

  ACL_LOCKED(
      acl_receive_kernel_update(event[1]->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  ACL_LOCKED(
      acl_receive_kernel_update(event[2]->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));
  ACL_LOCKED(
      acl_receive_kernel_update(event[2]->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));

  CHECK_EQUAL(CL_COMPLETE, event[0]->execution_status);
  CHECK_EQUAL(CL_COMPLETE, event[1]->execution_status);
  CHECK_EQUAL(CL_COMPLETE, event[2]->execution_status);

  clReleaseEvent(event[0]);
  clReleaseEvent(event[1]);
  clReleaseEvent(event[2]);

  clReleaseMemObject(src_mem);
  clReleaseKernel(kernel);
  clReleaseCommandQueue(cq2);
}

TEST(acl_kernel, multi_queue) {
  // A single non-fast-launchable kernel enqueued once on two different
  // command queues. We should see both of the kernels being pushed to
  // device_op_queue, with one being pushed onto device, and the other being
  // stalled.

  cl_int status;
  cl_command_queue cq2 = clCreateCommandQueue(
      m_context, m_device[0], CL_QUEUE_PROFILING_ENABLE, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  cl_kernel kernel = clCreateKernel(m_program, m_sample_task_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(0, kernel->accel_def->fast_launch_depth);

  // Set up kernel args.
  size_t mem_size = 64;
  cl_mem src_mem =
      clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(src_mem)));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));

  // Enqueue on two separate queues.
  cl_event event_1, event_2;
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, kernel, 0, 0, &event_1));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(cq2, kernel, 0, 0, &event_2));

  // nudge the queues
  ACL_LOCKED(acl_idle_update(m_context));

  CHECK(event_1->is_on_device_op_queue);
  CHECK(event_2->is_on_device_op_queue);
  CHECK_EQUAL(CL_SUBMITTED, event_1->execution_status);
  CHECK_EQUAL(CL_SUBMITTED, event_2->execution_status);

  // MEM_MIGRATES for both kernels should be finished
  // both events should be on their kernel_op
  // not using event->current_device_op because current_device_op
  // is set only after SUBMITTED state transition, and one of the kernels is
  // only QUEUED
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, event_1->last_device_op->info.type);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, event_2->last_device_op->info.type);

  // One kernel will be submitted while the other one will be stalled
  acl_device_op_t *active_op = (event_1->last_device_op->status == CL_SUBMITTED
                                    ? event_1->last_device_op
                                    : event_2->last_device_op);
  acl_device_op_t *stalled_op =
      (active_op->info.event == event_1 ? event_2->last_device_op
                                        : event_1->last_device_op);
  CHECK(active_op);
  CHECK(stalled_op);
  CHECK_EQUAL(CL_SUBMITTED, active_op->status);
  CHECK_EQUAL(CL_QUEUED, stalled_op->status);

  ACL_LOCKED(acl_receive_kernel_update(active_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));

  // Still stalled
  CHECK_EQUAL(CL_RUNNING, active_op->status);
  CHECK_EQUAL(CL_QUEUED, stalled_op->status);

  ACL_LOCKED(acl_receive_kernel_update(active_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));

  // Finally submitted
  CHECK_EQUAL(CL_COMPLETE, active_op->status);
  CHECK_EQUAL(CL_COMPLETE, active_op->info.event->execution_status);
  CHECK_EQUAL(CL_SUBMITTED, stalled_op->status);

  ACL_LOCKED(acl_receive_kernel_update(stalled_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));
  CHECK_EQUAL(CL_RUNNING, stalled_op->status);

  ACL_LOCKED(acl_receive_kernel_update(stalled_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  CHECK_EQUAL(CL_COMPLETE, stalled_op->status);
  CHECK_EQUAL(CL_COMPLETE, stalled_op->info.event->execution_status);

  clReleaseEvent(event_1);
  clReleaseEvent(event_2);

  clReleaseMemObject(src_mem);
  clReleaseKernel(kernel);
  clReleaseCommandQueue(cq2);
}

TEST(acl_kernel, multi_queue_with_fast_launch) {
  // A single fast-launchable kernel enqueued once on two different
  // command queues. We should see both of the kernels being pushed to
  // device_op_queue and onto the device. One buffered while the other executing

  cl_int status;
  cl_command_queue cq2 = clCreateCommandQueue(
      m_context, m_device[0], CL_QUEUE_PROFILING_ENABLE, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  cl_kernel kernel =
      clCreateKernel(m_program, m_sample_task_with_fast_relaunch_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(1, kernel->accel_def->fast_launch_depth);

  // Set up kernel args.
  size_t mem_size = 64;
  cl_mem src_mem =
      clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(src_mem)));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));

  // Enqueue on two separate queues.
  cl_event event_1;
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, kernel, 0, 0, &event_1));
  cl_event event_2;
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(cq2, kernel, 0, 0, &event_2));

  // nudge the queues
  ACL_LOCKED(acl_idle_update(m_context));

  CHECK(event_1->is_on_device_op_queue);
  CHECK(event_2->is_on_device_op_queue);
  CHECK_EQUAL(CL_SUBMITTED, event_1->execution_status);
  CHECK_EQUAL(CL_SUBMITTED, event_2->execution_status);

  // MEM_MIGRATES for both kernels should be finished
  // both events should be on their kernel_op
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, event_1->current_device_op->info.type);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, event_2->current_device_op->info.type);

  // Both kernels will be submitted to the device.
  // device_op_queue traversed sequentially, events that get there first
  // executed first
  acl_device_op_t *active_op = event_1->current_device_op;
  acl_device_op_t *buffered_op = event_2->current_device_op;
  CHECK(active_op);
  CHECK(buffered_op);
  CHECK_EQUAL(CL_SUBMITTED, active_op->status);
  CHECK_EQUAL(CL_SUBMITTED, buffered_op->status);

  ACL_LOCKED(acl_receive_kernel_update(active_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));

  // Still buffered
  CHECK_EQUAL(CL_RUNNING, active_op->status);
  CHECK_EQUAL(CL_SUBMITTED, buffered_op->status);

  ACL_LOCKED(acl_receive_kernel_update(active_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));

  // Runtime still considers the event buffered until the kernel reports it is
  // running
  CHECK_EQUAL(CL_COMPLETE, active_op->status);
  CHECK_EQUAL(CL_COMPLETE, active_op->info.event->execution_status);
  CHECK_EQUAL(CL_SUBMITTED, buffered_op->status);

  ACL_LOCKED(acl_receive_kernel_update(buffered_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));
  CHECK_EQUAL(CL_RUNNING, buffered_op->status);

  ACL_LOCKED(acl_receive_kernel_update(buffered_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  CHECK_EQUAL(CL_COMPLETE, buffered_op->status);
  CHECK_EQUAL(CL_COMPLETE, buffered_op->info.event->execution_status);

  clReleaseEvent(event_1);
  clReleaseEvent(event_2);

  clReleaseMemObject(src_mem);
  clReleaseKernel(kernel);
  clReleaseCommandQueue(cq2);
}

MT_TEST(acl_kernel, profiler) {
  ACL_LOCKED(acl_print_debug_msg("profiler\n"));
  cl_int status;
  cl_kernel kernel;
  status = CL_INVALID_VALUE;

  kernel = clCreateKernel(m_program, "kernel6_profiletest", &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  CHECK_EQUAL(149, kernel->accel_def->profiling_words_to_readback);

  clReleaseKernel(kernel);
}

TEST(acl_kernel, two_task) {
  // Launch a simple task kernel, twice
  // fast kernel relaunch is disabled, see that as soon as the first kernel is
  // set to RUNNING the second DOES NOT get launched on the device
  cl_int status;

  size_t mem_size = 64;
  char buf[64] = {'a', 'b'};
  int offset = m_devlog.num_ops;

  cl_mem src_mem =
      clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(src_mem)));

  CHECK_EQUAL(CL_SUCCESS, clEnqueueWriteBuffer(m_cq, src_mem, CL_TRUE, 0,
                                               mem_size, buf, 0, NULL, NULL));
  // Transfer buffer +
  // set MIRROR_TO_DEV to RUNNING +
  // set MIRROR_TO_DEV to COMPLETE = 3
  CHECK_EQUAL(offset + 3, m_devlog.num_ops);
  cl_kernel kernel = clCreateKernel(m_program, m_sample_task_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(0, kernel->accel_def->fast_launch_depth);

  cl_ulong before;
  ACL_LOCKED(before = acl_get_hal()->get_timestamp());
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));

  cl_event event;
  cl_event event2;
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, kernel, 0, 0, &event));
  // set MEM_MIGRATE1 to RUNNING +
  // set MEM_MIGRATE1 to COMPLETE +
  // submit KERNEL1 to device = 3
  CHECK_EQUAL(offset + 6, m_devlog.num_ops);
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, kernel, 0, 0, &event2));
  // set MEM_MIGRATE2 to RUNNING +
  // set MEM_MIGRATE2 to COMPLETE = 2
  // The Important Check: KERNEL 2 is not submitted to the device yet
  // Even through MEM_MIGRATE2 is safe to execute
  CHECK_EQUAL(offset + 8, m_devlog.num_ops); // nothing should change

  // Check some internal data.
  CHECK_EQUAL(CL_COMMAND_TASK, event->cmd.type);
  CHECK_EQUAL(kernel, event->cmd.info.ndrange_kernel.kernel);
  CHECK_EQUAL(CL_COMMAND_TASK, event2->cmd.type);
  CHECK_EQUAL(kernel, event2->cmd.info.ndrange_kernel.kernel);

  // Check profiling results.
  cl_ulong times[4];
  this->load_times(event, times);

  // Each kernel enqueue uses 1 event, to launch the kernel.
  CHECK_EQUAL(before + 1, times[0]); // has been queued
  CHECK(
      times[0] <
      times[1]); // has been submitted, becase we were not waiting on anything.
  // Waiting for response from device to say kernel has started running.
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));

  // set KERNEL1 to RUNNING = 1
  // The Important Check: KERNEL 2 is not submitted to the device yet
  CHECK_EQUAL(offset + 9, m_devlog.num_ops);
  this->load_times(event, times);
  CHECK(
      times[1] <
      times[2]); // The kernel is running, since the mem migration has happened.
  CHECK_EQUAL(0, times[3]); // not yet complete
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL1 to COMPLETE +
  // submit KERNEL2 to device = 2
  CHECK_EQUAL(offset + 11, m_devlog.num_ops);
  this->load_times(event, times);
  CHECK(times[2] < times[3]);
  ACL_LOCKED(acl_print_debug_msg(" ttt %d %d %d %d\n", (int)times[0],
                                 (int)times[1], (int)times[2], (int)times[3]));

  before = times[3]; // complete time of the first kernel.

  this->load_times(event2, times);
  // we received updates on the "program/marker" event and also on the kernel
  // launch event.
  ACL_LOCKED(acl_print_debug_msg(" ttt %d %d %d %d\n", (int)times[0],
                                 (int)times[1], (int)times[2], (int)times[3]));
  CHECK(0 < times[0]); // has been queued
  CHECK(
      times[0] <
      times[1]); // has been submitted, because we were not waiting on anything.

  // With fast kernel relaunch enabled or disabled, we can have multiple kernels
  // in a non CL_QUEUED state on the device at the same time.
  CHECK(before > times[1]); // submitted before the first one finished

  // Waiting for response from device to say kernel has started running.
  CHECK_EQUAL(0, times[3]);
  ACL_LOCKED(
      acl_receive_kernel_update(event2->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL2 to RUNNING = 1
  CHECK_EQUAL(offset + 12, m_devlog.num_ops);
  this->load_times(event, times);
  CHECK(times[1] < times[2]);
  ACL_LOCKED(
      acl_receive_kernel_update(event2->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL2 to COMPLETE = 1
  CHECK_EQUAL(offset + 13, m_devlog.num_ops);
  this->load_times(event, times);
  CHECK(times[2] < times[3]);

  CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));
  CHECK_EQUAL(offset + 13, m_devlog.num_ops); // nothing else should happen

  clReleaseMemObject(src_mem);
  clReleaseEvent(event);
  clReleaseEvent(event2);
  clReleaseKernel(kernel);
}

TEST(acl_kernel, two_task_with_fast_relaunch) {
  // Launch a simple task kernel, twice
  // fast kernel relaunch is enabled, see that as soon as the first kernel is
  // set to RUNNING the second gets launched on the device
  cl_int status;

  size_t mem_size = 64;
  char buf[64] = {'a', 'b'};
  int offset = m_devlog.num_ops;

  cl_mem src_mem =
      clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(src_mem)));

  CHECK_EQUAL(CL_SUCCESS, clEnqueueWriteBuffer(m_cq, src_mem, CL_TRUE, 0,
                                               mem_size, buf, 0, NULL, NULL));
  // Transfer buffer +
  // set MIRROR_TO_DEV to RUNNING +
  // set MIRROR_TO_DEV to COMPLETE = 3
  CHECK_EQUAL(offset + 3, m_devlog.num_ops);
  cl_kernel kernel =
      clCreateKernel(m_program, m_sample_task_with_fast_relaunch_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(1, kernel->accel_def->fast_launch_depth);

  cl_ulong before;
  ACL_LOCKED(before = acl_get_hal()->get_timestamp());
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));

  cl_event event;
  cl_event event2;
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, kernel, 0, 0, &event));
  // set MEM_MIGRATE1 to RUNNING +
  // set MEM_MIGRATE1 to COMPLETE +
  // submit KERNEL1 to device = 3
  CHECK_EQUAL(offset + 6, m_devlog.num_ops);
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, kernel, 0, 0, &event2));
  // set MEM_MIGRATE2 to RUNNING +
  // set MEM_MIGRATE2 to COMPLETE +
  // submit KERNEL2 to device = 3
  // safe to submit kernel2 to device because kernel1 is already on the device
  CHECK_EQUAL(offset + 9, m_devlog.num_ops);

  // Check some internal data.
  CHECK_EQUAL(CL_COMMAND_TASK, event->cmd.type);
  CHECK_EQUAL(kernel, event->cmd.info.ndrange_kernel.kernel);
  CHECK_EQUAL(CL_COMMAND_TASK, event2->cmd.type);
  CHECK_EQUAL(kernel, event2->cmd.info.ndrange_kernel.kernel);

  // Check profiling results.
  cl_ulong times[4];
  this->load_times(event, times);

  // Each kernel enqueue uses 1 event, to launch the kernel.
  CHECK_EQUAL(before + 1, times[0]); // has been queued
  CHECK(
      times[0] <
      times[1]); // has been submitted, because we were not waiting on anything.
  // Waiting for response from device to say kernel has started running.
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));

  // set KERNEL1 to RUNNING = 1
  // The Important Check: KERNEL 2 is submitted to device as soon as the
  // previous is RUNNING
  CHECK_EQUAL(offset + 10, m_devlog.num_ops);
  this->load_times(event, times);
  CHECK(
      times[1] <
      times[2]); // The kernel is running, since the mem migration has happened.
  CHECK_EQUAL(0, times[3]); // not yet complete
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL1 to COMPLETE = 1
  CHECK_EQUAL(offset + 11, m_devlog.num_ops);
  this->load_times(event, times);
  CHECK(times[2] < times[3]);
  ACL_LOCKED(acl_print_debug_msg(" ttt %d %d %d %d\n", (int)times[0],
                                 (int)times[1], (int)times[2], (int)times[3]));

  before = times[3]; // complete time of the first kernel.

  this->load_times(event2, times);
  // we received updates on the "program/marker" event and also on the kernel
  // launch event.
  ACL_LOCKED(acl_print_debug_msg(" ttt %d %d %d %d\n", (int)times[0],
                                 (int)times[1], (int)times[2], (int)times[3]));
  CHECK(0 < times[0]); // has been queued
  CHECK(
      times[0] <
      times[1]); // has been submitted, because we were not waiting on anything.

  // With fast kernel relaunch enabled or disabled, we can have multiple kernels
  // in a non CL_QUEUED state on the device at the same time.
  CHECK(before > times[1]); // submitted before the first one finished

  // Waiting for response from device to say kernel has started running.
  CHECK_EQUAL(0, times[3]);
  ACL_LOCKED(
      acl_receive_kernel_update(event2->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL2 to RUNNING = 1
  CHECK_EQUAL(offset + 12, m_devlog.num_ops);
  this->load_times(event, times);
  CHECK(times[1] < times[2]);
  ACL_LOCKED(
      acl_receive_kernel_update(event2->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL2 to COMPLETE = 1
  CHECK_EQUAL(offset + 13, m_devlog.num_ops);
  this->load_times(event, times);
  CHECK(times[2] < times[3]);

  CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));
  CHECK_EQUAL(offset + 13, m_devlog.num_ops); // nothing else should happen

  clReleaseMemObject(src_mem);
  clReleaseEvent(event);
  clReleaseEvent(event2);
  clReleaseKernel(kernel);
}

TEST(acl_kernel, fast_relaunch_with_subbuffer) {
  // Launch a simple task kernel, twice
  // Kernel uses subbuffers.
  // fast kernel relaunch is enabled,
  // see that no fast kernel behaviour is observed as
  cl_int status;

  const size_t mem_size = 64;
  size_t subbuffer_size = mem_size / 2;
  char buf[mem_size] = {'a', 'b'};
  int offset = m_devlog.num_ops;

  cl_mem src_mem = clCreateBuffer(m_context, CL_MEM_READ_ONLY,
                                  ACL_MEM_ALIGN * 2, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(src_mem)));

  cl_mem subbuffer[2];
  cl_buffer_region region = {0, subbuffer_size};
  subbuffer[0] = clCreateSubBuffer(src_mem, 0, CL_BUFFER_CREATE_TYPE_REGION,
                                   &region, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  region.origin = ACL_MEM_ALIGN;
  subbuffer[1] = clCreateSubBuffer(src_mem, 0, CL_BUFFER_CREATE_TYPE_REGION,
                                   &region, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  CHECK_EQUAL(CL_SUCCESS, clEnqueueWriteBuffer(m_cq, src_mem, CL_FALSE, 0,
                                               mem_size, buf, 0, NULL, NULL));

  // Transfer buffer +
  // set MIRROR_TO_DEV to RUNNING +
  // set MIRROR_TO_DEV to COMPLETE = 3
  CHECK_EQUAL(offset + 3, m_devlog.num_ops);
  cl_kernel kernel =
      clCreateKernel(m_program, m_sample_task_with_fast_relaunch_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(1, kernel->accel_def->fast_launch_depth);

  cl_ulong before;
  ACL_LOCKED(before = acl_get_hal()->get_timestamp());
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArg(kernel, 0, sizeof(cl_mem), &subbuffer[0]));

  cl_event event;
  cl_event event2;
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, kernel, 0, 0, &event));
  // set MEM_MIGRATE1 to RUNNING +
  // set MEM_MIGRATE1 to COMPLETE +
  // submit KERNEL1 to device = 3
  CHECK_EQUAL(offset + 6, m_devlog.num_ops);
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArg(kernel, 0, sizeof(cl_mem), &subbuffer[1]));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, kernel, 0, 0, &event2));
  CHECK_EQUAL(offset + 6, m_devlog.num_ops); // nothing should change

  // Check some internal data.
  CHECK_EQUAL(CL_COMMAND_TASK, event->cmd.type);
  CHECK_EQUAL(kernel, event->cmd.info.ndrange_kernel.kernel);
  CHECK_EQUAL(CL_COMMAND_TASK, event2->cmd.type);
  CHECK_EQUAL(kernel, event2->cmd.info.ndrange_kernel.kernel);

  // Check profiling results.
  cl_ulong times[4];
  this->load_times(event, times);

  // Each kernel enqueue uses 1 event, to launch the kernel.
  CHECK_EQUAL(before + 1, times[0]); // has been queued
  CHECK(
      times[0] <
      times[1]); // has been submitted, because we were not waiting on anything.
  // Waiting for response from device to say kernel has started running.
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));

  // set KERNEL1 to RUNNING  = 1
  // The Important Check: KERNEL 2 is not submitted to the device yet, has
  // subbuffers
  CHECK_EQUAL(offset + 7, m_devlog.num_ops);
  this->load_times(event, times);
  CHECK(
      times[1] <
      times[2]); // The kernel is running, since the mem migration has happened.
  CHECK_EQUAL(0, times[3]); // not yet complete
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL1 to COMPLETE +
  // set MEM_MIGRATE2 to RUNNING +
  // set MEM_MIGRATE2 to COMPLETE +
  // submit KERNEL2 to device = 4
  CHECK_EQUAL(offset + 11, m_devlog.num_ops);
  this->load_times(event, times);
  CHECK(times[2] < times[3]);
  ACL_LOCKED(acl_print_debug_msg(" ttt %d %d %d %d\n", (int)times[0],
                                 (int)times[1], (int)times[2], (int)times[3]));

  before = times[2]; // start time of the first kernel.
  cl_ulong e1_end_time = times[3];

  this->load_times(event2, times);
  // we received updates on the "program/marker" event and also on the kernel
  // launch event.
  ACL_LOCKED(acl_print_debug_msg(" ttt %d %d %d %d\n", (int)times[0],
                                 (int)times[1], (int)times[2], (int)times[3]));
  CHECK(0 < times[0]); // has been queued
  CHECK(
      times[0] <
      times[1]); // has been submitted, because we were not waiting on anything.

  // With fast kernel relaunch enabled or disabled, we can have multiple kernels
  // in a non CL_QUEUED state on the device at the same time.
  CHECK(before < times[1]); // submitted after the first one started
  CHECK(e1_end_time < times[1]);

  // Waiting for response from device to say kernel has started running.
  CHECK_EQUAL(0, times[3]);
  ACL_LOCKED(
      acl_receive_kernel_update(event2->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL2 to RUNNING = 1
  CHECK_EQUAL(offset + 12, m_devlog.num_ops);
  this->load_times(event, times);
  CHECK(times[1] < times[2]);
  ACL_LOCKED(
      acl_receive_kernel_update(event2->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL2 to COMPLETE = 1
  CHECK_EQUAL(offset + 13, m_devlog.num_ops);
  this->load_times(event, times);
  CHECK(times[2] < times[3]);

  CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));
  CHECK_EQUAL(offset + 13, m_devlog.num_ops); // nothing else should happen

  clReleaseMemObject(subbuffer[0]);
  clReleaseMemObject(subbuffer[1]);
  clReleaseMemObject(src_mem);
  clReleaseEvent(event);
  clReleaseEvent(event2);
  clReleaseKernel(kernel);
}

TEST(acl_kernel, two_task_with_fast_relaunch_id_conflict) {
  // Setup: CmdQ: [K0, K1]
  // K0 and K1 are from same aocx
  // K1 should not leave the CmdQ until K0 is COMPLETE
  cl_int status;
  size_t mem_size = 64;
  char buf[64] = {'a', 'b'};

  cl_mem src_mem =
      clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(src_mem)));

  CHECK_EQUAL(CL_SUCCESS, clEnqueueWriteBuffer(m_cq, src_mem, CL_TRUE, 0,
                                               mem_size, buf, 0, NULL, NULL));

  // Doesn't matter which kernels to use as long as they are on same aocx
  // These ones happen to be named very similarly
  cl_kernel kernel0 =
      clCreateKernel(m_program, m_sample_task_with_fast_relaunch_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  cl_kernel kernel1 = clCreateKernel(m_program, m_sample_task_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(kernel0->accel_def->id != kernel1->accel_def->id);
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel0, 0, sizeof(cl_mem), &src_mem));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel1, 0, sizeof(cl_mem), &src_mem));

  cl_event event0;
  cl_event event1;
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, kernel0, 0, 0, &event0));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, kernel1, 0, 0, &event1));

  CHECK_EQUAL(2, m_cq->num_commands);
  CHECK_EQUAL(CL_SUBMITTED, event0->execution_status);
  CHECK_EQUAL(1, event0->is_on_device_op_queue);
  CHECK_EQUAL(CL_QUEUED, event1->execution_status);
  CHECK_EQUAL(0, event1->is_on_device_op_queue);

  ACL_LOCKED(
      acl_receive_kernel_update(event0->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));

  CHECK_EQUAL(2, m_cq->num_commands);
  CHECK_EQUAL(CL_RUNNING, event0->execution_status);
  CHECK_EQUAL(1, event0->is_on_device_op_queue);
  CHECK_EQUAL(CL_QUEUED, event1->execution_status);
  CHECK_EQUAL(
      0, event1->is_on_device_op_queue); // good shouldn't of been submitted

  ACL_LOCKED(
      acl_receive_kernel_update(event0->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));

  CHECK_EQUAL(1, m_cq->num_commands);
  CHECK_EQUAL(CL_COMPLETE, event0->execution_status);
  CHECK_EQUAL(1, event0->is_on_device_op_queue);
  CHECK_EQUAL(CL_SUBMITTED, event1->execution_status);
  CHECK_EQUAL(1, event1->is_on_device_op_queue); // now it is safe

  ACL_LOCKED(
      acl_receive_kernel_update(event1->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));

  ACL_LOCKED(
      acl_receive_kernel_update(event1->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));

  CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));

  clReleaseMemObject(src_mem);
  clReleaseEvent(event0);
  clReleaseEvent(event1);
  clReleaseKernel(kernel0);
  clReleaseKernel(kernel1);
}

TEST(acl_kernel, reset_kernel) {
  ACL_LOCKED(acl_print_debug_msg("enqueue task\n"));
  // Launch a simple task kernel, twice
  cl_int status;

  size_t mem_size = 64;
  char buf[64] = {'a', 'b'};
  cl_mem src_mem =
      clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(src_mem)));

  CHECK_EQUAL(CL_SUCCESS, clEnqueueWriteBuffer(m_cq, src_mem, CL_TRUE, 0,
                                               mem_size, buf, 0, NULL, NULL));

  cl_kernel kernel = clCreateKernel(m_program, m_sample_task_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  cl_device_id invalid_dev_list;
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));

  CHECK_EQUAL(CL_INVALID_CONTEXT, clResetKernelsIntelFPGA(0, 1, &m_device[0]));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clResetKernelsIntelFPGA(m_context, 0, &m_device[0]));
  CHECK_EQUAL(CL_INVALID_DEVICE,
              clResetKernelsIntelFPGA(m_context, 1, &invalid_dev_list));
  syncThreads();
  CHECK_EQUAL(CL_SUCCESS, clResetKernelsIntelFPGA(m_context, 0, 0));
  CHECK_EQUAL(CL_SUCCESS, clResetKernelsIntelFPGA(m_context, 1, &m_device[0]));
  syncThreads();
  CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));

  clReleaseMemObject(src_mem);
  clReleaseKernel(kernel);
  ACL_LOCKED(acl_print_debug_msg("DONE!\n"));
}

MT_TEST(acl_kernel, kernel_arg_info_na) {
  char arg_name[100];
  size_t size_ret;
  cl_int status;
  cl_kernel kernel;
  status = CL_INVALID_VALUE;

  ACL_LOCKED(acl_print_debug_msg("arg_info_na\n"));

  kernel = clCreateKernel(m_program, "kernel0_copy_vecin_vecout", &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Kernel arg info is not available:
  // just size.
  CHECK_EQUAL(
      CL_KERNEL_ARG_INFO_NOT_AVAILABLE,
      clGetKernelArgInfo(kernel, 0, CL_KERNEL_ARG_NAME, 0, NULL, &size_ret));
  // just value
  CHECK_EQUAL(CL_KERNEL_ARG_INFO_NOT_AVAILABLE,
              clGetKernelArgInfo(kernel, 0, CL_KERNEL_ARG_NAME,
                                 sizeof(arg_name), arg_name, NULL));

  CHECK_EQUAL(CL_KERNEL_ARG_INFO_NOT_AVAILABLE,
              clGetKernelArgInfo(kernel, 1, CL_KERNEL_ARG_NAME,
                                 sizeof(arg_name), arg_name, &size_ret));

  clReleaseKernel(kernel);
}

TEST(acl_kernel, enqueue_ndrange_max_global_work_dim_zero_kernel) {
  ACL_LOCKED(acl_print_debug_msg("max_global_work_dim = 0 \n"));
  cl_int status;

  status = CL_INVALID_VALUE;
  cl_kernel kernel =
      clCreateKernel(m_program, m_sample_kernel_mgwd_zero_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  cl_mem all_args = clCreateBuffer(m_context, CL_MEM_READ_ONLY, 64, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(all_args)));

  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &all_args));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 1, sizeof(cl_mem), &all_args));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 2, sizeof(cl_mem), &all_args));

  size_t global_size[3] = {192, 192, 64};
  size_t local_size[3] = {64, 64, 8};

  cl_event event;

  cl_uint work_dim = 1;
  CHECK_EQUAL(CL_INVALID_GLOBAL_WORK_SIZE,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));
  global_size[0] = 1;
  CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));
  local_size[0] = 1;
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));

  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));

  work_dim = 2;
  CHECK_EQUAL(CL_INVALID_GLOBAL_WORK_SIZE,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));
  global_size[1] = 1;
  CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));
  local_size[1] = 1;
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));

  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));

  work_dim = 3;
  CHECK_EQUAL(CL_INVALID_GLOBAL_WORK_SIZE,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));
  global_size[2] = 1;
  CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));
  local_size[2] = 1;
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));

  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));

  CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));

  clReleaseMemObject(all_args);
  clReleaseKernel(kernel);
  clReleaseEvent(event);
}

TEST(acl_kernel, enqueue_ndrange_max_global_work_dim_one_kernel) {
  ACL_LOCKED(acl_print_debug_msg("max_global_work_dim = 1 \n"));
  cl_int status;

  status = CL_INVALID_VALUE;
  cl_kernel kernel =
      clCreateKernel(m_program, m_sample_kernel_mgwd_one_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  cl_mem mem_arg = clCreateBuffer(m_context, CL_MEM_READ_ONLY, 64, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(mem_arg)));

  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &mem_arg));

  size_t global_size[3] = {192, 192, 64};
  size_t local_size[3] = {64, 64, 8};

  cl_event event;

  cl_uint work_dim = 1;
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));

  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));

  work_dim = 2;
  CHECK_EQUAL(CL_INVALID_GLOBAL_WORK_SIZE,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));
  global_size[1] = 1;
  CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));
  local_size[1] = 1;
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));

  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));

  work_dim = 3;
  CHECK_EQUAL(CL_INVALID_GLOBAL_WORK_SIZE,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));
  global_size[2] = 1;
  CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));
  local_size[2] = 1;
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));

  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));

  CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));

  clReleaseMemObject(mem_arg);
  clReleaseKernel(kernel);
  clReleaseEvent(event);
}

TEST(acl_kernel, enqueue_ndrange_max_global_work_dim_two_kernel) {
  ACL_LOCKED(acl_print_debug_msg("max_global_work_dim = 2 \n"));
  cl_int status;

  status = CL_INVALID_VALUE;
  cl_kernel kernel =
      clCreateKernel(m_program, m_sample_kernel_mgwd_two_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  cl_mem all_args = clCreateBuffer(m_context, CL_MEM_READ_ONLY, 64, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  ACL_LOCKED(CHECK(acl_mem_is_valid(all_args)));

  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &all_args));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 1, sizeof(cl_mem), &all_args));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 2, sizeof(cl_mem), &all_args));

  size_t global_size[3] = {192, 192, 64};
  size_t local_size[3] = {64, 64, 8};

  cl_event event;

  cl_uint work_dim = 1;
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));

  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));

  work_dim = 2;
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));

  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));

  work_dim = 3;
  CHECK_EQUAL(CL_INVALID_GLOBAL_WORK_SIZE,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));
  global_size[2] = 1;
  CHECK_EQUAL(CL_INVALID_WORK_GROUP_SIZE,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));
  local_size[2] = 1;
  CHECK_EQUAL(CL_SUCCESS,
              clEnqueueNDRangeKernel(m_cq, kernel, work_dim, 0, global_size,
                                     local_size, 0, 0, &event));

  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(
      acl_receive_kernel_update(event->current_device_op->id, CL_COMPLETE));

  CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));

  clReleaseMemObject(all_args);
  clReleaseKernel(kernel);
  clReleaseEvent(event);
}

TEST_GROUP(acl_kernel_reprogram_scheduler) {
  // Preloads a standard platform, and finds all devices.
  enum { MAX_DEVICES = 100, m_num_devices_in_context = 3 };
  void setup() {
    acl_test_setenv("CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA",
                    ACLTEST_DEFAULT_BOARD);
    acl_test_setup_generic_system();
    this->load();

    m_program0 = this->load_program();
    this->build(m_program0);
    m_first_dev_bin = &(m_program0->dev_prog[0]->device_binary);
    CHECK(m_first_dev_bin);

    // With eager loading of the binaries, we should see this right away.
    CHECK_EQUAL(m_first_dev_bin, m_device->last_bin);
    CHECK_EQUAL(m_first_dev_bin, m_device->loaded_bin);

    m_program1 = this->load_program();
    this->build(m_program1);

    // No change.
    CHECK_EQUAL(m_first_dev_bin, m_device->last_bin);
    CHECK_EQUAL(m_first_dev_bin, m_device->loaded_bin);

    // See the example program in acl_test.cpp
    m_sample_task_name[0] = "vecaccum";
    m_sample_task_name[1] = "vecsum";
    m_sample_task_name[2] = "printit";
  }
  void teardown() {
    CHECK_EQUAL(CL_SUCCESS, clReleaseProgram(m_program0));
    CHECK_EQUAL(CL_SUCCESS, clReleaseProgram(m_program1));

    // Never reference a released program!
    CHECK_EQUAL(0, m_device->last_bin);
    CHECK_EQUAL(0, m_device->loaded_bin);

    this->unload();
    // acl_test_unsetenv("CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA");
    acl_test_unsetenv("CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA");
    const char *env = acl_getenv("CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA");
    acl_print_debug_msg(" context offline dev %p %s\n", env, (env ? env : ""));
    acl_test_teardown_generic_system();

    acl_test_run_standard_teardown_checks();
  }

  void load(void) {
    CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &m_platform, 0));
    CHECK(acl_platform_is_valid(m_platform));
    CHECK_EQUAL(CL_SUCCESS,
                clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, MAX_DEVICES,
                               &m_device_arr[0], &m_num_devices));
    m_device = m_device_arr[0];
    CHECK(m_device);

    cl_int status;
    cl_context_properties properties[] = {
        CL_CONTEXT_COMPILER_MODE_INTELFPGA,
        CL_CONTEXT_COMPILER_MODE_OFFLINE_CREATE_EXE_LIBRARY_INTELFPGA, 0, 0};
    m_context = clCreateContext(properties, 1, m_device_arr, NULL, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_context);
    CHECK_EQUAL(1, m_context->saves_and_restores_buffers_for_reprogramming);

    // Pretend that the buffer is on device even though we are using an offline
    // device to force buffer copies during reprogram command.
    m_context->global_mem->is_host_accessible = 0;
    CHECK_EQUAL(0, m_context->global_mem->is_host_accessible);

    m_cq = clCreateCommandQueue(m_context, m_device, CL_QUEUE_PROFILING_ENABLE,
                                &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_cq);

    // Nothing scheduled yet!
    CHECK_EQUAL(0, m_device->last_bin);
    CHECK_EQUAL(0, m_device->loaded_bin);

    acl_dot_push(&m_devlog, &acl_platform.device_op_queue);
  }

  void unload(void) {
    int cq_is_valid, context_is_valid;
    ACL_LOCKED(cq_is_valid = acl_command_queue_is_valid(m_cq));
    if (cq_is_valid) {
      clReleaseCommandQueue(m_cq);
    }
    ACL_LOCKED(context_is_valid = acl_context_is_valid(m_context));
    if (context_is_valid) {
      m_context->global_mem->is_host_accessible = 1;
      clReleaseContext(m_context);
    }
  }

  cl_program load_program() {
    cl_int status;
    cl_program program;

    {
      ACL_LOCKED(acl_print_debug_msg("Loading program for devices:"));
      ACL_LOCKED(
          acl_print_debug_msg(" %d:%s", m_device->id,
                              m_device->def.autodiscovery_def.name.c_str()));
      ACL_LOCKED(acl_print_debug_msg("\n"));
    }
    status = CL_INVALID_VALUE;

    size_t example_bin_len = 0;
    const unsigned char *example_bin =
        acl_test_get_example_binary(&example_bin_len);

    cl_int bin_status = CL_BUILD_NONE;

    program =
        clCreateProgramWithBinary(m_context, 1, &m_device, &example_bin_len,
                                  &example_bin, &bin_status, &status);
    { CHECK_EQUAL(CL_SUCCESS, bin_status); }
    CHECK_EQUAL(CL_SUCCESS, clBuildProgram(program, 1, &m_device, 0, 0, 0));

    CHECK_EQUAL(1, program->num_devices);
    CHECK(program->dev_prog[0]);
    ACL_LOCKED(acl_program_dump_dev_prog(program->dev_prog[0]));

    CHECK_EQUAL(CL_BUILD_SUCCESS, program->dev_prog[0]->build_status);

    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(program);
    ACL_LOCKED(CHECK(acl_program_is_valid(program)));
    return program;
  }
  void build(cl_program program) {
    CHECK_EQUAL(CL_SUCCESS, clBuildProgram(program, 0, 0, "", 0, 0));
  }
  void load_times(cl_event event, cl_ulong times[4]) {
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_QUEUED,
                                        sizeof(times[0]), &times[0], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_SUBMIT,
                                        sizeof(times[1]), &times[1], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START,
                                        sizeof(times[2]), &times[2], 0));
    CHECK_EQUAL(CL_SUCCESS,
                clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END,
                                        sizeof(times[3]), &times[3], 0));
  }

  acl_device_program_info_t *check_dev_prog(cl_program prog) {
    ACL_LOCKED(CHECK(acl_program_is_valid(prog)));
    acl_device_program_info_t *dev_prog = prog->dev_prog[m_device->id];
    CHECK(dev_prog);
    CHECK_EQUAL(m_device, dev_prog->device);
    CHECK_EQUAL(prog, dev_prog->program);
    CHECK_EQUAL(CL_BUILD_SUCCESS, dev_prog->build_status);
    CHECK("" == dev_prog->build_options);
    return dev_prog;
  }

  cl_kernel get_kernel(cl_program prog, unsigned index = 0) {
    cl_int status;
    cl_kernel kernel = clCreateKernel(prog, m_sample_task_name[index], &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_kernel_is_valid(kernel)));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 0, sizeof(cl_mem), 0)); // NULL mem obj
    return kernel;
  }
  cl_event get_user_event(void) {
    cl_int status;
    cl_event event = clCreateUserEvent(m_context, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    ACL_LOCKED(CHECK(acl_event_is_valid(event)));
    CHECK_EQUAL(1, acl_ref_count(event));
    ACL_LOCKED(CHECK(acl_event_is_live(event)));
    return event;
  }

protected:
  cl_platform_id m_platform;
  cl_device_id m_device_arr[MAX_DEVICES];
  cl_device_id m_device;
  cl_uint m_num_devices;
  cl_context m_context;
  cl_command_queue m_cq;
  cl_program m_program0, m_program1;
  const acl_device_binary_t *m_first_dev_bin;

  const char *m_sample_task_name[3];

  acl_device_op_test_ctx_t m_devlog;
};

TEST(acl_kernel_reprogram_scheduler, init) {
  ACL_LOCKED(CHECK(acl_program_is_valid(m_program0)));
  ACL_LOCKED(CHECK(acl_program_is_valid(m_program1)));

  acl_device_program_info_t *dp0 = check_dev_prog(m_program0);
  acl_device_program_info_t *dp1 = check_dev_prog(m_program1);

  CHECK(dp0);
  CHECK(dp1);

  // Nothing scheduled yet.
  CHECK_EQUAL(m_first_dev_bin, m_device->last_bin);
  CHECK_EQUAL(m_first_dev_bin, m_device->loaded_bin);
}

enum { NUM_MEMS_COUNTED = 100 };
static int num_read_mems;
static cl_mem read_mems[NUM_MEMS_COUNTED];
static int num_write_mems;
static cl_mem write_mems[NUM_MEMS_COUNTED];

static void CL_CALLBACK read_mem_callback(cl_mem mem, int after_xfer) {
  acl_print_debug_msg("Read callback on %p %s\n", mem,
                      (after_xfer ? "after" : "before"));
  if (after_xfer) {
    CHECK_EQUAL('r', *(char *)mem->host_mem.aligned_ptr);
  } else {
    memset(mem->block_allocation->range.begin, 'r', mem->size);
    if (num_read_mems + 1 < NUM_MEMS_COUNTED) {
      read_mems[num_read_mems++] = mem;
    }
  }
}
static void CL_CALLBACK write_mem_callback(cl_mem mem, int after_xfer) {
  acl_print_debug_msg("Write callback on %p %s\n", mem,
                      (after_xfer ? "after" : "before"));
  if (after_xfer) {
    CHECK_EQUAL('w', *(char *)mem->block_allocation->range.begin);
  } else {
    memset(mem->host_mem.aligned_ptr, 'w', mem->size);
    if (num_write_mems + 1 < NUM_MEMS_COUNTED) {
      write_mems[num_write_mems++] = mem;
    }
  }
}

// Testing the clGetKernelArgInfo function. The result is expected to be
// available only when program is created with source, but we have the
// information from binary too. This should actually belong to acl_kernel group
// but the load and load_program used in that group won't build a valid non
// empty program to be used for these tests.
TEST(acl_kernel_reprogram_scheduler, kernel_arg_info) {

  cl_kernel k0, k1, k2;
  char arg_name[100], arg_type_name[100];
  size_t size_ret;
  cl_uint ret_value;
  k0 = get_kernel(m_program0, 0);
  k1 = get_kernel(m_program0, 1);
  k2 = get_kernel(m_program0, 2);

  // Bad queries

  // Invalid kernel
  acl_set_allow_invalid_type<cl_kernel>(1);
  CHECK_EQUAL(CL_INVALID_KERNEL,
              clGetKernelArgInfo(0, 0, CL_KERNEL_ARG_NAME, sizeof(arg_name),
                                 arg_name, &size_ret));
  acl_set_allow_invalid_type<cl_kernel>(0);

  // invalid return params.
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetKernelArgInfo(k0, 0, CL_KERNEL_ARG_NAME, sizeof(arg_name),
                                 NULL, &size_ret));

  // Invalid query param.
  CHECK_EQUAL(
      CL_INVALID_VALUE,
      clGetKernelArgInfo(k0, 0, 123456, sizeof(arg_name), arg_name, &size_ret));
  // Return param_value_size too small
  CHECK_EQUAL(CL_INVALID_VALUE, clGetKernelArgInfo(k0, 0, CL_KERNEL_ARG_NAME, 1,
                                                   arg_name, &size_ret));
  // Invalid kernel arg index.
  CHECK_EQUAL(CL_INVALID_ARG_INDEX,
              clGetKernelArgInfo(k0, 2, CL_KERNEL_ARG_NAME, sizeof(arg_name),
                                 arg_name, &size_ret));
  CHECK_EQUAL(CL_INVALID_ARG_INDEX,
              clGetKernelArgInfo(k2, 1, CL_KERNEL_ARG_TYPE_NAME,
                                 sizeof(arg_name), arg_name, &size_ret));
  CHECK_EQUAL(CL_INVALID_ARG_INDEX,
              clGetKernelArgInfo(k2, 2, CL_KERNEL_ARG_ADDRESS_QUALIFIER,
                                 sizeof(arg_name), arg_name, &size_ret));

  // Good queries

  // just size.
  CHECK_EQUAL(CL_SUCCESS, clGetKernelArgInfo(k0, 0, CL_KERNEL_ARG_NAME, 0, NULL,
                                             &size_ret));
  CHECK_EQUAL(sizeof(char) * 2,
              size_ret); // the kernel name is "A". Length is 1+1

  // just value
  memset(arg_name, 0, sizeof(arg_name));
  arg_name[size_ret] =
      10; // marking the last byte to make sure it is not affected by the call.
  CHECK_EQUAL(CL_SUCCESS, clGetKernelArgInfo(k0, 0, CL_KERNEL_ARG_NAME,
                                             sizeof(arg_name), arg_name, NULL));
  CHECK_EQUAL(10, arg_name[size_ret]); // the kernel name is "A".
  CHECK(!strcmp(arg_name, "A"));       // the kernel arg_name is "A".

  CHECK_EQUAL(CL_SUCCESS,
              clGetKernelArgInfo(k0, 1, CL_KERNEL_ARG_NAME, sizeof(arg_name),
                                 arg_name, &size_ret));
  CHECK(!strcmp(arg_name, "B")); // the kernel arg_name is "B".

  CHECK_EQUAL(CL_SUCCESS,
              clGetKernelArgInfo(k1, 0, CL_KERNEL_ARG_ADDRESS_QUALIFIER,
                                 sizeof(ret_value), &ret_value, &size_ret));
  CHECK_EQUAL(CL_KERNEL_ARG_ADDRESS_GLOBAL, ret_value);
  CHECK_EQUAL(CL_SUCCESS,
              clGetKernelArgInfo(k1, 0, CL_KERNEL_ARG_TYPE_QUALIFIER,
                                 sizeof(ret_value), &ret_value, &size_ret));
  CHECK_EQUAL(sizeof(cl_uint), size_ret);
  CHECK_EQUAL(CL_KERNEL_ARG_TYPE_NONE, ret_value);

  CHECK_EQUAL(CL_SUCCESS,
              clGetKernelArgInfo(k1, 1, CL_KERNEL_ARG_ADDRESS_QUALIFIER,
                                 sizeof(ret_value), &ret_value, &size_ret));
  CHECK_EQUAL(CL_KERNEL_ARG_ADDRESS_CONSTANT, ret_value);
  CHECK_EQUAL(CL_SUCCESS,
              clGetKernelArgInfo(k1, 1, CL_KERNEL_ARG_TYPE_QUALIFIER,
                                 sizeof(ret_value), &ret_value, &size_ret));
  CHECK_EQUAL(CL_KERNEL_ARG_TYPE_CONST, ret_value);

  CHECK_EQUAL(CL_SUCCESS,
              clGetKernelArgInfo(k2, 0, CL_KERNEL_ARG_ACCESS_QUALIFIER,
                                 sizeof(ret_value), &ret_value, &size_ret));
  CHECK_EQUAL(CL_KERNEL_ARG_ACCESS_NONE, ret_value);

  CHECK_EQUAL(CL_SUCCESS, clGetKernelArgInfo(k2, 0, CL_KERNEL_ARG_TYPE_NAME,
                                             sizeof(arg_type_name),
                                             arg_type_name, &size_ret));
  CHECK(!strcmp(arg_type_name, "int*"));

  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(k0));
  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(k1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(k2));
}

TEST(acl_kernel_reprogram_scheduler, release_and_reprogram) {
  // This test will make sure that we can still issue kernel opeartions
  // even after we release a previously used program
  cl_program program0 = this->load_program();
  this->build(program0);
  cl_program program1 = this->load_program();
  this->build(program1);
  cl_kernel k0 = get_kernel(program0);
  cl_kernel k1 = get_kernel(program1);
  CHECK(program0 != program1);

  cl_int status = CL_INVALID_VALUE;
  cl_mem mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE, 2048, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(mem);

  cl_event k_e = NULL;
  CHECK(k0->program->device[0]->last_bin != NULL); // Important check
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k0, 0, sizeof(cl_mem), &mem));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k0, 1, sizeof(cl_mem), &mem));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, k0, 0, NULL, &k_e));
  CHECK(k_e != NULL);

  ACL_LOCKED(acl_receive_kernel_update(k_e->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));
  ACL_LOCKED(
      acl_receive_kernel_update(k_e->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(k_e));
  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(k0));
  CHECK_EQUAL(CL_SUCCESS, clReleaseProgram(program0));

  CHECK(k1->program->device[0]->last_bin == NULL); // Important check
  k_e = NULL;
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k1, 0, sizeof(cl_mem), &mem));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k1, 1, sizeof(cl_mem), &mem));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, k1, 0, NULL, &k_e));
  CHECK(k_e != NULL);

  ACL_LOCKED(acl_receive_kernel_update(k_e->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));
  ACL_LOCKED(
      acl_receive_kernel_update(k_e->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(k_e));
  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(k1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseProgram(program1));
}

TEST(acl_kernel_reprogram_scheduler, require_reprogram) {
  // In this test, we have an existing command queue, and schedule a task
  // on it.
  acl_device_program_info_t *dp0 = check_dev_prog(m_program0);

  m_context->reprogram_buf_read_callback = read_mem_callback;
  m_context->reprogram_buf_write_callback = write_mem_callback;

  // A device side buffer
  cl_int status = CL_INVALID_VALUE;
  cl_mem mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE, 2048, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(mem);
  memset(mem->host_mem.aligned_ptr, 'X', mem->size);
  memset(mem->block_allocation->range.begin, 'x', mem->size);

  CHECK_EQUAL(1, m_context->device_buffers_have_backing_store);
  CHECK_EQUAL(0, mem->block_allocation->region->is_host_accessible);
  CHECK_EQUAL(0, mem->writable_copy_on_host);

  cl_kernel k = get_kernel(m_program0);

  // Just the initial program load.
  CHECK_EQUAL(m_first_dev_bin, m_device->last_bin);
  CHECK_EQUAL(m_first_dev_bin, m_device->loaded_bin);

  cl_event ue = get_user_event();
  cl_event k_e = 0;

  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k, 0, sizeof(cl_mem), &mem));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k, 1, sizeof(cl_mem), &mem));

  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, k, 1, &ue, &k_e));

  CHECK_EQUAL(CL_COMMAND_TASK, k_e->cmd.type);

  // Only initial programming has occurred.
  // Has 3 transitions logged: SUBMITTED, RUNNING, COMPLETE
  CHECK_EQUAL(3, m_devlog.num_ops);
  CHECK_EQUAL(m_first_dev_bin, m_device->last_bin);
  CHECK_EQUAL(m_first_dev_bin, m_device->loaded_bin);

  acl_print_debug_msg("Forcing user event completion\n");
  CHECK_EQUAL(CL_SUCCESS, clSetUserEventStatus(ue, CL_COMPLETE));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(ue));

  // Should have recorded that we loaded the program.
  CHECK_EQUAL(&(dp0->device_binary), m_device->last_bin);
  CHECK_EQUAL(&(dp0->device_binary), m_device->loaded_bin);

  // And executed the right device operations. (Four of these are mem migration)
  CHECK_EQUAL(8, m_devlog.num_ops);
  const acl_device_op_t *op0submit = &(m_devlog.before[0]);
  const acl_device_op_t *op0running = &(m_devlog.before[1]);
  const acl_device_op_t *op0complete = &(m_devlog.before[2]);

  // Initial eager programming.
  CHECK_EQUAL(ACL_DEVICE_OP_REPROGRAM, op0submit->info.type);
  CHECK_EQUAL(0, op0submit->id);
  CHECK(op0submit->info.event);
  CHECK_EQUAL(CL_SUBMITTED, op0submit->status);
  CHECK_EQUAL(0, op0submit->info.num_printf_bytes_pending);
  CHECK_EQUAL(1, op0submit->first_in_group);
  CHECK_EQUAL(1, op0submit->last_in_group);

  CHECK_EQUAL(ACL_DEVICE_OP_REPROGRAM, op0running->info.type);
  CHECK_EQUAL(0, op0running->id);
  CHECK(op0running->info.event);
  CHECK_EQUAL(CL_RUNNING, op0running->status);
  CHECK_EQUAL(0, op0running->info.num_printf_bytes_pending);
  CHECK_EQUAL(1, op0running->first_in_group);
  CHECK_EQUAL(1, op0running->last_in_group);

  CHECK_EQUAL(ACL_DEVICE_OP_REPROGRAM, op0complete->info.type);
  CHECK_EQUAL(0, op0complete->id);
  CHECK(op0complete->info.event);
  CHECK_EQUAL(CL_COMPLETE, op0complete->status);
  CHECK_EQUAL(0, op0complete->info.num_printf_bytes_pending);
  CHECK_EQUAL(1, op0complete->first_in_group);
  CHECK_EQUAL(1, op0complete->last_in_group);

  // The device is still programmed with the same program.
  CHECK_EQUAL(&(dp0->device_binary), m_device->last_bin);
  CHECK_EQUAL(&(dp0->device_binary), m_device->loaded_bin);

  const acl_device_op_t *op1submit = &(m_devlog.before[7]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op1submit->info.type);
  CHECK_EQUAL(k_e, op1submit->info.event);
  CHECK_EQUAL(CL_SUBMITTED, op1submit->status);
  CHECK_EQUAL(0, op1submit->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, op1submit->first_in_group); // mem migration is first
  CHECK_EQUAL(1, op1submit->last_in_group);

  // The user-level event is linked to the kernel device op now.
  CHECK_EQUAL(op1submit->id, k_e->current_device_op->id);

  // Pretend to start the kernel
  acl_print_debug_msg("Say kernel is running\n");
  ACL_LOCKED(acl_receive_kernel_update(k_e->current_device_op->id, CL_RUNNING));
  CHECK_EQUAL(CL_RUNNING, k_e->current_device_op->execution_status);

  ACL_LOCKED(acl_idle_update(m_context));

  // Now we have a "running" transition
  CHECK_EQUAL(9, m_devlog.num_ops);
  const acl_device_op_t *op2a = &(m_devlog.after[8]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op2a->info.type);
  CHECK_EQUAL(k_e, op2a->info.event);
  CHECK_EQUAL(CL_RUNNING, op2a->status);
  CHECK_EQUAL(0, op2a->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, op2a->first_in_group);
  CHECK_EQUAL(1, op2a->last_in_group);

  // The running status was propagated up to the user-level event.
  CHECK_EQUAL(CL_RUNNING, k_e->execution_status);

  acl_print_debug_msg("Say kernel is complete\n");
  ACL_LOCKED(
      acl_receive_kernel_update(k_e->current_device_op->id, CL_COMPLETE));
  CHECK_EQUAL(CL_COMPLETE, k_e->current_device_op->execution_status);

  ACL_LOCKED(acl_idle_update(m_context));
  // Now we have a "complete" transition
  CHECK_EQUAL(10, m_devlog.num_ops);
  const acl_device_op_t *op3a = &(m_devlog.after[9]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op3a->info.type);
  CHECK_EQUAL(k_e, op3a->info.event);
  CHECK_EQUAL(CL_COMPLETE, op3a->status);
  CHECK_EQUAL(0, op3a->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, op3a->first_in_group);
  CHECK_EQUAL(1, op3a->last_in_group);

  // Completion timestamp has propagated up to the user level event.
  CHECK_EQUAL(acl_platform.device_op_queue.op[op3a->id].timestamp[CL_COMPLETE],
              k_e->timestamp[CL_COMPLETE]);

  // Completion wipes out the downlink.
  CHECK_EQUAL(0, k_e->current_device_op);

  // And let go.
  // (Don't check for CL_INVALID_EVENT on a second release of each of
  // these events because the events might be reused.)
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(k_e));
  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(k));
  acl_print_debug_msg("DONE!\n");
}

TEST(acl_kernel_reprogram_scheduler, switch_prog) {
  // Although these programs are different pointers they are the same source
  acl_device_program_info_t *dp0 = check_dev_prog(m_program0);
  acl_device_program_info_t *dp1 = check_dev_prog(m_program1);
  // Changing to different hash value for second program
  dp1->device_binary.get_devdef().autodiscovery_def.binary_rand_hash =
      "sample40byterandomhash000000000000000000";

  cl_kernel k0 = get_kernel(m_program0);
  cl_kernel k1 = get_kernel(m_program1);
  int offset = m_devlog.num_ops;

  cl_mem mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE, 2048, 0, 0);
  CHECK(mem);
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k0, 0, sizeof(cl_mem), &mem));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k0, 1, sizeof(cl_mem), &mem));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k1, 0, sizeof(cl_mem), &mem));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k1, 1, sizeof(cl_mem), &mem));

  // Nothing scheduled yet.
  CHECK_EQUAL(m_first_dev_bin, m_device->last_bin);
  CHECK_EQUAL(m_first_dev_bin, m_device->loaded_bin);

  m_context->reprogram_buf_read_callback = read_mem_callback;
  m_context->reprogram_buf_write_callback = write_mem_callback;

  cl_event ue = get_user_event();
  cl_event k0_e = 0;
  cl_event k1_e = 0;

  // Schedule the first kernel.
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, k0, 1, &ue, &k0_e));
  CHECK_EQUAL(CL_COMMAND_TASK, k0_e->cmd.type);

  // Nothing happened
  CHECK_EQUAL(offset, m_devlog.num_ops);
  CHECK_EQUAL(m_first_dev_bin, m_device->last_bin);
  CHECK_EQUAL(m_first_dev_bin, m_device->loaded_bin);

  // This is the initial eager programming of the device.
  const acl_device_op_t *op0 = &(m_devlog.after[0]);
  op0->info.event->context->programs_devices = 1;

  CHECK_EQUAL(ACL_DEVICE_OP_REPROGRAM, op0->info.type);
  CHECK(op0->info.event); // The eager programming uses a different event!
  CHECK_EQUAL(CL_SUBMITTED, op0->status); // submitted
  CHECK_EQUAL(1, op0->first_in_group);
  CHECK_EQUAL(1, op0->last_in_group);

  // Now schedule a kernel from the other program.
  // No explicit dependencies.
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, k1, 0, 0, &k1_e));
  CHECK_EQUAL(CL_COMMAND_TASK, k1_e->cmd.type);

  // Still just the early programming was scheduled on the device.
  CHECK_EQUAL(offset, m_devlog.num_ops);
  CHECK_EQUAL(m_first_dev_bin, m_device->last_bin);
  CHECK_EQUAL(m_first_dev_bin, m_device->loaded_bin);

  // Complete the user event, which should program the device, and launch
  // the first kernel.
  acl_print_debug_msg("Forcing user event completion\n");
  CHECK_EQUAL(CL_SUCCESS, clSetUserEventStatus(ue, CL_COMPLETE));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(ue));
  ACL_LOCKED(acl_idle_update(m_context));

  // set MEM_MIGRATE1.1 to RUNNING +
  // set MEM_MIGRATE1.1 to COMPLETE +
  // set MEM_MIGRATE1.2 to RUNNING +
  // set MEM_MIGRATE1.2 to COMPLETE +
  // submit KERNEL1 to device = 5
  CHECK_EQUAL(offset + 5, m_devlog.num_ops);
  CHECK_EQUAL(&(dp0->device_binary), m_device->loaded_bin);
  CHECK_EQUAL(&(dp1->device_binary), m_device->last_bin);

  const acl_device_op_t *op1 = &(m_devlog.after[1]);
  CHECK_EQUAL(ACL_DEVICE_OP_REPROGRAM, op1->info.type);
  CHECK_EQUAL(CL_RUNNING, op1->status); // running
  CHECK_EQUAL(1, op1->first_in_group);
  CHECK_EQUAL(1, op1->last_in_group);

  const acl_device_op_t *op2 = &(m_devlog.after[2]);
  CHECK_EQUAL(ACL_DEVICE_OP_REPROGRAM, op2->info.type);
  CHECK_EQUAL(CL_COMPLETE, op2->status); // complete
  CHECK_EQUAL(1, op2->first_in_group);
  CHECK_EQUAL(1, op2->last_in_group);

  // submitted kernel
  const acl_device_op_t *op5 =
      &(m_devlog.after[7]); // log entries 3 and 4,5,6 are the mem migration
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op5->info.type);
  CHECK_EQUAL(k0_e, op5->info.event);
  CHECK_EQUAL(CL_SUBMITTED, op5->status);
  CHECK_EQUAL(0, op5->first_in_group); // mem migration is the first in the
                                       // group
  CHECK_EQUAL(1, op5->last_in_group);

  ACL_LOCKED(
      acl_receive_kernel_update(k0_e->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(
      acl_receive_kernel_update(k0_e->current_device_op->id, CL_COMPLETE));

  // Count mem copies.
  num_read_mems = 0;
  num_write_mems = 0;

  // Force the schedule update!
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL1 to RUNNING +
  // set KERNEL1 to COMPLETE +
  // submit REPROGRAM to device +
  // set REPROGRAM to RUNNING +
  // set REPROGRAM to COMPLETE +
  // set MEM_MIGRATE2.1 to RUNNING +
  // set MEM_MIGRATE2.1 to COMPLETE +
  // set MEM_MIGRATE2.2 to RUNNING +
  // set MEM_MIGRATE2.2 to COMPLETE +
  // submit KERNEL2 to device = 10
  CHECK_EQUAL(offset + 15, m_devlog.num_ops);

  // Should have copied the memory over.
  acl_print_debug_msg(" num_write_mems %d\n", num_write_mems);

  CHECK(num_write_mems == 0);
  CHECK(num_read_mems == 0);

  // reprogram it again to test the memory non-preserved programming
  acl_test_setenv("ACL_PCIE_USE_JTAG_PROGRAMMING", "1");

  ACL_LOCKED(
      acl_receive_kernel_update(k1_e->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(
      acl_receive_kernel_update(k1_e->current_device_op->id, CL_COMPLETE));
  // Force the schedule update!
  ACL_LOCKED(acl_idle_update(m_context));

  // set KERNEL1 to RUNNING +
  // set KERNEL1 to COMPLETE = 2
  CHECK_EQUAL(offset + 17, m_devlog.num_ops);
  CHECK_EQUAL(&(dp1->device_binary), m_device->last_bin);
  CHECK_EQUAL(&(dp1->device_binary), m_device->loaded_bin);
  CHECK_EQUAL(k0_e->current_device_op, CL_COMPLETE);
  CHECK_EQUAL(k1_e->current_device_op, CL_COMPLETE);

  CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));

  // reset the events
  ue = get_user_event();
  k0_e = 0;
  k1_e = 0;
  offset = m_devlog.num_ops;

  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, k0, 1, &ue, &k0_e));
  CHECK_EQUAL(CL_COMMAND_TASK, k0_e->cmd.type);

  // This is the initial eager programming of the device.
  CHECK_EQUAL(ACL_DEVICE_OP_REPROGRAM, op0->info.type);
  CHECK(op0->info.event); // The eager programming uses a different event!
  CHECK_EQUAL(CL_SUBMITTED, op0->status); // submitted
  CHECK_EQUAL(1, op0->first_in_group);
  CHECK_EQUAL(1, op0->last_in_group);

  // Now schedule a kernel from the other program.
  // No explicit dependencies.
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, k1, 0, 0, &k1_e));
  CHECK_EQUAL(CL_COMMAND_TASK, k1_e->cmd.type);

  // Nothing happening blocked by user event
  CHECK_EQUAL(offset, m_devlog.num_ops);
  CHECK_EQUAL(&(dp1->device_binary), m_device->last_bin);
  CHECK_EQUAL(&(dp1->device_binary), m_device->loaded_bin);

  // Complete the user event, which should program the device, and launch
  // the first kernel.
  acl_print_debug_msg("Forcing user event completion\n");
  CHECK_EQUAL(CL_SUCCESS, clSetUserEventStatus(ue, CL_COMPLETE));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(ue));

  // submit REPROGRAM to device +
  // set REPROGRAM to RUNNING +
  // set REPROGRAM to COMPLETE +
  // set MEM_MIGRATE1.1 to RUNNING +
  // set MEM_MIGRATE1.1 to COMPLETE +
  // set MEM_MIGRATE1.2 to RUNNING +
  // set MEM_MIGRATE1.2 to COMPLETE +
  // submit KERNEL1 to device = 8
  CHECK_EQUAL(offset + 8, m_devlog.num_ops);

  CHECK_EQUAL(&(dp0->device_binary), m_device->loaded_bin);
  CHECK_EQUAL(&(dp1->device_binary), m_device->last_bin);

  CHECK_EQUAL(ACL_DEVICE_OP_REPROGRAM, op1->info.type);
  CHECK_EQUAL(CL_RUNNING, op1->status); // running
  CHECK_EQUAL(1, op1->first_in_group);
  CHECK_EQUAL(1, op1->last_in_group);

  CHECK_EQUAL(ACL_DEVICE_OP_REPROGRAM, op2->info.type);
  CHECK_EQUAL(CL_COMPLETE, op2->status); // complete
  CHECK_EQUAL(1, op2->first_in_group);
  CHECK_EQUAL(1, op2->last_in_group);

  // submitted kernel
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op5->info.type);
  CHECK_EQUAL(CL_SUBMITTED, op5->status);
  CHECK_EQUAL(0, op5->first_in_group); // mem migration is the frist in the
                                       // group
  CHECK_EQUAL(1, op5->last_in_group);

  ACL_LOCKED(
      acl_receive_kernel_update(k0_e->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(
      acl_receive_kernel_update(k0_e->current_device_op->id, CL_COMPLETE));

  // Force the schedule update!
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL1 to RUNNING +
  // set KERNEL1 to COMPLETE +
  // submit REPROGRAM to device +
  // set REPROGRAM to RUNNING +
  // set REPROGRAM to COMPLETE +
  // set MEM_MIGRATE2.1 to RUNNING +
  // set MEM_MIGRATE2.1 to COMPLETE +
  // set MEM_MIGRATE2.2 to RUNNING +
  // set MEM_MIGRATE2.2 to COMPLETE +
  // submit KERNEL2 to device = 10
  CHECK_EQUAL(offset + 18, m_devlog.num_ops);

  // Should have copied the memory over.
  acl_print_debug_msg(" num_write_mems %d\n", num_write_mems);

  CHECK(num_write_mems > 0);
  CHECK(num_write_mems < NUM_MEMS_COUNTED);
  CHECK(num_read_mems > 0);
  CHECK(num_read_mems < NUM_MEMS_COUNTED);

  acl_test_unsetenv("ACL_PCIE_USE_JTAG_PROGRAMMING");

  op0->info.event->context->programs_devices = 0;

  CHECK_EQUAL(&(dp1->device_binary), m_device->last_bin);
  CHECK_EQUAL(&(dp1->device_binary), m_device->loaded_bin);

  {
    // Make sure the commands we expect are actually getting executed
    acl_device_op_t *op;
    const int expectedNumOps = 10;
    offset = m_devlog.num_ops;
    // commands executed during the last idle_update() [10 in total]
    acl_device_op_type_t expectedType[] = {
        ACL_DEVICE_OP_KERNEL,        ACL_DEVICE_OP_KERNEL,
        ACL_DEVICE_OP_REPROGRAM,     ACL_DEVICE_OP_REPROGRAM,
        ACL_DEVICE_OP_REPROGRAM,     ACL_DEVICE_OP_MEM_MIGRATION,
        ACL_DEVICE_OP_MEM_MIGRATION, ACL_DEVICE_OP_MEM_MIGRATION,
        ACL_DEVICE_OP_MEM_MIGRATION, ACL_DEVICE_OP_KERNEL};
    cl_int expectedStatus[] = {
        CL_RUNNING, CL_COMPLETE, CL_SUBMITTED, CL_RUNNING,  CL_COMPLETE,
        CL_RUNNING, CL_COMPLETE, CL_RUNNING,   CL_COMPLETE, CL_SUBMITTED};
    int expectedFirstInGroup[] = {0, 0, 1, 1, 1, 0, 0, 0, 0, 0};
    int expectedLastInGroup[] = {1, 1, 0, 0, 0, 0, 0, 0, 0, 1};
    cl_event expectedEvent[] = {k0_e, k0_e, k1_e, k1_e, k1_e,
                                k1_e, k1_e, k1_e, k1_e, k1_e};

    for (int i = 0; i < expectedNumOps; i++) {
      op = &(m_devlog.after[offset - expectedNumOps + i]);
      CHECK_EQUAL(expectedType[i], op->info.type);
      if (expectedEvent[i] != NULL) {
        CHECK_EQUAL(expectedEvent[i], op->info.event);
      }
      CHECK_EQUAL(expectedStatus[i], op->status);
      CHECK_EQUAL(expectedFirstInGroup[i], op->first_in_group);
      CHECK_EQUAL(expectedLastInGroup[i], op->last_in_group);
    }

    ACL_LOCKED(
        acl_receive_kernel_update(k1_e->current_device_op->id, CL_RUNNING));
    ACL_LOCKED(acl_idle_update(m_context));

    // set KERNEL1 to RUNNING = 1
    CHECK_EQUAL(offset + 1, m_devlog.num_ops);
    op = &(m_devlog.after[offset + 1 - 1]);
    CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op->info.type);
    CHECK_EQUAL(k1_e, op->info.event);
    CHECK_EQUAL(CL_RUNNING, op->status);
    CHECK_EQUAL(0, op->first_in_group);
    CHECK_EQUAL(1, op->last_in_group);

    ACL_LOCKED(
        acl_receive_kernel_update(k1_e->current_device_op->id, CL_COMPLETE));
    ACL_LOCKED(acl_idle_update(m_context));

    // set KERNEL1 to COMPLETE = 1
    CHECK_EQUAL(offset + 2, m_devlog.num_ops);
    op = &(m_devlog.after[offset + 2 - 1]);

    CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op->info.type);
    CHECK_EQUAL(k1_e, op->info.event);
    CHECK_EQUAL(CL_COMPLETE, op->status);
    CHECK_EQUAL(0, op->first_in_group);
    CHECK_EQUAL(1, op->last_in_group);
  }

  // Create another command queue for the device, and schedule a task from
  // m_program0 onto it.
  // Complex because the first program command doesn't even see this.
  offset = m_devlog.num_ops;
  cl_int status = CL_INVALID_CONTEXT;
  cl_command_queue cq2 = clCreateCommandQueue(
      m_context, m_device, CL_QUEUE_PROFILING_ENABLE, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  cl_event k2_e = 0;
  status = CL_INVALID_CONTEXT;
  cl_event ue2 = clCreateUserEvent(m_context, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(cq2, k0, 1, &ue2, &k2_e));

  CHECK_EQUAL(&(dp1->device_binary), m_device->last_bin);
  CHECK_EQUAL(&(dp1->device_binary), m_device->loaded_bin);

  // Nothing additional scheduled yet.
  CHECK_EQUAL(offset, m_devlog.num_ops);

  // complete the user event.
  CHECK_EQUAL(CL_SUCCESS, clSetUserEventStatus(ue2, CL_COMPLETE));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(ue2));

  {
    acl_device_op_t *op;
    int expectedNumOps = 8;
    acl_device_op_type_t expectedType[] = {
        ACL_DEVICE_OP_REPROGRAM,     ACL_DEVICE_OP_REPROGRAM,
        ACL_DEVICE_OP_REPROGRAM,     ACL_DEVICE_OP_MEM_MIGRATION,
        ACL_DEVICE_OP_MEM_MIGRATION, ACL_DEVICE_OP_MEM_MIGRATION,
        ACL_DEVICE_OP_MEM_MIGRATION, ACL_DEVICE_OP_KERNEL};
    cl_int expectedStatus[] = {CL_SUBMITTED, CL_RUNNING,  CL_COMPLETE,
                               CL_RUNNING,   CL_COMPLETE, CL_RUNNING,
                               CL_COMPLETE,  CL_SUBMITTED};
    int expectedFirstInGroup[] = {1, 1, 1, 0, 0, 0, 0, 0};
    int expectedLastInGroup[] = {0, 0, 0, 0, 0, 0, 0, 1};
    cl_event expectedEvent[] = {k2_e, k2_e, k2_e, k2_e, k2_e, k2_e, k2_e, k2_e};

    ACL_LOCKED(acl_idle_update(m_context));

    CHECK_EQUAL(offset + expectedNumOps, m_devlog.num_ops);

    for (int i = 0; i < expectedNumOps; i++) {
      op = &(m_devlog.after[i + offset]);
      CHECK_EQUAL(expectedType[i], op->info.type);
      if (expectedEvent[i] != NULL) {
        CHECK_EQUAL(expectedEvent[i], op->info.event);
      }
      CHECK_EQUAL(expectedStatus[i], op->status);
      CHECK_EQUAL(expectedFirstInGroup[i], op->first_in_group);
      CHECK_EQUAL(expectedLastInGroup[i], op->last_in_group);
    }

    // Pretend to complete the third kernel
    acl_print_debug_msg("Forcing kernel2 running\n");
    ACL_LOCKED(
        acl_receive_kernel_update(k2_e->current_device_op->id, CL_RUNNING));
    ACL_LOCKED(acl_idle_update(m_context));

    expectedNumOps++;
    CHECK_EQUAL(offset + expectedNumOps, m_devlog.num_ops);

    op = &(m_devlog.after[offset + expectedNumOps - 1]);
    CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op->info.type);
    CHECK_EQUAL(k2_e, op->info.event);
    CHECK_EQUAL(CL_RUNNING, op->status);
    CHECK_EQUAL(0, op->first_in_group);
    CHECK_EQUAL(1, op->last_in_group);

    ACL_LOCKED(
        acl_receive_kernel_update(k2_e->current_device_op->id, CL_COMPLETE));
    ACL_LOCKED(acl_idle_update(m_context));

    expectedNumOps++;
    CHECK_EQUAL(offset + expectedNumOps, m_devlog.num_ops);

    op = &(m_devlog.after[offset + expectedNumOps - 1]);
    CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op->info.type);
    CHECK_EQUAL(k2_e, op->info.event);
    CHECK_EQUAL(CL_COMPLETE, op->status);
    CHECK_EQUAL(0, op->first_in_group);
    CHECK_EQUAL(1, op->last_in_group);
  }

  // Set up a scenario where the device_op_queue looks like this
  // Group | Device_op (AOCX)
  //   3     Kernel(1)
  //   3     Reprogram(1)      <-- Make sure this is not missing
  //   2     Kernel(2)
  //   2     Reprogram(2)
  //   1     Kernel(1)
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(k0_e));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(k1_e));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(k2_e));

  offset = m_devlog.num_ops;
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, k0, 0, NULL, &k0_e));
  // Note: no reprogram as we are using the same kernel that ran last time
  // set MEM_MIGRATE1.1 to RUNNING +
  // set MEM_MIGRATE1.1 to COMPLETE +
  // set MEM_MIGRATE1.2 to RUNNING +
  // set MEM_MIGRATE1.2 to COMPLETE +
  // submit KERNEL1 to device = 5
  CHECK_EQUAL(offset + 5, m_devlog.num_ops);

  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(cq2, k1, 0, NULL, &k1_e));
  // Nothing should change as the reprograms cant execute while KERNEL1 is
  // submitted
  CHECK_EQUAL(offset + 5, m_devlog.num_ops);
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, k0, 0, NULL, &k2_e));
  // Nothing should change as the reprograms cant execute while KERNEL1 is
  // submitted
  CHECK_EQUAL(offset + 5, m_devlog.num_ops);
  // At this point all three events should be in the device_op_queue

  ACL_LOCKED(
      acl_receive_kernel_update(k0_e->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL1 to RUNNING = 1
  // still can't execute reprogram
  CHECK_EQUAL(offset + 6, m_devlog.num_ops);

  ACL_LOCKED(
      acl_receive_kernel_update(k0_e->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL1 to COMPLETE +
  // submit REPROGRAM2 to device +
  // set REPROGRAM2 to RUNNING +
  // set REPROGRAM2 to COMPLETE +
  // set MEM_MIGRATE2.1 to RUNNING +
  // set MEM_MIGRATE2.1 to COMPLETE +
  // set MEM_MIGRATE2.2 to RUNNING +
  // set MEM_MIGRATE2.2 to COMPLETE +
  // submit KERNEL2 to device = 9
  CHECK_EQUAL(offset + 15, m_devlog.num_ops);

  ACL_LOCKED(
      acl_receive_kernel_update(k1_e->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL1 to RUNNING = 1
  CHECK_EQUAL(offset + 16, m_devlog.num_ops);

  ACL_LOCKED(
      acl_receive_kernel_update(k1_e->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL2 to COMPLETE +
  // submit REPROGRAM1 to device +  <---This is the important one, must not be
  // missing set REPROGRAM1 to RUNNING + set REPROGRAM1 to COMPLETE + set
  // MEM_MIGRATE1.1 to RUNNING + set MEM_MIGRATE1.1 to COMPLETE + set
  // MEM_MIGRATE1.2 to RUNNING + set MEM_MIGRATE1.2 to COMPLETE + submit KERNEL1
  // to device = 9
  CHECK_EQUAL(offset + 25, m_devlog.num_ops);

  ACL_LOCKED(
      acl_receive_kernel_update(k2_e->current_device_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));
  ACL_LOCKED(
      acl_receive_kernel_update(k2_e->current_device_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));
  // set KERNEL1 to RUNNING +
  // set KERNEL1 to COMPLETE = 2
  CHECK_EQUAL(offset + 27, m_devlog.num_ops);

  acl_print_debug_msg("Finish m_cq\n");
  CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));

  // And let go.
  // (Don't check for CL_INVALID_EVENT on a second release of each of
  // these events because the events might be reused.)
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(k0_e));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(k1_e));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(k2_e));
  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(k0));
  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(k1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseCommandQueue(cq2));
}

TEST(acl_kernel_reprogram_scheduler, device_global_reprogram) {
  // In this test, we will force the device to contain reprogram
  // device global. The device will be first reprogrammed eagerly
  // due to the clCreateProgramWithBinary call which will set the
  // last_bin and loaded_bin. We revert that by setting them to
  // null again to emulate a hw device with binary on the board
  // but not yet reprogrammed in execution.
  // The kernel will be launched two times, the first time should
  // trigger a reprogram even thought the random hash matches due
  // to the device global, the second time shouldn't as the device
  // has been reprogrammed in the execution.

  // Force device to contain device global
  m_device->def.autodiscovery_def.device_global_mem_defs.insert(
      {"dev_glob1",
       {/* address */ 1024,
        /* size */ 1024,
        /* host_access */ ACL_DEVICE_GLOBAL_HOST_ACCESS_READ_WRITE,
        /* init_mode */ ACL_DEVICE_GLOBAL_INIT_MODE_REPROGRAM,
        /* implement_in_csr */ false}});

  // Initial eager reprogram
  int offset = m_devlog.num_ops;
  CHECK_EQUAL(3, offset);
  // Just the initial program load.
  CHECK_EQUAL(m_first_dev_bin, m_device->last_bin);
  CHECK_EQUAL(m_first_dev_bin, m_device->loaded_bin);

  // Pretend execution starts now
  m_device->last_bin->unload_content();
  m_device->last_bin = NULL;
  m_device->loaded_bin->unload_content();
  m_device->loaded_bin = NULL;

  acl_device_program_info_t *dp0 = check_dev_prog(m_program0);
  m_context->reprogram_buf_read_callback = read_mem_callback;
  m_context->reprogram_buf_write_callback = write_mem_callback;

  // A device side buffer
  cl_int status = CL_INVALID_VALUE;
  cl_mem mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE, 2048, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(mem);
  memset(mem->host_mem.aligned_ptr, 'X', mem->size);
  memset(mem->block_allocation->range.begin, 'x', mem->size);

  CHECK_EQUAL(1, m_context->device_buffers_have_backing_store);
  CHECK_EQUAL(0, mem->block_allocation->region->is_host_accessible);
  CHECK_EQUAL(0, mem->writable_copy_on_host);

  cl_kernel k = get_kernel(m_program0);
  cl_event ue1 = get_user_event();
  cl_event ue2 = get_user_event();
  cl_event k_e1 = 0;
  cl_event k_e2 = 0;

  // Launch the kernel for the first time
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k, 0, sizeof(cl_mem), &mem));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k, 1, sizeof(cl_mem), &mem));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, k, 1, &ue1, &k_e1));
  CHECK_EQUAL(CL_COMMAND_TASK, k_e1->cmd.type);
  CHECK(m_device->def.autodiscovery_def.binary_rand_hash ==
        k_e1->cmd.info.ndrange_kernel.dev_bin->get_devdef()
            .autodiscovery_def.binary_rand_hash);

  // last_bin and loaded_bin should still in a reset state
  CHECK(m_device->last_bin == NULL);
  CHECK(m_device->loaded_bin == NULL);

  acl_print_debug_msg("Forcing user event completion for first kernel\n");
  CHECK_EQUAL(CL_SUCCESS, clSetUserEventStatus(ue1, CL_COMPLETE));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(ue1));

  // Should have recorded that we loaded the program.
  CHECK_EQUAL(&(dp0->device_binary), m_device->last_bin);
  CHECK_EQUAL(&(dp0->device_binary), m_device->loaded_bin);

  // submit device global forced REPROGRAM +
  // set REPROGRAM to RUNNING +
  // set REPROGRAM to COMPLETE +
  // set MEM_MIGRATE 1 to RUNNING +
  // set MEM_MIGRATE 1 to COMPLETE +
  // set MEM_MIGRATE 2 to RUNNING +
  // set MEM_MIGRATE 2 to COMPLETE +
  // submit KERNEL = 8
  CHECK_EQUAL(offset + 8, m_devlog.num_ops);
  const acl_device_op_t *op0submit = &(m_devlog.before[3]);
  const acl_device_op_t *op0running = &(m_devlog.before[4]);
  const acl_device_op_t *op0complete = &(m_devlog.before[5]);

  // Device global forced reprogram
  CHECK_EQUAL(ACL_DEVICE_OP_REPROGRAM, op0submit->info.type);
  CHECK_EQUAL(0, op0submit->id);
  CHECK(op0submit->info.event);
  CHECK_EQUAL(CL_SUBMITTED, op0submit->status);
  CHECK_EQUAL(0, op0submit->info.num_printf_bytes_pending);
  CHECK_EQUAL(1, op0submit->first_in_group);
  CHECK_EQUAL(0, op0submit->last_in_group);

  CHECK_EQUAL(ACL_DEVICE_OP_REPROGRAM, op0running->info.type);
  CHECK_EQUAL(0, op0running->id);
  CHECK(op0running->info.event);
  CHECK_EQUAL(CL_RUNNING, op0running->status);
  CHECK_EQUAL(0, op0running->info.num_printf_bytes_pending);
  CHECK_EQUAL(1, op0running->first_in_group);
  CHECK_EQUAL(0, op0running->last_in_group);

  CHECK_EQUAL(ACL_DEVICE_OP_REPROGRAM, op0complete->info.type);
  CHECK_EQUAL(0, op0complete->id);
  CHECK(op0complete->info.event);
  CHECK_EQUAL(CL_COMPLETE, op0complete->status);
  CHECK_EQUAL(0, op0complete->info.num_printf_bytes_pending);
  CHECK_EQUAL(1, op0complete->first_in_group);
  CHECK_EQUAL(0, op0complete->last_in_group);

  // The device is still programmed with the same program.
  CHECK_EQUAL(&(dp0->device_binary), m_device->last_bin);
  CHECK_EQUAL(&(dp0->device_binary), m_device->loaded_bin);

  const acl_device_op_t *op1submit = &(m_devlog.before[10]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op1submit->info.type);
  CHECK_EQUAL(k_e1, op1submit->info.event);
  CHECK_EQUAL(CL_SUBMITTED, op1submit->status);
  CHECK_EQUAL(0, op1submit->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, op1submit->first_in_group); // reprogram is first
  CHECK_EQUAL(1, op1submit->last_in_group);

  // The user-level event is linked to the kernel device op now.
  CHECK_EQUAL(op1submit->id, k_e1->current_device_op->id);

  // Pretend to start the kernel
  acl_print_debug_msg("Say kernel is running\n");
  ACL_LOCKED(
      acl_receive_kernel_update(k_e1->current_device_op->id, CL_RUNNING));
  CHECK_EQUAL(CL_RUNNING, k_e1->current_device_op->execution_status);

  ACL_LOCKED(acl_idle_update(m_context));

  // Now we have a "running" transition
  CHECK_EQUAL(offset + 9, m_devlog.num_ops);
  const acl_device_op_t *op1running = &(m_devlog.after[11]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op1running->info.type);
  CHECK_EQUAL(k_e1, op1running->info.event);
  CHECK_EQUAL(CL_RUNNING, op1running->status);
  CHECK_EQUAL(0, op1running->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, op1running->first_in_group);
  CHECK_EQUAL(1, op1running->last_in_group);

  // The running status was propagated up to the user-level event.
  CHECK_EQUAL(CL_RUNNING, k_e1->execution_status);

  acl_print_debug_msg("Say kernel is complete\n");
  ACL_LOCKED(
      acl_receive_kernel_update(k_e1->current_device_op->id, CL_COMPLETE));
  CHECK_EQUAL(CL_COMPLETE, k_e1->current_device_op->execution_status);

  ACL_LOCKED(acl_idle_update(m_context));
  // Now we have a "complete" transition
  CHECK_EQUAL(offset + 10, m_devlog.num_ops);
  const acl_device_op_t *op1complete = &(m_devlog.after[12]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op1complete->info.type);
  CHECK_EQUAL(k_e1, op1complete->info.event);
  CHECK_EQUAL(CL_COMPLETE, op1complete->status);
  CHECK_EQUAL(0, op1complete->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, op1complete->first_in_group);
  CHECK_EQUAL(1, op1complete->last_in_group);

  // Completion timestamp has propagated up to the user level event.
  CHECK_EQUAL(
      acl_platform.device_op_queue.op[op1complete->id].timestamp[CL_COMPLETE],
      k_e1->timestamp[CL_COMPLETE]);

  // Completion wipes out the downlink.
  CHECK_EQUAL(0, k_e1->current_device_op);

  // Launch the kernel for the second time
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, k, 1, &ue2, &k_e2));
  CHECK_EQUAL(CL_COMMAND_TASK, k_e2->cmd.type);
  CHECK(m_device->def.autodiscovery_def.binary_rand_hash ==
        k_e2->cmd.info.ndrange_kernel.dev_bin->get_devdef()
            .autodiscovery_def.binary_rand_hash);

  acl_print_debug_msg("Forcing user event completion for second kernel\n");
  CHECK_EQUAL(CL_SUCCESS, clSetUserEventStatus(ue2, CL_COMPLETE));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(ue2));

  // Should still have the same program loaded
  CHECK_EQUAL(&(dp0->device_binary), m_device->last_bin);
  CHECK_EQUAL(&(dp0->device_binary), m_device->loaded_bin);

  // set MEM_MIGRATE 1 to RUNNING +
  // set MEM_MIGRATE 1 to COMPLETE +
  // set MEM_MIGRATE 2 to RUNNING +
  // set MEM_MIGRATE 2 to COMPLETE +
  // submit KERNEL = 5
  CHECK_EQUAL(offset + 15, m_devlog.num_ops);
  const acl_device_op_t *op2submit = &(m_devlog.before[17]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op2submit->info.type);
  CHECK_EQUAL(k_e2, op2submit->info.event);
  CHECK_EQUAL(CL_SUBMITTED, op2submit->status);
  CHECK_EQUAL(0, op2submit->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, op2submit->first_in_group); // mem migration is first
  CHECK_EQUAL(1, op2submit->last_in_group);

  // The user-level event is linked to the kernel device op now.
  CHECK_EQUAL(op2submit->id, k_e2->current_device_op->id);

  // Pretend to start the kernel
  acl_print_debug_msg("Say kernel is running\n");
  ACL_LOCKED(
      acl_receive_kernel_update(k_e2->current_device_op->id, CL_RUNNING));
  CHECK_EQUAL(CL_RUNNING, k_e2->current_device_op->execution_status);

  ACL_LOCKED(acl_idle_update(m_context));

  // Now we have a "running" transition
  CHECK_EQUAL(offset + 16, m_devlog.num_ops);
  const acl_device_op_t *op2running = &(m_devlog.after[18]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op2running->info.type);
  CHECK_EQUAL(k_e2, op2running->info.event);
  CHECK_EQUAL(CL_RUNNING, op2running->status);
  CHECK_EQUAL(0, op2running->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, op2running->first_in_group);
  CHECK_EQUAL(1, op2running->last_in_group);

  // The running status was propagated up to the user-level event.
  CHECK_EQUAL(CL_RUNNING, k_e2->execution_status);

  acl_print_debug_msg("Say kernel is complete\n");
  ACL_LOCKED(
      acl_receive_kernel_update(k_e2->current_device_op->id, CL_COMPLETE));
  CHECK_EQUAL(CL_COMPLETE, k_e2->current_device_op->execution_status);

  ACL_LOCKED(acl_idle_update(m_context));
  // Now we have a "complete" transition
  CHECK_EQUAL(offset + 17, m_devlog.num_ops);
  const acl_device_op_t *op2complete = &(m_devlog.after[19]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op2complete->info.type);
  CHECK_EQUAL(k_e2, op2complete->info.event);
  CHECK_EQUAL(CL_COMPLETE, op2complete->status);
  CHECK_EQUAL(0, op2complete->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, op2complete->first_in_group);
  CHECK_EQUAL(1, op2complete->last_in_group);

  // Completion timestamp has propagated up to the user level event.
  CHECK_EQUAL(
      acl_platform.device_op_queue.op[op2complete->id].timestamp[CL_COMPLETE],
      k_e2->timestamp[CL_COMPLETE]);

  // Completion wipes out the downlink.
  CHECK_EQUAL(0, k_e2->current_device_op);

  // And let go.
  // (Don't check for CL_INVALID_EVENT on a second release of each of
  // these events because the events might be reused.)
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(k_e1));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(k_e2));
  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(k));

  // Clean up device global
  m_device->def.autodiscovery_def.device_global_mem_defs.clear();
}

TEST(acl_kernel_reprogram_scheduler, skip_reprogram_on_start) {
  // Test if reprogram is skipped if the binary currently loaded
  // on the board is the same as the one to be loaded

  // Initial eager reprogram
  int offset = m_devlog.num_ops;
  CHECK_EQUAL(3, offset);
  // Just the initial program load.
  CHECK_EQUAL(m_first_dev_bin, m_device->last_bin);
  CHECK_EQUAL(m_first_dev_bin, m_device->loaded_bin);

  // Pretend execution starts now
  m_device->last_bin->unload_content();
  m_device->last_bin = NULL;
  m_device->loaded_bin->unload_content();
  m_device->loaded_bin = NULL;

  acl_device_program_info_t *dp0 = check_dev_prog(m_program0);
  m_context->reprogram_buf_read_callback = read_mem_callback;
  m_context->reprogram_buf_write_callback = write_mem_callback;

  // A device side buffer
  cl_int status = CL_INVALID_VALUE;
  cl_mem mem = clCreateBuffer(m_context, CL_MEM_READ_WRITE, 2048, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(mem);
  memset(mem->host_mem.aligned_ptr, 'X', mem->size);
  memset(mem->block_allocation->range.begin, 'x', mem->size);

  CHECK_EQUAL(1, m_context->device_buffers_have_backing_store);
  CHECK_EQUAL(0, mem->block_allocation->region->is_host_accessible);
  CHECK_EQUAL(0, mem->writable_copy_on_host);

  cl_kernel k = get_kernel(m_program0);
  cl_event ue = get_user_event();
  cl_event k_e = 0;

  // Launch the kernel for the first time
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k, 0, sizeof(cl_mem), &mem));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k, 1, sizeof(cl_mem), &mem));
  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, k, 1, &ue, &k_e));
  CHECK_EQUAL(CL_COMMAND_TASK, k_e->cmd.type);
  CHECK(m_device->def.autodiscovery_def.binary_rand_hash ==
        k_e->cmd.info.ndrange_kernel.dev_bin->get_devdef()
            .autodiscovery_def.binary_rand_hash);

  // last_bin and loaded_bin should still in a reset state
  CHECK(m_device->last_bin == NULL);
  CHECK(m_device->loaded_bin == NULL);

  acl_print_debug_msg("Forcing user event completion for first kernel\n");
  CHECK_EQUAL(CL_SUCCESS, clSetUserEventStatus(ue, CL_COMPLETE));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(ue));

  // Since reprogram didn't occur, only last_bin should be updated
  CHECK_EQUAL(&(dp0->device_binary), m_device->last_bin);
  CHECK(m_device->loaded_bin == NULL);

  // set MEM_MIGRATE 1 to RUNNING +
  // set MEM_MIGRATE 1 to COMPLETE +
  // set MEM_MIGRATE 2 to RUNNING +
  // set MEM_MIGRATE 2 to COMPLETE +
  // submit KERNEL = 5
  CHECK_EQUAL(offset + 5, m_devlog.num_ops);
  const acl_device_op_t *op0submit = &(m_devlog.before[7]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op0submit->info.type);
  CHECK_EQUAL(k_e, op0submit->info.event);
  CHECK_EQUAL(CL_SUBMITTED, op0submit->status);
  CHECK_EQUAL(0, op0submit->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, op0submit->first_in_group); // mem migrate is first
  CHECK_EQUAL(1, op0submit->last_in_group);

  // The user-level event is linked to the kernel device op now.
  CHECK_EQUAL(op0submit->id, k_e->current_device_op->id);

  // Pretend to start the kernel
  acl_print_debug_msg("Say kernel is running\n");
  ACL_LOCKED(acl_receive_kernel_update(k_e->current_device_op->id, CL_RUNNING));
  CHECK_EQUAL(CL_RUNNING, k_e->current_device_op->execution_status);

  ACL_LOCKED(acl_idle_update(m_context));

  // Now we have a "running" transition
  CHECK_EQUAL(offset + 6, m_devlog.num_ops);
  const acl_device_op_t *op0running = &(m_devlog.after[8]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op0running->info.type);
  CHECK_EQUAL(k_e, op0running->info.event);
  CHECK_EQUAL(CL_RUNNING, op0running->status);
  CHECK_EQUAL(0, op0running->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, op0running->first_in_group);
  CHECK_EQUAL(1, op0running->last_in_group);

  // The running status was propagated up to the user-level event.
  CHECK_EQUAL(CL_RUNNING, k_e->execution_status);

  acl_print_debug_msg("Say kernel is complete\n");
  ACL_LOCKED(
      acl_receive_kernel_update(k_e->current_device_op->id, CL_COMPLETE));
  CHECK_EQUAL(CL_COMPLETE, k_e->current_device_op->execution_status);

  ACL_LOCKED(acl_idle_update(m_context));
  // Now we have a "complete" transition
  CHECK_EQUAL(offset + 7, m_devlog.num_ops);
  const acl_device_op_t *op0complete = &(m_devlog.after[9]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op0complete->info.type);
  CHECK_EQUAL(k_e, op0complete->info.event);
  CHECK_EQUAL(CL_COMPLETE, op0complete->status);
  CHECK_EQUAL(0, op0complete->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, op0complete->first_in_group);
  CHECK_EQUAL(1, op0complete->last_in_group);

  // Completion timestamp has propagated up to the user level event.
  CHECK_EQUAL(
      acl_platform.device_op_queue.op[op0complete->id].timestamp[CL_COMPLETE],
      k_e->timestamp[CL_COMPLETE]);

  // Completion wipes out the downlink.
  CHECK_EQUAL(0, k_e->current_device_op);

  // And let go.
  // (Don't check for CL_INVALID_EVENT on a second release of each of
  // these events because the events might be reused.)
  CHECK_EQUAL(CL_SUCCESS, clReleaseMemObject(mem));
  CHECK_EQUAL(CL_SUCCESS, clReleaseEvent(k_e));
  CHECK_EQUAL(CL_SUCCESS, clReleaseKernel(k));
}

TEST(acl_kernel_reprogram_scheduler, use_host_buf_as_arg) {
  // Must be able to use a host-side buffer as a kernel argument.
  cl_int status = 0;
  enum { SIZE = 2048 };
  char buf[SIZE];

  cl_kernel k = get_kernel(m_program0);

  // Both kinds of host side buffers: user auto alloc, and user pointer.

  status = CL_INVALID_VALUE;
  cl_mem mem0 =
      clCreateBuffer(m_context, CL_MEM_ALLOC_HOST_PTR, SIZE, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  status = CL_INVALID_VALUE;
  cl_mem mem1 =
      clCreateBuffer(m_context, CL_MEM_USE_HOST_PTR, SIZE, buf, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k, 0, sizeof(cl_mem), &mem0));
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k, 1, sizeof(cl_mem), &mem1));

  // And reset back to NULL argument eliminates the value again.
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k, 0, sizeof(cl_mem), 0));

  // And set it back to a buf does it again!
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k, 0, sizeof(cl_mem), &mem1));

  clReleaseMemObject(mem0);
  clReleaseMemObject(mem1);
  clReleaseKernel(k);
}

typedef struct {
  void *buf;
  const char *shouldbe;
  const char *change_to;
} bufcheck_t;

TEST(acl_kernel_reprogram_scheduler, use_host_buf_use_twice_same_invocation) {
  // Must be able to use a host-side buffer as a kernel argument.
  //
  // If the same buffer is used in two arguments, then we only make one
  // device side buffer.

  cl_int status = 0;
  enum { SIZE = 2048 };

  cl_kernel k = get_kernel(m_program0);
  cl_event ke = 0;

  // Both kinds of host side buffers: user auto alloc, and user pointer.

  status = CL_INVALID_VALUE;
  cl_mem mem0 =
      clCreateBuffer(m_context, CL_MEM_ALLOC_HOST_PTR, SIZE, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k, 0, sizeof(cl_mem), &mem0));
  // Same host buffer!
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k, 1, sizeof(cl_mem), &mem0));

  CHECK_EQUAL(CL_SUCCESS, clEnqueueTask(m_cq, k, 0, 0, &ke));

  acl_device_op_t *active_op = ke->current_device_op;
  ACL_LOCKED(acl_receive_kernel_update(active_op->id, CL_RUNNING));
  ACL_LOCKED(acl_idle_update(m_context));
  ACL_LOCKED(acl_receive_kernel_update(active_op->id, CL_COMPLETE));
  ACL_LOCKED(acl_idle_update(m_context));

  clReleaseEvent(ke);
  clReleaseMemObject(mem0);
  clReleaseKernel(k);
}

TEST(acl_kernel_reprogram_scheduler, printf_handler) {

  // Ensure we manage the printf interrupts properly.

  cl_int status = 0;
  enum { SIZE = 1024 };

  // Use the printit kernel.
  cl_kernel k = get_kernel(m_program0, 2);

  status = CL_INVALID_VALUE;
  cl_mem mem = clCreateBuffer(m_context, CL_MEM_READ_ONLY, SIZE, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(k, 0, sizeof(cl_mem), &mem));

  cl_event ke = 0;
  status = clEnqueueTask(m_cq, k, 0, 0, &ke);
  CHECK_EQUAL(CL_SUCCESS, status);

  acl_device_op_t *op = 0;

  // Initial programming of the device.
  // There are already three updates in the log from the device programming
  // (during the test setup). There are four more updates from the two mem
  // migration ops changing to RUNNING and then COMPLETE (there are two ops for
  // mem migration because printf kernels have an implicit buffer added) There
  // is one more from the kernel launch 8 total.
  {
    int expectedNumOps = 8;
    acl_device_op_type_t expectedType[] = {
        ACL_DEVICE_OP_REPROGRAM,     ACL_DEVICE_OP_REPROGRAM,
        ACL_DEVICE_OP_REPROGRAM,     ACL_DEVICE_OP_MEM_MIGRATION,
        ACL_DEVICE_OP_MEM_MIGRATION, ACL_DEVICE_OP_MEM_MIGRATION,
        ACL_DEVICE_OP_MEM_MIGRATION, ACL_DEVICE_OP_KERNEL};
    cl_int expectedStatus[] = {CL_SUBMITTED, CL_RUNNING,  CL_COMPLETE,
                               CL_RUNNING,   CL_COMPLETE, CL_RUNNING,
                               CL_COMPLETE,  CL_SUBMITTED};
    int expectedFirstInGroup[] = {1, 1, 1, 1, 1, 0, 0, 0};
    int expectedLastInGroup[] = {1, 1, 1, 0, 0, 0, 0, 1};
    cl_event expectedEvent[] = {NULL, NULL, NULL, ke, ke, ke, ke, ke};

    CHECK_EQUAL(expectedNumOps, m_devlog.num_ops);

    for (int i = 0; i < expectedNumOps; i++) {
      op = &(m_devlog.before[i]);
      CHECK_EQUAL(expectedType[i], op->info.type);
      if (expectedEvent[i] != NULL) {
        CHECK_EQUAL(expectedEvent[i], op->info.event);
      }
      CHECK_EQUAL(expectedStatus[i], op->status);
      CHECK_EQUAL(expectedFirstInGroup[i], op->first_in_group);
      CHECK_EQUAL(expectedLastInGroup[i], op->last_in_group);
    }
  }
  // Say it's running.
  ACL_LOCKED(acl_receive_kernel_update(op->id, CL_RUNNING));

  // Check that acl_receive_kernel_update does the right thing with the index.
  CHECK_EQUAL(CL_RUNNING, l_find_op(op->id)->execution_status);
  CHECK_EQUAL(CL_SUBMITTED, l_find_op(op->id)->status); // not copied up!
  CHECK(acl_is_retained(op->info.event->cmd.info.ndrange_kernel
                            .invocation_wrapper)); // non-zero reference count
                                                   // on the invocation wrapper.
  auto accel_def = op->info.event->cmd.info.ndrange_kernel.kernel->accel_def;
  CHECK_EQUAL(op->info.event,
              op->info.event->cmd.info.ndrange_kernel.dev_bin->get_dev_prog()
                  ->current_event[accel_def]); // async signal handler doesn't
                                               // mark the accelerator as clear.

  ACL_LOCKED(acl_idle_update(m_context)); // bump scheduler
  CHECK_EQUAL(CL_RUNNING, l_find_op(op->id)->execution_status);
  CHECK_EQUAL(CL_RUNNING, l_find_op(op->id)->status); // Now it's copied up.

  CHECK_EQUAL(9, m_devlog.num_ops);
  op = &(m_devlog.before[8]);
  CHECK_EQUAL(acl_platform.device_op_queue.op + op->id, ke->current_device_op);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op->info.type);
  CHECK_EQUAL(CL_RUNNING, op->status);
  CHECK_EQUAL(ke, op->info.event);
  CHECK_EQUAL(0, op->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, op->info.debug_dump_printf);

  // Testing debug_printf_dump
  // Pretend the kernel actually put something in the device buffer.
  CHECK(k->printf_device_buffer);
  cl_mem printf_buf = k->printf_device_buffer;

  // The printf data is just a %d for an integer.
  const int printf_conversion_index =
      0; // Which % conversion specifier in the format string?
  const int printf_data = 42;
  const unsigned int printf_bytes = 8;
  *((int *)printf_buf->block_allocation->range.begin) = printf_conversion_index;
  *(((int *)printf_buf->block_allocation->range.begin) + 1) = printf_data;
  // Check operation of printf-pickup-scheduler call back function.
  // Activation id should be the device op id!
  ACL_LOCKED(acl_schedule_printf_buffer_pickup(op->id, printf_bytes,
                                               1 /*Debug printf dump*/));
  CHECK_EQUAL(
      printf_bytes,
      acl_platform.device_op_queue.op[op->id].info.num_printf_bytes_pending);
  ACL_LOCKED(acl_idle_update(m_context));

  CHECK_EQUAL(10, m_devlog.num_ops);

  op = &(m_devlog.before[9]);
  CHECK_EQUAL(acl_platform.device_op_queue.op + op->id, ke->current_device_op);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op->info.type);
  CHECK_EQUAL(CL_RUNNING, op->status);
  CHECK_EQUAL(ke, op->info.event);
  CHECK_EQUAL(printf_bytes, op->info.num_printf_bytes_pending);
  CHECK_EQUAL(1, op->info.debug_dump_printf);

  op = &(m_devlog.after[9]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op->info.type);
  CHECK_EQUAL(CL_RUNNING, op->status);
  CHECK_EQUAL(ke, op->info.event);
  CHECK_EQUAL(0, op->info.num_printf_bytes_pending); // should have cleared it.
  CHECK_EQUAL(1, op->info.debug_dump_printf);
  CHECK_EQUAL(printf_bytes, k->processed_printf_buffer_size);
  ACL_LOCKED(acl_idle_update(m_context));

  // Testing normal printf dump
  *(((int *)printf_buf->block_allocation->range.begin) + 2) = printf_data;
  // Check operation of printf-pickup-scheduler call back function.
  // Activation id should be the device op id!
  // Now we have two printf_data in the buffer, therefore the size is doubled.
  ACL_LOCKED(acl_schedule_printf_buffer_pickup(op->id, printf_bytes * 2,
                                               0 /*Not debug printf dump*/));
  CHECK_EQUAL(
      printf_bytes * 2,
      acl_platform.device_op_queue.op[op->id].info.num_printf_bytes_pending);
  ACL_LOCKED(acl_idle_update(m_context));

  CHECK_EQUAL(11, m_devlog.num_ops);

  op = &(m_devlog.before[10]);
  CHECK_EQUAL(acl_platform.device_op_queue.op + op->id, ke->current_device_op);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op->info.type);
  CHECK_EQUAL(CL_RUNNING, op->status);
  CHECK_EQUAL(ke, op->info.event);
  CHECK_EQUAL(printf_bytes * 2, op->info.num_printf_bytes_pending);

  op = &(m_devlog.after[10]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op->info.type);
  CHECK_EQUAL(CL_RUNNING, op->status);
  CHECK_EQUAL(ke, op->info.event);
  CHECK_EQUAL(0, op->info.num_printf_bytes_pending); // should have cleared it.
  CHECK_EQUAL(0, op->info.debug_dump_printf);
  CHECK_EQUAL(0, k->processed_printf_buffer_size); // A full dump should reset
                                                   // this variable to 0
  ACL_LOCKED(acl_idle_update(m_context));
  // Testing normal printf dump end

  // Say it's complete, but with some printf stuff to clean up.
  ACL_LOCKED(acl_receive_kernel_update(op->id, CL_COMPLETE));
  // Check that acl_receive_kernel_update does the right thing with the index.
  CHECK_EQUAL(CL_COMPLETE, l_find_op(op->id)->execution_status);
  CHECK_EQUAL(CL_RUNNING, l_find_op(op->id)->status); // not copied up!
  // Set printf bytes which should be picked up
  acl_platform.device_op_queue.op[op->id].info.num_printf_bytes_pending =
      printf_bytes;
  CHECK_EQUAL(0, op->info.debug_dump_printf);

  ACL_LOCKED(acl_idle_update(m_context)); // bump scheduler
  CHECK_EQUAL(CL_COMPLETE, l_find_op(op->id)->execution_status);
  CHECK_EQUAL(CL_COMPLETE, l_find_op(op->id)->status); // Now it's copied up.

  CHECK_EQUAL(13, m_devlog.num_ops);

  // #10 is the printf flush.
  op = &(m_devlog.before[11]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op->info.type);
  CHECK_EQUAL(CL_RUNNING, op->status);
  CHECK_EQUAL(CL_COMPLETE, op->execution_status);
  CHECK_EQUAL(ke, op->info.event);
  CHECK_EQUAL(printf_bytes, op->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, op->info.debug_dump_printf);

  op = &(m_devlog.after[11]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op->info.type);
  CHECK_EQUAL(CL_RUNNING, op->status);
  CHECK_EQUAL(CL_COMPLETE, op->execution_status);
  CHECK_EQUAL(ke, op->info.event);
  CHECK_EQUAL(0, op->info.num_printf_bytes_pending); // should have cleared it.
  CHECK_EQUAL(0, op->info.debug_dump_printf);

  // #11 is the completion.
  op = &(m_devlog.before[12]);
  CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, op->info.type);
  CHECK_EQUAL(CL_COMPLETE, op->status);
  CHECK_EQUAL(ke, op->info.event);
  CHECK_EQUAL(0, op->info.num_printf_bytes_pending);
  CHECK_EQUAL(0, k->processed_printf_buffer_size);
  CHECK_EQUAL(0, op->info.debug_dump_printf);

  CHECK_EQUAL(0, ke->current_device_op); // DONE!

  clReleaseEvent(ke);
  clReleaseKernel(k);
  clReleaseMemObject(mem);
}

MT_TEST(acl_kernel, set_svm_kernel_arg) {
  const char *sample_svm_kernel_name = "kernel8_svm_args";
  const char *sample_svm_kernel_name2 = "kernel14_svm_arg_alignment";

  ACL_LOCKED(acl_print_debug_msg("set_kernel_arg\n"));
  cl_int status;
  cl_kernel kernel;
  cl_kernel kernel2;
  acl_aligned_ptr_t tmp_aligned_pointer;
  int *tmp_int_pointer;
  acl_svm_entry_t *svm_entry;

  CHECK_EQUAL(0, m_program->num_kernels);

  status = CL_INVALID_VALUE;
  kernel = clCreateKernel(m_program, sample_svm_kernel_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  kernel2 = clCreateKernel(m_program, sample_svm_kernel_name2, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Arg 0 is __global
  // Arg 0 is __local

  // Bad kernel
  acl_set_allow_invalid_type<cl_kernel>(1);
  CHECK_EQUAL(CL_INVALID_KERNEL, clSetKernelArgSVMPointerIntelFPGA(0, 0, 0));
  CHECK_EQUAL(CL_INVALID_KERNEL,
              clSetKernelArgSVMPointerIntelFPGA((cl_kernel)m_program, 0, 0));
  acl_set_allow_invalid_type<cl_kernel>(0);

  // Manually create & free an unaligned pointer. This is because the clSVMAlloc
  // function Doesn't currently allow us to create an unaligned pointer.
  ACL_LOCKED(tmp_aligned_pointer =
                 acl_mem_aligned_malloc(sizeof(char) + sizeof(int)));
  tmp_int_pointer =
      (int *)((char *)tmp_aligned_pointer.aligned_ptr + sizeof(char) * 8);
  svm_entry = m_context->svm_list;
  m_context->svm_list = (acl_svm_entry_t *)acl_malloc(sizeof(acl_svm_entry_t));
  CHECK(m_context->svm_list);
  m_context->svm_list->next = svm_entry;
  m_context->svm_list->read_only = CL_FALSE;
  m_context->svm_list->write_only = CL_FALSE;
  m_context->svm_list->is_mapped = CL_FALSE;
  m_context->svm_list->ptr = tmp_int_pointer;
  m_context->svm_list->size = 1;

  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArgSVMPointerIntelFPGA(kernel, 0, tmp_int_pointer));

  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgSVMPointerIntelFPGA(kernel2, 0, tmp_int_pointer));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgSVMPointerIntelFPGA(kernel2, 1, tmp_int_pointer));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgSVMPointerIntelFPGA(kernel2, 2, tmp_int_pointer));
  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArgSVMPointerIntelFPGA(kernel2, 3, tmp_int_pointer));
  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArgSVMPointerIntelFPGA(kernel2, 4, tmp_int_pointer));

  ACL_LOCKED(acl_mem_aligned_free(m_context, &tmp_aligned_pointer));
  svm_entry = m_context->svm_list;
  m_context->svm_list = m_context->svm_list->next;
  acl_free(svm_entry);

  // Set up mem args.
  void *test_ptr = clSVMAllocIntelFPGA(m_context, CL_MEM_READ_WRITE, 8, 0);

  // Valid args
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArgSVMPointerIntelFPGA(
                              kernel, 0, test_ptr)); // can pass NULL arg

  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgSVMPointerIntelFPGA(kernel2, 0, test_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgSVMPointerIntelFPGA(kernel2, 1, test_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgSVMPointerIntelFPGA(kernel2, 2, test_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgSVMPointerIntelFPGA(kernel2, 3, test_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgSVMPointerIntelFPGA(kernel2, 4, test_ptr));

  clSVMFreeIntelFPGA(m_context, test_ptr);
  clReleaseKernel(kernel);
  clReleaseKernel(kernel2);

  CHECK_EQUAL(0, m_program->num_kernels);
}

MT_TEST(acl_kernel, set_usm_kernel_arg) {
  const char *sample_svm_kernel_name2 = "kernel14_svm_arg_alignment";
  char *usm_device_alloc;
  char *usm_device_ptr;

  ACL_LOCKED(acl_print_debug_msg("set_usm_kernel_arg\n"));
  cl_int status;
  cl_kernel kernel;

  CHECK_EQUAL(0, m_program->num_kernels);

  status = CL_INVALID_VALUE;
  kernel = clCreateKernel(m_program, sample_svm_kernel_name2, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Bad kernel
  acl_set_allow_invalid_type<cl_kernel>(1);
  CHECK_EQUAL(CL_INVALID_KERNEL, clSetKernelArgMemPointerINTEL(0, 0, 0));
  CHECK_EQUAL(CL_INVALID_KERNEL,
              clSetKernelArgMemPointerINTEL((cl_kernel)m_program, 0, 0));
  acl_set_allow_invalid_type<cl_kernel>(0);

  usm_device_alloc = (char *)clDeviceMemAllocINTEL(
      m_context, m_device[0], NULL, 4096, ACL_MEM_ALIGN, &status);
  CHECK(usm_device_alloc != NULL);
  CHECK_EQUAL(status, CL_SUCCESS);
  usm_device_ptr = usm_device_alloc;

  // Kernel arguments have alignments of 1, 4, 8, 16, and 1024.  Test them all.

  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 0, usm_device_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 1, usm_device_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 2, usm_device_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 3, usm_device_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 4, usm_device_ptr));

  usm_device_ptr++;
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 0, usm_device_ptr));
  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArgMemPointerINTEL(kernel, 1, usm_device_ptr));
  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArgMemPointerINTEL(kernel, 2, usm_device_ptr));
  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArgMemPointerINTEL(kernel, 3, usm_device_ptr));
  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArgMemPointerINTEL(kernel, 4, usm_device_ptr));

  usm_device_ptr += 3;
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 0, usm_device_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 1, usm_device_ptr));
  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArgMemPointerINTEL(kernel, 2, usm_device_ptr));
  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArgMemPointerINTEL(kernel, 3, usm_device_ptr));
  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArgMemPointerINTEL(kernel, 4, usm_device_ptr));

  usm_device_ptr += 4;
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 0, usm_device_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 1, usm_device_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 2, usm_device_ptr));
  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArgMemPointerINTEL(kernel, 3, usm_device_ptr));
  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArgMemPointerINTEL(kernel, 4, usm_device_ptr));

  usm_device_ptr += 8;
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 0, usm_device_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 1, usm_device_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 2, usm_device_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 3, usm_device_ptr));
  CHECK_EQUAL(CL_INVALID_ARG_VALUE,
              clSetKernelArgMemPointerINTEL(kernel, 4, usm_device_ptr));

  usm_device_ptr += 1008;
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 0, usm_device_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 1, usm_device_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 2, usm_device_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 3, usm_device_ptr));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArgMemPointerINTEL(kernel, 4, usm_device_ptr));

  clMemFreeINTEL(m_context, usm_device_alloc);

  clReleaseKernel(kernel);

  CHECK_EQUAL(0, m_program->num_kernels);
}

MT_TEST(acl_kernel, set_kernel_exec_info) {
  const char *sample_svm_kernel_name = "kernel8_svm_args";

  ACL_LOCKED(acl_print_debug_msg("set_kernel_exec_info\n"));
  cl_int status;
  cl_kernel kernel;

  CHECK_EQUAL(0, m_program->num_kernels);

  status = CL_INVALID_VALUE;
  kernel = clCreateKernel(m_program, sample_svm_kernel_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Arg 0 is __global
  // Arg 0 is __local

  // Bad kernel
  acl_set_allow_invalid_type<cl_kernel>(1);
  CHECK_EQUAL(CL_INVALID_KERNEL, clSetKernelExecInfoIntelFPGA(0, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_KERNEL,
              clSetKernelExecInfoIntelFPGA((cl_kernel)m_program, 0, 0, 0));
  acl_set_allow_invalid_type<cl_kernel>(0);

  // Set up mem args.
  void *good_ptr = clSVMAllocIntelFPGA(m_context, CL_MEM_READ_WRITE, 8, 0);
  void *good_ptr2 = clSVMAllocIntelFPGA(m_context, CL_MEM_READ_WRITE, 8, 0);
  void *bad_ptr = malloc(16);
  CHECK(bad_ptr);
  void *good_ptrs[] = {good_ptr, good_ptr2};
  void *bad_ptrs[] = {good_ptr, bad_ptr};
  cl_bool use_fine_grain = CL_TRUE;
  // Invalid value
  CHECK_EQUAL(CL_INVALID_VALUE,
              clSetKernelExecInfoIntelFPGA(kernel, 0, sizeof(cl_bool),
                                           0)); // invalid param_name
  CHECK_EQUAL(CL_INVALID_VALUE,
              clSetKernelExecInfoIntelFPGA(
                  kernel, CL_KERNEL_EXEC_INFO_SVM_FINE_GRAIN_SYSTEM,
                  sizeof(cl_bool), 0)); // invalid param_value
  CHECK_EQUAL(CL_INVALID_VALUE,
              clSetKernelExecInfoIntelFPGA(
                  kernel, CL_KERNEL_EXEC_INFO_SVM_FINE_GRAIN_SYSTEM, 0,
                  &use_fine_grain)); // invalid param size
  CHECK_EQUAL(CL_INVALID_VALUE, clSetKernelExecInfoIntelFPGA(
                                    kernel, CL_KERNEL_EXEC_INFO_SVM_PTRS, 10,
                                    good_ptrs)); // invalid param size
  CHECK_EQUAL(CL_INVALID_VALUE,
              clSetKernelExecInfoIntelFPGA(kernel, CL_KERNEL_EXEC_INFO_SVM_PTRS,
                                           2 * sizeof(void *),
                                           bad_ptrs)); // invalid svm pointers

  CHECK_EQUAL(CL_INVALID_OPERATION,
              clSetKernelExecInfoIntelFPGA(
                  kernel, CL_KERNEL_EXEC_INFO_SVM_FINE_GRAIN_SYSTEM,
                  sizeof(cl_bool),
                  &use_fine_grain)); // We don't fine-grain system SVM (yet)

  // Valid args
  use_fine_grain = CL_FALSE;
  CHECK_EQUAL(CL_SUCCESS, clSetKernelExecInfoIntelFPGA(
                              kernel, CL_KERNEL_EXEC_INFO_SVM_FINE_GRAIN_SYSTEM,
                              sizeof(cl_bool), &use_fine_grain));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelExecInfoIntelFPGA(kernel, CL_KERNEL_EXEC_INFO_SVM_PTRS,
                                           2 * sizeof(void *), good_ptrs));

  free(bad_ptr);
  clSVMFreeIntelFPGA(m_context, good_ptr);
  clSVMFreeIntelFPGA(m_context, good_ptr2);
  clReleaseKernel(kernel);

  CHECK_EQUAL(0, m_program->num_kernels);
}

#if ACL_SUPPORT_IMAGES == 1
MT_TEST(acl_kernel, set_image_kernel_arg) {
  const char *sample_image_kernel_name = "kernel9_image_args";

  ACL_LOCKED(acl_print_debug_msg("set_kernel_arg\n"));
  cl_int status;
  cl_kernel kernel;
  cl_image_format image_format = {CL_R, CL_FLOAT};
  cl_image_desc image_desc = {
      CL_MEM_OBJECT_IMAGE3D, 5, 6, 7, 0, 0, 0, 0, 0, {NULL}};
  cl_mem image;
  cl_sampler_properties sampler_properties[7];
  cl_sampler sampler;

  CHECK_EQUAL(0, m_program->num_kernels);

  status = CL_INVALID_VALUE;
  kernel = clCreateKernel(m_program, sample_image_kernel_name, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  image = clCreateImage(m_context, CL_MEM_READ_WRITE, &image_format,
                        &image_desc, 0, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  sampler_properties[0] = 0;
  sampler =
      clCreateSamplerWithProperties(m_context, sampler_properties, &status);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Bad image object.
  syncThreads();
  acl_set_allow_invalid_type<cl_mem>(1);
  syncThreads();
  struct _cl_mem fake_mem = {0};
  cl_mem fake_mem_ptr = &fake_mem;
  CHECK_EQUAL(CL_INVALID_MEM_OBJECT,
              clSetKernelArg(kernel, 0, sizeof(cl_mem), &fake_mem_ptr));
  syncThreads();
  acl_set_allow_invalid_type<cl_mem>(0);
  syncThreads();

  // Invalid sampler
  CHECK_EQUAL(CL_INVALID_SAMPLER,
              clSetKernelArg(kernel, 1, sizeof(cl_sampler), &kernel));

  // Bad arg size
  CHECK_EQUAL(CL_INVALID_ARG_SIZE,
              clSetKernelArg(kernel, 0, sizeof(cl_mem) - 1, &image));
  CHECK_EQUAL(CL_INVALID_ARG_SIZE,
              clSetKernelArg(kernel, 1, sizeof(cl_sampler) - 1, &sampler));

  // Valid args
  CHECK_EQUAL(CL_SUCCESS, clSetKernelArg(kernel, 0, sizeof(cl_mem), &image));
  CHECK_EQUAL(CL_SUCCESS,
              clSetKernelArg(kernel, 1, sizeof(cl_sampler), &sampler));

  clReleaseMemObject(image);
  clReleaseSampler(sampler);
  clReleaseKernel(kernel);

  CHECK_EQUAL(0, m_program->num_kernels);
}
#endif
