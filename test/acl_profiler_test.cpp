// Copyright (C) 2014-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include <acl_command_queue.h>
#include <acl_context.h>
#include <acl_device_op.h>
#include <acl_event.h>
#include <acl_globals.h>
#include <acl_kernel.h>
#include <acl_printf.h>
#include <acl_profiler.h>
#include <acl_program.h>
#include <acl_support.h>
#include <acl_types.h>
#include <acl_util.h>

#include "acl_device_op_test.h"
#include "acl_test.h"

#define PROFILE_MON "profile.mon"

// A default empty program binary
#define EXAMPLE_BINARY                                                         \
  const unsigned char *l_bin[m_num_devices_in_context];                        \
  size_t l_bin_lengths[m_num_devices_in_context];                              \
  cl_int l_bin_status[m_num_devices_in_context];                               \
  for (cl_uint i = 0; i < m_num_devices_in_context; i++) {                     \
    l_bin[i] = (const unsigned char *)"0";                                     \
    l_bin_lengths[i] = 1;                                                      \
  }

MT_TEST_GROUP(acl_profile) {
  // Preloads a standard platform, and finds all devices.
  enum { MAX_DEVICES = 100, m_num_devices_in_context = 3 };
  void setup() {
    if (threadNum() == 0) {
      acl_test_setenv("ACL_RUNTIME_PROFILING_ACTIVE", "1");
      acl_test_setenv("ACL_PROFILE_TIMER", "1");
      acl_test_setup_generic_system();
      acl_dot_push(&m_devlog, &acl_platform.device_op_queue);

      this->load();
      m_program = this->load_program();
      this->build(m_program);
    }
    syncThreads();
  }
  void teardown() {
    syncThreads();
    if (threadNum() == 0) {
      acl_test_unsetenv("ACL_RUNTIME_PROFILING_ACTIVE");
      acl_test_unsetenv("ACL_PROFILE_TIMER");
      this->unload_program(m_program);
      this->unload();
      acl_test_teardown_generic_system();
    }
    syncThreads();
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

    m_cq = clCreateCommandQueue(m_context, m_device[0],
                                CL_QUEUE_PROFILING_ENABLE, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_cq);
  }

  void unload(void) {
    int cq_is_valid, context_is_valid, context2_is_valid;
    ACL_LOCKED(cq_is_valid = acl_command_queue_is_valid(m_cq));
    ACL_LOCKED(context_is_valid = acl_context_is_valid(m_context));
    ACL_LOCKED(context2_is_valid = acl_context_is_valid(m_context2));
    if (cq_is_valid) {
      clReleaseCommandQueue(m_cq);
    }
    if (context_is_valid) {
      clReleaseContext(m_context);
    }
    if (context2_is_valid) {
      clReleaseContext(m_context2);
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
  void unload_program(cl_program program) {
    int program_is_valid;
    ACL_LOCKED(program_is_valid = acl_program_is_valid(program));
    if (program_is_valid) {
      cl_int status;
      while (program->num_kernels) {
        clReleaseKernel(program->kernel_list->kernel);
      }
      CHECK_EQUAL(1, acl_ref_count(program));
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

protected:
  static cl_platform_id m_platform;
  static cl_device_id m_device[MAX_DEVICES];
  static cl_uint m_num_devices;
  static cl_context m_context, m_context2;
  static cl_program m_program;
  static cl_command_queue m_cq;
  static acl_device_op_test_ctx_t m_devlog;
};

TEST_GROUP_STATIC(acl_profile, cl_platform_id, m_platform);
TEST_GROUP_STATIC(acl_profile, cl_device_id, m_device[MAX_DEVICES]);
TEST_GROUP_STATIC(acl_profile, cl_uint, m_num_devices);
TEST_GROUP_STATIC(acl_profile, cl_context, m_context);
TEST_GROUP_STATIC(acl_profile, cl_context, m_context2);
TEST_GROUP_STATIC(acl_profile, cl_program, m_program);
TEST_GROUP_STATIC(acl_profile, cl_command_queue, m_cq);
TEST_GROUP_STATIC(acl_profile, acl_device_op_test_ctx_t, m_devlog);

MT_TEST(acl_profile, profiler) {
  ACL_LOCKED(acl_print_debug_msg("profiler\n"));
  cl_int status = CL_INVALID_VALUE;

  static cl_kernel kernel;
  static cl_mem src_mem;
  static cl_event event;

  if (threadNum() == 0) {
    kernel = clCreateKernel(m_program, "kernel6_profiletest", &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    CHECK_EQUAL(149, kernel->accel_def->profiling_words_to_readback);

    // Set up kernel args
    size_t mem_size = 64;
    src_mem = clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 1, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 2, sizeof(cl_mem), &src_mem));

    const cl_uint work_dim = 3;
    size_t global_size[work_dim] = {192, 192, 64}; // 192 = 3 * 64
    ACL_LOCKED(acl_print_debug_msg("about to run clEnqueueNDRangeKernel\n"));
    status = clEnqueueNDRangeKernel(m_cq, kernel, 1, 0, global_size, 0, 0, 0,
                                    &event);
    CHECK_EQUAL(CL_SUCCESS, status);
  }

  syncThreads();

  static acl_device_op_t *active_op;

  if (threadNum() == 0) {
    active_op = event->current_device_op;
    CHECK(active_op);
    CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, active_op->info.type);
    ACL_LOCKED(acl_set_device_op_execution_status(active_op, CL_RUNNING));
    ACL_LOCKED(acl_idle_update(m_context));
  }

  syncThreads();

  // do it a few times to ensure multiple threads call it at once
  for (int i = 0; i < 10; i++) {
    cl_ulong times[4];
    this->load_times(event, times);
    CHECK(times[1] < times[2]);

    CHECK_EQUAL(CL_SUCCESS, clGetProfileInfoIntelFPGA(event));
  }

  syncThreads();

  if (threadNum() == 0) {
    FILE *fp = fopen(PROFILE_MON, "r");
    CHECK(fp);
    fclose(fp);

    // Check that the low level call checks event completion status.
    event->execution_status = CL_COMPLETE;
    ACL_LOCKED(CHECK(!acl_process_profiler_scan_chain(active_op)));

    clReleaseEvent(event);
    clReleaseMemObject(src_mem);
    clReleaseKernel(kernel);
  }
}

TEST(acl_profile, open_close_file) {
  ACL_LOCKED(acl_print_debug_msg("profiler_open_close_file_API\n"));
  cl_int status = CL_INVALID_VALUE;

  static cl_kernel kernel;
  static cl_mem src_mem;
  static cl_event event;
  static acl_device_op_t *active_op;

  if (threadNum() == 0) {
    kernel = clCreateKernel(m_program, "kernel6_profiletest", &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    // Set up kernel args
    size_t mem_size = 64;
    src_mem = clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 1, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 2, sizeof(cl_mem), &src_mem));

    const cl_uint work_dim = 3;
    size_t global_size[work_dim] = {192, 192, 64}; // 192 = 3 * 64
    ACL_LOCKED(acl_print_debug_msg("about to run clEnqueueNDRangeKernel\n"));
    status = clEnqueueNDRangeKernel(m_cq, kernel, 1, 0, global_size, 0, 0, 0,
                                    &event);
    CHECK_EQUAL(CL_SUCCESS, status);

    active_op = event->current_device_op;
    CHECK(active_op);
    CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, active_op->info.type);
    ACL_LOCKED(acl_set_device_op_execution_status(active_op, CL_RUNNING));
    ACL_LOCKED(acl_idle_update(m_context));
  }

  syncThreads();

  // The profiler file should be kept open as long as the function
  // "acl_open_profiler_file" has been called more than the function
  // "acl_close_profiler_file".
  int num_open_close_operations = 100;
  for (int i = 0; i < num_open_close_operations; i++) {
    ACL_LOCKED(CHECK_EQUAL(1, acl_open_profiler_file()));
  }
  for (int i = 0; i < num_open_close_operations; i++) {
    ACL_LOCKED(CHECK(acl_process_profiler_scan_chain(active_op)));
    ACL_LOCKED(CHECK_EQUAL(1, acl_close_profiler_file()));
  }

  syncThreads();

  if (threadNum() == 0) {
    event->execution_status = CL_COMPLETE;
    clReleaseEvent(event);
    clReleaseMemObject(src_mem);
    clReleaseKernel(kernel);
  }
}

TEST(acl_profile, op_type_checks) {

  ACL_LOCKED(acl_print_debug_msg("op_type_checks\n"));

  CHECK(is_profile_timer_on());

  // Static is needed or else later function calls in the test don't think the
  // variables are defined
  static cl_kernel kernel;
  static cl_mem src_mem;
  static cl_event event;
  static acl_device_op_t *active_op;

  if (threadNum() == 0) {
    cl_int status = CL_INVALID_VALUE;

    kernel = clCreateKernel(m_program, "kernel6_profiletest", &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    // Set up kernel args
    const size_t mem_size = 64;
    src_mem = clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 1, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 2, sizeof(cl_mem), &src_mem));

    const cl_uint work_dim = 3;
    const size_t global_size[work_dim] = {192, 192, 64}; // 192 = 3 * 64
    ACL_LOCKED(acl_print_debug_msg("about to run clEnqueueNDRangeKernel\n"));
    status = clEnqueueNDRangeKernel(m_cq, kernel, 1, 0, global_size, 0, 0, 0,
                                    &event);
    CHECK_EQUAL(CL_SUCCESS, status);
  }

  syncThreads();

  if (threadNum() == 0) {
    active_op = event->current_device_op;
    CHECK(active_op);
    ACL_LOCKED(acl_set_device_op_execution_status(active_op, CL_RUNNING));
    ACL_LOCKED(acl_idle_update(m_context));
  }

  syncThreads();

  if (threadNum() == 0) {
    // Set device op type to all possible options & see if profiler
    // passes/errors
    for (int curr_device_op = 0; curr_device_op < (int)ACL_NUM_DEVICE_OP_TYPES;
         curr_device_op++) {
      if (curr_device_op == (int)ACL_DEVICE_OP_NONE)
        continue;
      active_op->info.type = (acl_device_op_type_t)curr_device_op;
      ACL_LOCKED(CHECK(acl_process_profiler_scan_chain(active_op)));
    }
    event->execution_status = CL_COMPLETE;
    clReleaseEvent(event);
    clReleaseMemObject(src_mem);
    clReleaseKernel(kernel);
  }
}

TEST(acl_profile, valid_checks) {
  acl_device_op_t op;
  op.info.type = ACL_DEVICE_OP_KERNEL;
  memset(op.timestamp, 0, sizeof(op.timestamp));
  op.info.event = 0;
  ACL_LOCKED(CHECK(!acl_event_is_valid(op.info.event)));
  ACL_LOCKED(CHECK(!acl_process_profiler_scan_chain(&op)));

  acl_set_allow_invalid_type<cl_event>(1);
  struct _cl_event fake_event = {0};
  op.info.event = &fake_event;
  ACL_LOCKED(CHECK(!acl_event_is_valid(op.info.event)));
  ACL_LOCKED(CHECK(!acl_process_profiler_scan_chain(&op)));
  acl_set_allow_invalid_type<cl_event>(0);
}

TEST(acl_profile, shared_counter_checks) {
  acl_test_unsetenv("ACL_INTERAL_SHARED_CONTROL_SETTING");
  set_env_shared_counter_val();
  CHECK_EQUAL(-1, get_env_profile_shared_counter_val());

  for (int i = 0; i < 4; i++) {
    const std::string env_value = std::to_string(i);
    acl_test_setenv("ACL_INTERAL_SHARED_CONTROL_SETTING", env_value.c_str());
    set_env_shared_counter_val();
    CHECK_EQUAL(i, get_env_profile_shared_counter_val());
  }

  acl_test_unsetenv("ACL_INTERAL_SHARED_CONTROL_SETTING");
}

TEST(acl_profile, init_profiler) {
  acl_test_setenv("ACL_RUNTIME_PROFILING_ACTIVE", "2");
  acl_test_setenv("ACL_PROFILE_TIMER", "2");
  ACL_LOCKED(acl_init_profiler());
  CHECK(!is_profile_enabled());
  CHECK(!is_profile_timer_on());

  acl_test_setenv("ACL_RUNTIME_PROFILING_ACTIVE", "1");
  acl_test_setenv("ACL_PROFILE_TIMER", "1");
  ACL_LOCKED(acl_init_profiler());
  CHECK(is_profile_enabled());
  CHECK(is_profile_timer_on());

  acl_test_unsetenv("ACL_RUNTIME_PROFILING_ACTIVE");
  acl_test_unsetenv("ACL_PROFILE_TIMER");
  ACL_LOCKED(acl_init_profiler());
  CHECK(!is_profile_enabled());
  CHECK(!is_profile_timer_on());
}

TEST(acl_profile, profiler_host_query_api) {
  ACL_LOCKED(acl_print_debug_msg("profiler_host_query_api\n"));

  cl_device_id device_id = m_device[0];
  cl_program program = m_program;
  cl_bool read_enqueue_kernels = CL_TRUE;
  cl_bool read_auto_enqueued = CL_FALSE;
  cl_bool clear_counters_after_readback = CL_FALSE;
  size_t param_value_size = 0;
  void *param_value = 0;
  size_t param_value_size_ret = 0;
  cl_int errcode_ret = CL_SUCCESS;

  // Test clGetProfileDataDeviceIntelFPGA
  // Good query
  CHECK_EQUAL(CL_SUCCESS,
              clGetProfileDataDeviceIntelFPGA(
                  device_id, program, read_enqueue_kernels, read_auto_enqueued,
                  clear_counters_after_readback, param_value_size, param_value,
                  &param_value_size_ret, &errcode_ret));

  // Bad query arguments
  // Invalid device_id
  cl_device_id invalid_device = (cl_device_id)(-1);
  CHECK_EQUAL(CL_INVALID_DEVICE,
              clGetProfileDataDeviceIntelFPGA(
                  invalid_device, program, read_enqueue_kernels,
                  read_auto_enqueued, clear_counters_after_readback,
                  param_value_size, param_value, &param_value_size_ret,
                  &errcode_ret));

  // Invalid program
  acl_set_allow_invalid_type<cl_program>(1);
  struct _cl_program fake_program = {0};
  CHECK_EQUAL(CL_INVALID_PROGRAM,
              clGetProfileDataDeviceIntelFPGA(
                  device_id, &fake_program, read_enqueue_kernels,
                  read_auto_enqueued, clear_counters_after_readback,
                  param_value_size, param_value, &param_value_size_ret,
                  &errcode_ret));
  acl_set_allow_invalid_type<cl_program>(0);

  // Trying to read for autorun kernel profiling data when there is no readback
  // kernel created
  cl_bool wrong_read_auto_enqueued = CL_TRUE;
  CHECK_EQUAL(CL_INVALID_KERNEL_NAME,
              clGetProfileDataDeviceIntelFPGA(
                  device_id, program, read_enqueue_kernels,
                  wrong_read_auto_enqueued, clear_counters_after_readback,
                  param_value_size, param_value, &param_value_size_ret,
                  &errcode_ret));
}

TEST(acl_profile, profile_info) {
  ACL_LOCKED(acl_print_debug_msg("profile_info_api\n"));
  cl_int status = CL_INVALID_VALUE;

  static cl_kernel kernel;
  static cl_mem src_mem;
  static cl_event event;
  static acl_device_op_t *active_op;

  if (threadNum() == 0) {
    kernel = clCreateKernel(m_program, "kernel6_profiletest", &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    // Set up kernel args
    size_t mem_size = 64;
    src_mem = clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 1, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 2, sizeof(cl_mem), &src_mem));

    const cl_uint work_dim = 3;
    size_t global_size[work_dim] = {192, 192, 64}; // 192 = 3 * 64
    ACL_LOCKED(acl_print_debug_msg("about to run clEnqueueNDRangeKernel\n"));
    status = clEnqueueNDRangeKernel(m_cq, kernel, 1, 0, global_size, 0, 0, 0,
                                    &event);
    CHECK_EQUAL(CL_SUCCESS, status);

    active_op = event->current_device_op;
    CHECK(active_op);
    CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, active_op->info.type);
    ACL_LOCKED(acl_set_device_op_execution_status(active_op, CL_RUNNING));
    ACL_LOCKED(acl_idle_update(m_context));
  }

  syncThreads();

  CHECK_EQUAL(CL_SUCCESS, clGetProfileInfoIntelFPGA(event));

  event->execution_status = CL_COMPLETE;
  CHECK_EQUAL(CL_INVALID_EVENT, clGetProfileInfoIntelFPGA(event));
  event->execution_status = CL_RUNNING;

  static cl_event fake_event;
  CHECK_EQUAL(CL_INVALID_EVENT, clGetProfileInfoIntelFPGA(fake_event));

  cl_device_id prev_device_id = event->command_queue->device;
  event->command_queue->device = NULL;
  CHECK_EQUAL(CL_INVALID_DEVICE, clGetProfileInfoIntelFPGA(event));
  event->command_queue->device = prev_device_id;

  cl_kernel prev_kernel = event->cmd.info.ndrange_kernel.kernel;
  event->cmd.info.ndrange_kernel.kernel = NULL;
  CHECK_EQUAL(CL_INVALID_KERNEL, clGetProfileInfoIntelFPGA(event));
  event->cmd.info.ndrange_kernel.kernel = prev_kernel;

  _cl_command_queue *prev_command_queue = event->command_queue;
  event->command_queue = NULL;
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, clGetProfileInfoIntelFPGA(event));
  event->command_queue = prev_command_queue;

  syncThreads();

  if (threadNum() == 0) {
    event->execution_status = CL_COMPLETE;
    clReleaseEvent(event);
    clReleaseMemObject(src_mem);
    clReleaseKernel(kernel);
  }
}

MT_TEST_GROUP(acl_no_profile) {
  // Preloads a standard platform, and finds all devices.
  enum { MAX_DEVICES = 100, m_num_devices_in_context = 3 };
  void setup() {
    if (threadNum() == 0) {
      // Remove PROFILE_MON if it exists.
      (void)remove(PROFILE_MON);

      acl_test_setup_generic_system();

      this->load();
      m_program = this->load_program();
      this->build(m_program);
    }
    syncThreads();
  }
  void teardown() {
    syncThreads();
    if (threadNum() == 0) {
      this->unload_program(m_program);
      this->unload();
      acl_test_teardown_generic_system();
    }
    syncThreads();
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

    m_cq = clCreateCommandQueue(m_context, m_device[0],
                                CL_QUEUE_PROFILING_ENABLE, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_cq);
  }

  void unload(void) {
    int cq_is_valid, context_is_valid;
    ACL_LOCKED(cq_is_valid = acl_command_queue_is_valid(m_cq));
    ACL_LOCKED(context_is_valid = acl_context_is_valid(m_context));
    if (cq_is_valid) {
      clReleaseCommandQueue(m_cq);
    }
    if (context_is_valid) {
      clReleaseContext(m_context);
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
  void unload_program(cl_program program) {
    int program_is_valid;
    ACL_LOCKED(program_is_valid = acl_program_is_valid(program));
    if (program_is_valid) {
      cl_int status;
      while (program->num_kernels) {
        clReleaseKernel(program->kernel_list->kernel);
      }
      CHECK_EQUAL(1, acl_ref_count(program));
      status = clReleaseProgram(program);
      CHECK_EQUAL(CL_SUCCESS, status);
    }
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
  void build(cl_program program) {
    CHECK_EQUAL(CL_SUCCESS, clBuildProgram(program, 0, 0, "", 0, 0));
  }

protected:
  static cl_platform_id m_platform;
  static cl_device_id m_device[MAX_DEVICES];
  static cl_uint m_num_devices;
  static cl_context m_context;
  static cl_program m_program;
  static cl_command_queue m_cq;
};

TEST_GROUP_STATIC(acl_no_profile, cl_platform_id, m_platform);
TEST_GROUP_STATIC(acl_no_profile, cl_device_id, m_device[MAX_DEVICES]);
TEST_GROUP_STATIC(acl_no_profile, cl_uint, m_num_devices);
TEST_GROUP_STATIC(acl_no_profile, cl_context, m_context);
TEST_GROUP_STATIC(acl_no_profile, cl_program, m_program);
TEST_GROUP_STATIC(acl_no_profile, cl_command_queue, m_cq);

MT_TEST(acl_no_profile, no_profiler) {
  ACL_LOCKED(acl_print_debug_msg("no_profiler\n"));
  cl_int status = CL_INVALID_VALUE;

  static cl_kernel kernel;
  static cl_mem src_mem;
  static cl_event event;

  if (threadNum() == 0) {
    kernel = clCreateKernel(m_program, "kernel6_profiletest", &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    // Set up kernel args
    size_t mem_size = 64;
    src_mem = clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 1, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 2, sizeof(cl_mem), &src_mem));

    const cl_uint work_dim = 3;
    size_t global_size[work_dim] = {192, 192, 64}; // 192 = 3 * 64
    ACL_LOCKED(acl_print_debug_msg("about to run clEnqueueNDRangeKernel\n"));
    status = clEnqueueNDRangeKernel(m_cq, kernel, 1, 0, global_size, 0, 0, 0,
                                    &event);
    CHECK_EQUAL(CL_SUCCESS, status);
  }

  syncThreads();

  static acl_device_op_t *active_op;

  if (threadNum() == 0) {
    active_op = event->current_device_op;
    CHECK(active_op);
    CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, active_op->info.type);
    ACL_LOCKED(acl_set_device_op_execution_status(active_op, CL_RUNNING));
    ACL_LOCKED(acl_idle_update(m_context));
  }

  syncThreads();

  syncThreads();

  if (threadNum() == 0) {
    FILE *fp = fopen(PROFILE_MON, "r");
    if (fp != NULL) {
      fclose(fp);
    }
    CHECK(fp == NULL);

    // Check that the low level call checks event completion status.
    event->execution_status = CL_COMPLETE;

    // Since profiling is disabled, the following profiler API calls should
    // return success
    ACL_LOCKED(
        CHECK(acl_process_profiler_scan_chain(event->current_device_op)));
    ACL_LOCKED(CHECK(acl_process_autorun_profiler_scan_chain(0, 0)));

    clReleaseEvent(event);
    clReleaseMemObject(src_mem);
    clReleaseKernel(kernel);
  }
}

TEST(acl_no_profile, profile_info) {
  ACL_LOCKED(acl_print_debug_msg("no_profiler_host_query_api\n"));
  cl_int status = CL_INVALID_VALUE;

  static cl_kernel kernel;
  static cl_mem src_mem;
  static cl_event event;

  if (threadNum() == 0) {
    kernel = clCreateKernel(m_program, "kernel6_profiletest", &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    // Set up kernel args
    size_t mem_size = 64;
    src_mem = clCreateBuffer(m_context, CL_MEM_READ_ONLY, mem_size, 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);

    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 0, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 1, sizeof(cl_mem), &src_mem));
    CHECK_EQUAL(CL_SUCCESS,
                clSetKernelArg(kernel, 2, sizeof(cl_mem), &src_mem));

    const cl_uint work_dim = 3;
    size_t global_size[work_dim] = {192, 192, 64}; // 192 = 3 * 64
    ACL_LOCKED(acl_print_debug_msg("about to run clEnqueueNDRangeKernel\n"));
    status = clEnqueueNDRangeKernel(m_cq, kernel, 1, 0, global_size, 0, 0, 0,
                                    &event);
    CHECK_EQUAL(CL_SUCCESS, status);
  }

  syncThreads();

  static acl_device_op_t *active_op;

  if (threadNum() == 0) {
    active_op = event->current_device_op;
    CHECK(active_op);
    CHECK_EQUAL(ACL_DEVICE_OP_KERNEL, active_op->info.type);
    ACL_LOCKED(acl_set_device_op_execution_status(active_op, CL_RUNNING));
    ACL_LOCKED(acl_idle_update(m_context));
  }

  syncThreads();

  CHECK_EQUAL(CL_OUT_OF_RESOURCES, clGetProfileInfoIntelFPGA(event));

  event->execution_status = CL_COMPLETE;
  CHECK_EQUAL(CL_INVALID_EVENT, clGetProfileInfoIntelFPGA(event));
  event->execution_status = CL_RUNNING;

  static cl_event fake_event;
  CHECK_EQUAL(CL_INVALID_EVENT, clGetProfileInfoIntelFPGA(fake_event));

  cl_device_id prev_device_id = event->command_queue->device;
  event->command_queue->device = NULL;
  CHECK_EQUAL(CL_INVALID_DEVICE, clGetProfileInfoIntelFPGA(event));
  event->command_queue->device = prev_device_id;

  cl_kernel prev_kernel = event->cmd.info.ndrange_kernel.kernel;
  event->cmd.info.ndrange_kernel.kernel = NULL;
  CHECK_EQUAL(CL_INVALID_KERNEL, clGetProfileInfoIntelFPGA(event));
  event->cmd.info.ndrange_kernel.kernel = prev_kernel;

  _cl_command_queue *prev_command_queue = event->command_queue;
  event->command_queue = NULL;
  CHECK_EQUAL(CL_INVALID_COMMAND_QUEUE, clGetProfileInfoIntelFPGA(event));
  event->command_queue = prev_command_queue;

  syncThreads();

  if (threadNum() == 0) {
    // Check that the low level call checks event completion status.
    event->execution_status = CL_COMPLETE;

    clReleaseEvent(event);
    clReleaseMemObject(src_mem);
    clReleaseKernel(kernel);
  }
}
