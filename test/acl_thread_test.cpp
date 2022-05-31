// Copyright (C) 2015-2021 Intel Corporation
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
#include <acl_globals.h>
#include <acl_thread.h>
#include <acl_util.h>

#include "acl_hal_test.h"
#include "acl_test.h"

#include <vector>

MT_TEST_GROUP(acl_thread) {
  void setup() {
    if (threadNum() == 0) {
      acl_test_setup_generic_system();
      setup_shared_data();
    }
    syncThreads();

    cl_int status;
    m_cq = clCreateCommandQueue(m_context, m_device, CL_QUEUE_PROFILING_ENABLE,
                                &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_cq);
  }

  void setup_shared_data() {
    cl_int status;

    CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &m_platform, 0));
    ACL_LOCKED(CHECK(acl_platform_is_valid(m_platform)));
    CHECK_EQUAL(CL_SUCCESS, clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, 1,
                                           &m_device, &m_num_devices));
    CHECK(m_num_devices > 0);

    m_context = clCreateContext(acl_test_context_prop_preloaded_binary_only(),
                                1, &m_device, 0, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_context);

    const unsigned char *binary = (const unsigned char *)"0";
    size_t binary_length = 1;
    m_program = clCreateProgramWithBinary(
        m_context, 1, &m_device, &binary_length, &binary, NULL, &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    status = clBuildProgram(m_program, 1, &m_device, "", NULL, NULL);
    CHECK_EQUAL(CL_SUCCESS, status);
  }

  void teardown() {
    cl_int status = clReleaseCommandQueue(m_cq);
    CHECK_EQUAL(CL_SUCCESS, status);

    syncThreads();
    if (threadNum() == 0) {
      status = clReleaseProgram(m_program);
      CHECK_EQUAL(CL_SUCCESS, status);
      status = clReleaseContext(m_context);
      CHECK_EQUAL(CL_SUCCESS, status);

      acl_test_teardown_generic_system();
    }
    acl_assert_unlocked();
  }

protected:
  static cl_platform_id m_platform;
  static cl_device_id m_device;
  static cl_uint m_num_devices;
  static cl_context m_context;
  static cl_program m_program;
  cl_command_queue m_cq;
};

TEST_GROUP_STATIC(acl_thread, cl_platform_id, m_platform);
TEST_GROUP_STATIC(acl_thread, cl_device_id, m_device);
TEST_GROUP_STATIC(acl_thread, cl_uint, m_num_devices);
TEST_GROUP_STATIC(acl_thread, cl_context, m_context);
TEST_GROUP_STATIC(acl_thread, cl_program, m_program);

// The purpose of this test is to test multiple "kernel complete" and "printf
// buffer ready" callbacks occurring at the same time from multiple threads.
// Each thread runs a different kernel simultaneously and they all call the
// callbacks at the same time.
MT_TEST(acl_thread, kernel_and_printf_callback) {
  cl_int status;
  cl_kernel kernel = NULL;
  cl_event kernel_event = NULL;
  cl_int activation_id = 0;

  std::vector<cl_mem> mem_args;
  std::vector<cl_sampler> sampler_args;

  const acl_system_def_t *sys_def;
  ACL_LOCKED(sys_def = acl_present_board_def());
  const acl_device_def_autodiscovery_t *device_def =
      &(sys_def->device[0].autodiscovery_def);

  // We need this device to have at least a few kernels for us to use,
  // otherwise most of the threads running this test are doing nothing. At
  // the time of writing the first FPGA device in the test environment had 10
  // kernels. This check therefore ensures that we can run this test with up
  // to 10 threads and each will be doing something useful.
  CHECK(device_def->accel.size() >= 10);

  // if threadNum() is greater than or equal to the number of kernels then
  // there's no kernel for this thread to run
  if (threadNum() < (int)device_def->accel.size()) {
    const acl_accel_def_t *accel_def =
        &(device_def->accel[static_cast<size_t>(threadNum())]);
    const acl_kernel_interface_t *kernel_iface = &(accel_def->iface);

    // create kernel with the right name
    kernel = clCreateKernel(m_program, kernel_iface->name.c_str(), &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(kernel);

    // set arguments of kernel to valid values based on acl_kernel_interface_t
    for (cl_uint arg_idx = 0; arg_idx < kernel_iface->args.size(); ++arg_idx) {
      const acl_kernel_arg_info_t *arg_info = &(kernel_iface->args[arg_idx]);

      cl_uint val_arg = 0;
      cl_mem mem_arg = NULL;
      cl_sampler sampler_arg = NULL;

      switch (arg_info->category) {
      case ACL_ARG_BY_VALUE:
        status = clSetKernelArg(
            kernel, arg_idx, arg_info->size,
            arg_info->addr_space == ACL_ARG_ADDR_LOCAL ? NULL : &val_arg);
        CHECK_EQUAL(CL_SUCCESS, status);

        break;
      case ACL_ARG_MEM_OBJ:
        mem_arg = clCreateBuffer(m_context, CL_MEM_READ_ONLY, 64, 0, &status);
        CHECK_EQUAL(CL_SUCCESS, status);
        CHECK(mem_arg);
        mem_args.push_back(mem_arg);

        status = clSetKernelArg(kernel, arg_idx, sizeof(cl_mem), &mem_arg);
        CHECK_EQUAL(CL_SUCCESS, status);

        break;
      case ACL_ARG_SAMPLER:
        sampler_arg = clCreateSampler(m_context, CL_FALSE, CL_ADDRESS_NONE,
                                      CL_FILTER_NEAREST, &status);
        CHECK_EQUAL(CL_SUCCESS, status);
        CHECK(sampler_arg);
        sampler_args.push_back(sampler_arg);

        status =
            clSetKernelArg(kernel, arg_idx, sizeof(cl_sampler), &sampler_arg);
        CHECK_EQUAL(CL_SUCCESS, status);

        break;
      default:
        FAIL_TEST("Unknown argument category");
        break;
      }
    }

    // set local_work_size to (1, 1, 1) unless the kernel has a
    // "reqd_work_group_size" attribute to require it to be something else
    size_t local_work_size[3] = {1, 1, 1};
    for (int i = 0; i < 3; ++i) {
      if (accel_def->compile_work_group_size[i] > 0) {
        local_work_size[i] = accel_def->compile_work_group_size[i];
      }
    }

    size_t global_work_size[3] = {local_work_size[0] * 10,
                                  local_work_size[1] * 10,
                                  local_work_size[2] * 10};

    status = clEnqueueNDRangeKernel(m_cq, kernel, 3, NULL, global_work_size,
                                    local_work_size, 0, NULL, &kernel_event);
    CHECK_EQUAL(CL_SUCCESS, status);

    // wait for kernel to be submitted
    cl_int execution_status = CL_QUEUED;
    while (execution_status > CL_SUBMITTED) {
      status = clGetEventInfo(kernel_event, CL_EVENT_COMMAND_EXECUTION_STATUS,
                              sizeof(cl_int), &execution_status, NULL);
      CHECK_EQUAL(CL_SUCCESS, status);
    }

    activation_id = kernel_event->cmd.info.ndrange_kernel.invocation_wrapper
                        ->image->activation_id;
  }

  syncThreads();

  if (threadNum() < (int)device_def->accel.size()) {
    // signal kernel is running
    acltest_call_kernel_update_callback(sys_def->device[0].physical_device_id, activation_id, CL_RUNNING);

    // wait for kernel status to change
    cl_int execution_status = CL_QUEUED;
    while (execution_status != CL_RUNNING) {
      status = clGetEventInfo(kernel_event, CL_EVENT_COMMAND_EXECUTION_STATUS,
                              sizeof(cl_int), &execution_status, NULL);
      CHECK_EQUAL(CL_SUCCESS, status);
    }
  }

  syncThreads();

  if (threadNum() < (int)device_def->accel.size()) {
    // signal kernel has printf buffer
    acltest_call_printf_buffer_callback(sys_def->device[0].physical_device_id, activation_id, 100, 0);

    // wait for printf buffer to be cleared
    CHECK(kernel_event);
    while (kernel_event->current_device_op->info.num_printf_bytes_pending > 0) {
      cl_int execution_status = CL_QUEUED;
      status = clGetEventInfo(kernel_event, CL_EVENT_COMMAND_EXECUTION_STATUS,
                              sizeof(cl_int), &execution_status, NULL);
      CHECK_EQUAL(CL_SUCCESS, status);
      CHECK_EQUAL(CL_RUNNING, execution_status);
    }
  }

  syncThreads();

  if (threadNum() < (int)device_def->accel.size()) {
    // signal kernel is finished
    acltest_call_kernel_update_callback(sys_def->device[0].physical_device_id, activation_id, CL_COMPLETE);

    status = clWaitForEvents(1, &kernel_event);
    CHECK_EQUAL(CL_SUCCESS, status);
  }

  {
    std::vector<cl_mem>::iterator iter = mem_args.begin();
    std::vector<cl_mem>::iterator iter_end = mem_args.end();
    for (; iter != iter_end; ++iter) {
      status = clReleaseMemObject(*iter);
      CHECK_EQUAL(CL_SUCCESS, status);
    }
  }

  {
    std::vector<cl_sampler>::iterator iter = sampler_args.begin();
    std::vector<cl_sampler>::iterator iter_end = sampler_args.end();
    for (; iter != iter_end; ++iter) {
      status = clReleaseSampler(*iter);
      CHECK_EQUAL(CL_SUCCESS, status);
    }
  }

  status = clReleaseEvent(kernel_event);
  CHECK_EQUAL(CL_SUCCESS, status);
  status = clReleaseKernel(kernel);
  CHECK_EQUAL(CL_SUCCESS, status);
}
