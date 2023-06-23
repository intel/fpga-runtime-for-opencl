// Copyright (C) 2020-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause
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

MT_TEST_GROUP(acl_mem_device_global) {
public:
  enum { MEM_SIZE = 1024, SUBBUF_SIZE = 256 };

  void setup() {
    if (threadNum() == 0) {
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
      unload_context();

      if (m_context) {
        clReleaseContext(m_context);
        m_context = 0;
      }

      acl_test_teardown_generic_system();
    }
    syncThreads();
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

  cl_program load_program() {
    cl_int status;
    cl_program program;
    status = CL_INVALID_VALUE;
    const unsigned char *bin = (const unsigned char *)"0";
    size_t bin_length = 1;
    cl_int bin_status;
    program = clCreateProgramWithBinary(m_context, 1, &m_device[0], &bin_length,
                                        &bin, &bin_status, &status);
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
  cl_program m_program;

public:
  bool yeah;
};

#if 1
static void CL_CALLBACK notify_me_print(const char *errinfo,
                                        const void *private_info, size_t cb,
                                        void *user_data) {
  CppUTestGroupacl_mem_device_global *inst =
      (CppUTestGroupacl_mem_device_global *)user_data;
  if (inst) {
    if (inst->yeah)
      printf("Context error: %s\n", errinfo);
  }
  cb = cb;                     // avoid warning on windows
  private_info = private_info; // avoid warning on windows
}
#endif

MT_TEST(acl_mem_device_global, read_device_global) {
  ACL_LOCKED(acl_print_debug_msg("begin read_device_global\n"));
  char str[100];
  const size_t strsize = sizeof(str) / sizeof(char); // includes NUL (!)
  char resultbuf[strsize];
  cl_int status;
  cl_event read_event = 0;

  // Prepare host memory
  syncThreads();
  // Host pointer example
  void *src_ptr = malloc(strsize);
  CHECK(src_ptr != NULL);

  syncThreads();

  // Read from device global
  status = clEnqueueReadGlobalVariableINTEL(m_cq, m_program, "dev_global_name",
                                            CL_FALSE, strsize, 0, src_ptr, 0,
                                            NULL, &read_event);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Read from device global failed
  status = clEnqueueReadGlobalVariableINTEL(
      m_cq, m_program, "dev_global_name_not_exist", CL_FALSE, strsize, 0,
      src_ptr, 0, NULL, &read_event);
  CHECK_EQUAL(CL_INVALID_ARG_VALUE, status);

  // Block on all event completion
  CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));

  // Free host pointer
  free(src_ptr);

  ACL_LOCKED(acl_print_debug_msg("end read_device_global\n"));
}

MT_TEST(acl_mem_device_global, write_device_global) {
  ACL_LOCKED(acl_print_debug_msg("begin write_device_global\n"));
  char str[100];
  const size_t strsize = sizeof(str) / sizeof(char); // includes NUL (!)
  char resultbuf[strsize];
  cl_int status;
  cl_event write_event = 0;

  // Prepare host memory
  syncThreads();
  // Host pointer example
  void *src_ptr = malloc(strsize);
  CHECK(src_ptr != NULL);

  syncThreads();

  // Write device global
  status = clEnqueueWriteGlobalVariableINTEL(m_cq, m_program, "dev_global_name",
                                             CL_FALSE, strsize, 0, src_ptr, 0,
                                             NULL, &write_event);
  CHECK_EQUAL(CL_SUCCESS, status);

  // Write device global failed
  status = clEnqueueWriteGlobalVariableINTEL(
      m_cq, m_program, "dev_global_name_not_exist", CL_FALSE, strsize, 0,
      src_ptr, 0, NULL, &write_event);
  CHECK_EQUAL(CL_INVALID_ARG_VALUE, status);

  // Block on all event completion
  CHECK_EQUAL(CL_SUCCESS, clFinish(m_cq));

  // Free host pointer
  free(src_ptr);

  ACL_LOCKED(acl_print_debug_msg("end write_device_global\n"));
}

#endif // __arm__
