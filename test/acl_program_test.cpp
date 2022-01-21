// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef _MSC_VER
#pragma GCC diagnostic ignored "-Wconversion-null"
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter
#endif
#include <CppUTest/TestHarness.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <CL/opencl.h>

#include <stdio.h>
#include <string>

#include <acl.h>
#include <acl_context.h>
#include <acl_program.h>
#include <acl_support.h>
#include <acl_types.h>
#include <acl_util.h>

#include "acl_test.h"

// A default empty program source
#define EXAMPLE_SOURCE                                                         \
  const char *l_source[] = {"abc", "defgh"};                                   \
  const size_t l_source_lengths[] = {3, 5};                                    \
  const int l_num_sources = sizeof(l_source) / sizeof(l_source[0]);

// A default empty program binary
#define EXAMPLE_BINARY                                                         \
  const unsigned char *l_bin[m_num_devices_in_context];                        \
  unsigned char l_bin_values[m_num_devices_in_context][100];                   \
  size_t l_bin_lengths[m_num_devices_in_context];                              \
  for (size_t i = 0; i < m_num_devices_in_context; i++) {                      \
    l_bin_lengths[i] =                                                         \
        (size_t)sprintf((char *)l_bin_values[i], "bin%u", unsigned(i));        \
    l_bin[i] = l_bin_values[i];                                                \
  }

MT_TEST_GROUP(acl_program) {
  // Preloads a standard platform, and finds all devices.
  enum { MAX_DEVICES = 100, m_num_devices_in_context = 3 };
  void setup() {
    if (threadNum() == 0) {
      acl_test_setup_generic_system();
    }

    syncThreads();

    this->load();
  }
  void teardown() {
    clReleaseContext(m_context2);
    clReleaseContext(m_context);

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
  }

  cl_program load_program(int num_bins_to_load = m_num_devices_in_context) {
    cl_int status;
    cl_program program;
    EXAMPLE_BINARY;

    // Sometimes we avoid providing binaries for some devices.
    for (int i = num_bins_to_load; i < m_num_devices_in_context; i++) {
      l_bin_lengths[i] = 0;
      l_bin[i] = 0;
    }

    status = CL_INVALID_VALUE;
    program = clCreateProgramWithBinary(m_context, m_num_devices_in_context,
                                        &m_device[0], l_bin_lengths, l_bin,
                                        NULL, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(program);
    return program;
  }
  void unload_program(cl_program program) {
    cl_int status;
    CHECK_EQUAL(1, acl_ref_count(program));
    status = clReleaseProgram(program);
    CHECK_EQUAL(CL_SUCCESS, status);
  }

protected:
  cl_platform_id m_platform;
  cl_device_id m_device[MAX_DEVICES];
  cl_uint m_num_devices;
  cl_context m_context, m_context2;
};

MT_TEST(acl_program, link) {
  clRetainProgram(0);
  clReleaseProgram(0);
  ACL_LOCKED(acl_is_valid_ptr<cl_program>(0));
  ACL_LOCKED(acl_program_is_valid(0));
  CHECK_EQUAL(CL_SUCCESS, clUnloadCompiler());
  clCreateProgramWithSource(0, 0, 0, 0, 0);
  clCreateProgramWithBinary(0, 0, 0, 0, 0, 0, 0);
  clBuildProgram(0, 0, 0, 0, 0, 0);
  clBuildProgram(0, 0, 0, 0, 0, 0);
  clGetProgramInfo(0, 0, 0, 0, 0);
  clGetProgramBuildInfo(0, 0, 0, 0, 0, 0);
}

MT_TEST(acl_program, valid) {
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(1);
  syncThreads();
  struct _cl_program fake_program = {0};
  ACL_LOCKED(CHECK(!acl_is_valid_ptr<cl_program>(0)));
  ACL_LOCKED(CHECK(!acl_is_valid_ptr<cl_program>(&fake_program)));
  ACL_LOCKED(CHECK(!acl_program_is_valid(0)));
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(0);
  syncThreads();
}

MT_TEST(acl_program, retain_release_validity) {
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(1);
  syncThreads();
  struct _cl_program fake_program = {0};
  CHECK_EQUAL(CL_INVALID_PROGRAM, clRetainProgram(0));
  CHECK_EQUAL(CL_INVALID_PROGRAM, clRetainProgram(&fake_program));
  CHECK_EQUAL(CL_INVALID_PROGRAM, clReleaseProgram(0));
  CHECK_EQUAL(CL_INVALID_PROGRAM, clReleaseProgram(&fake_program));
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(0);
  syncThreads();
}

MT_TEST(acl_program, create_from_source_bad) {
  cl_int status;
  EXAMPLE_SOURCE;

  // Bad context
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  status = CL_SUCCESS;
  clCreateProgramWithSource(0, l_num_sources, l_source, l_source_lengths,
                            &status);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);
  status = CL_SUCCESS;
  clCreateProgramWithSource(&fake_context, l_num_sources, l_source,
                            l_source_lengths, &status);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();

  // Check count
  status = CL_SUCCESS;
  clCreateProgramWithSource(m_context, 0, l_source, l_source_lengths, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Check strings arg
  status = CL_SUCCESS;
  clCreateProgramWithSource(m_context, 1, 0, l_source_lengths, &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  const char *bad_sources[] = {"", 0 /* not allow NULL*/};
  status = CL_SUCCESS;
  clCreateProgramWithSource(m_context, 2, bad_sources, l_source_lengths,
                            &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Impossible to check the length arg.
}

MT_TEST(acl_program, create_from_source) {
  cl_int status;
  cl_program program;
  EXAMPLE_SOURCE;

  // Create a program
  status = CL_INVALID_VALUE;
  program = clCreateProgramWithSource(m_context, l_num_sources, l_source,
                                      l_source_lengths, &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(program);

  // Check program source result
  size_t size_ret;
  char ret_sources[100];
  CHECK_EQUAL(CL_SUCCESS,
              clGetProgramInfo(program, CL_PROGRAM_SOURCE, 0, 0, &size_ret));
  size_t total_len = 0;
  for (int i = 0; i < l_num_sources; i++) {
    total_len += l_source_lengths[i];
  }
  total_len += 1; // include the terminating nul.
  CHECK_EQUAL(total_len, size_ret);
  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(program, CL_PROGRAM_SOURCE,
                                           total_len, ret_sources, &size_ret));
  CHECK_EQUAL(total_len, size_ret);
  CHECK_EQUAL(0, strncmp("abcdefgh", ret_sources, total_len));

  // Check validity tests.
  ACL_LOCKED(CHECK(acl_is_valid_ptr(program)));
  ACL_LOCKED(CHECK(acl_program_is_valid(program)));

  CHECK(program);
  CHECK(acl_ref_count(program));

  // Check retain and release
  CHECK_EQUAL(1, acl_ref_count(program));
  CHECK_EQUAL(CL_SUCCESS, clRetainProgram(program));
  CHECK_EQUAL(2, acl_ref_count(program));
  ACL_LOCKED(CHECK(acl_program_is_valid(program)));
  CHECK_EQUAL(CL_SUCCESS, clRetainProgram(program));
  CHECK_EQUAL(3, acl_ref_count(program));
  CHECK_EQUAL(CL_SUCCESS, clReleaseProgram(program));
  CHECK_EQUAL(2, acl_ref_count(program));
  CHECK_EQUAL(CL_SUCCESS, clRetainProgram(program));
  CHECK_EQUAL(3, acl_ref_count(program));
  CHECK_EQUAL(CL_SUCCESS, clReleaseProgram(program));
  CHECK_EQUAL(2, acl_ref_count(program));
  CHECK_EQUAL(CL_SUCCESS, clReleaseProgram(program));
  CHECK_EQUAL(1, acl_ref_count(program));
  CHECK_EQUAL(CL_SUCCESS, clReleaseProgram(program));

  // All gone now...
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(1);
  syncThreads();
  struct _cl_program fake_program = {0};
  CHECK_EQUAL(CL_INVALID_PROGRAM, clRetainProgram(&fake_program));
  CHECK_EQUAL(CL_INVALID_PROGRAM, clReleaseProgram(&fake_program));
  ACL_LOCKED(CHECK(!acl_program_is_valid(&fake_program)));
  ACL_LOCKED(CHECK(!acl_is_valid_ptr(&fake_program)));
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(0);
  syncThreads();
}

MT_TEST(acl_program, create_from_binary_bad) {
  cl_int status;
  EXAMPLE_BINARY

  // Bad context
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  status = CL_SUCCESS;
  clCreateProgramWithBinary(0, 1, &m_device[0], l_bin_lengths, l_bin, NULL,
                            &status);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);

  status = CL_SUCCESS;
  clCreateProgramWithBinary(&fake_context, 1, &m_device[0], l_bin_lengths,
                            l_bin, NULL, &status);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();

  // Devices args
  status = CL_SUCCESS;
  clCreateProgramWithBinary(m_context, 0, m_device, l_bin_lengths, l_bin, NULL,
                            &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  status = CL_SUCCESS;
  clCreateProgramWithBinary(m_context, 1, 0, l_bin_lengths, l_bin, NULL,
                            &status);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // Bad device values
  cl_device_id bad_devices0[2] = {m_device[0], 0}; // bad device
  cl_device_id bad_devices1[2] = {m_device[0],
                                  (cl_device_id)&status}; // bad device ptr
  cl_device_id bad_devices2[2] = {
      m_device[0], m_device[m_num_devices_in_context]}; // wrong context

  status = CL_SUCCESS;
  clCreateProgramWithBinary(m_context, 2, bad_devices0, l_bin_lengths, l_bin,
                            NULL, &status);
  CHECK_EQUAL(CL_INVALID_DEVICE, status);

  status = CL_SUCCESS;
  clCreateProgramWithBinary(m_context, 2, bad_devices1, l_bin_lengths, l_bin,
                            NULL, &status);
  CHECK_EQUAL(CL_INVALID_DEVICE, status);

  status = CL_SUCCESS;
  clCreateProgramWithBinary(m_context, 2, bad_devices2, l_bin_lengths, l_bin,
                            NULL, &status);
  CHECK_EQUAL(CL_INVALID_DEVICE, status);

  // Impossible to check the length arg.
}

MT_TEST(acl_program, create_from_binary) {
  cl_int status;
  cl_program program;
  EXAMPLE_BINARY;

  // Create a program
  status = CL_INVALID_VALUE;
  program = clCreateProgramWithBinary(m_context, m_num_devices_in_context,
                                      &m_device[0], l_bin_lengths, l_bin, NULL,
                                      &status);
  CHECK_EQUAL(CL_SUCCESS, status);
  CHECK(program);

  // Check validity tests.
  ACL_LOCKED(CHECK(acl_is_valid_ptr(program)));
  ACL_LOCKED(CHECK(acl_program_is_valid(program)));

  CHECK(acl_ref_count(program));

  // Check release/retain
  CHECK_EQUAL(1, acl_ref_count(program));
  CHECK_EQUAL(CL_SUCCESS, clRetainProgram(program));
  CHECK_EQUAL(2, acl_ref_count(program));
  CHECK_EQUAL(CL_SUCCESS, clRetainProgram(program));
  CHECK_EQUAL(3, acl_ref_count(program));
  CHECK_EQUAL(CL_SUCCESS, clReleaseProgram(program));
  CHECK_EQUAL(2, acl_ref_count(program));
  CHECK_EQUAL(CL_SUCCESS, clReleaseProgram(program));
  CHECK_EQUAL(1, acl_ref_count(program));

  // Get rid of it.
  status = clReleaseProgram(program);
  CHECK_EQUAL(CL_SUCCESS, status);
}

MT_TEST(acl_program, create_from_builtin) {
  cl_int status;
  cl_program program;

  program = clCreateProgramWithBuiltInKernels(0, 0, 0, 0, &status);
  CHECK_EQUAL(NULL, program);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);

  program = clCreateProgramWithBuiltInKernels(m_context, 0, &m_device[0],
                                              "hello", &status);
  CHECK_EQUAL(NULL, program);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  program =
      clCreateProgramWithBuiltInKernels(m_context, 1, NULL, "hello", &status);
  CHECK_EQUAL(NULL, program);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  program = clCreateProgramWithBuiltInKernels(m_context, 1, &m_device[0], NULL,
                                              &status);
  CHECK_EQUAL(NULL, program);
  CHECK_EQUAL(CL_INVALID_VALUE, status);

  // We currently have no built-in kernels! so this call should fail all the
  // time!
  program = clCreateProgramWithBuiltInKernels(m_context, 1, &m_device[0],
                                              "hello", &status);
  CHECK_EQUAL(NULL, program);
  CHECK_EQUAL(CL_INVALID_VALUE, status);
}

MT_TEST(acl_program, build_bad) {
  cl_int status;
  cl_program program = this->load_program();

  // Bad program
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(1);
  syncThreads();
  struct _cl_program fake_program = {0};
  CHECK_EQUAL(CL_INVALID_PROGRAM, clBuildProgram(0, 0, 0, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_PROGRAM, clBuildProgram(&fake_program, 0, 0, 0, 0, 0));
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(0);
  syncThreads();

  // Bad device lists
  CHECK_EQUAL(CL_INVALID_VALUE,
              clBuildProgram(program, 0, &m_device[0], 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_VALUE, clBuildProgram(program, 1, 0, 0, 0, 0));

  // Bad notify.
  // Can't provide user data without notify fn.
  CHECK_EQUAL(CL_INVALID_VALUE, clBuildProgram(program, 0, 0, "", 0, &status));

  // If no binary associated with the program, then

  this->unload_program(program);
}

MT_TEST(acl_program, build_bad_invalid_bin_loaded) {
  cl_program program = this->load_program();

  this->unload_program(program);
}

MT_TEST(acl_program, build_ok_probe_data) {
  cl_program program = this->load_program();

  CHECK_EQUAL(CL_SUCCESS,
              clBuildProgram(program, 1, &m_device[0], "my options  ", 0, 0));

  acl_device_program_info_t *dev_prog = program->dev_prog[0];
  CHECK(dev_prog);

  CHECK_EQUAL(CL_BUILD_SUCCESS, dev_prog->build_status);

  CHECK("my options" == dev_prog->build_options);

  CHECK(dev_prog->build_log != "");
  ACL_LOCKED(acl_print_debug_msg("%s", dev_prog->build_log.c_str()));

  CHECK_EQUAL(CL_PROGRAM_BINARY_TYPE_EXECUTABLE,
              dev_prog->device_binary.get_binary_type());

  this->unload_program(program);
}

MT_TEST(acl_program, program_info) {
  cl_program program = this->load_program();
  cl_uint refcnt;
  size_t size_ret, num_kernels;
  char names[2000];

  // Bad program
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(1);
  syncThreads();
  struct _cl_program fake_program = {0};
  CHECK_EQUAL(CL_INVALID_PROGRAM,
              clGetProgramInfo(0, CL_PROGRAM_REFERENCE_COUNT, sizeof(refcnt),
                               &refcnt, &size_ret));
  CHECK_EQUAL(CL_INVALID_PROGRAM,
              clGetProgramInfo(&fake_program, CL_PROGRAM_REFERENCE_COUNT,
                               sizeof(refcnt), &refcnt, &size_ret));
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(0);
  syncThreads();

  // Bad param return args.
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetProgramInfo(program, CL_PROGRAM_REFERENCE_COUNT, 1, &refcnt,
                               &size_ret));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetProgramInfo(program, CL_PROGRAM_REFERENCE_COUNT,
                               sizeof(refcnt), 0, &size_ret));

  // Bad query
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetProgramInfo(program, (cl_program_info)ACL_OPEN,
                               sizeof(refcnt), &refcnt, &size_ret));

  // Good queries
  // Reference count
  refcnt = 10000000;
  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(program, CL_PROGRAM_REFERENCE_COUNT,
                                           sizeof(refcnt), &refcnt, &size_ret));
  CHECK_EQUAL(sizeof(refcnt), size_ret);
  CHECK_EQUAL(1, refcnt);
  CHECK_EQUAL(CL_SUCCESS, clRetainProgram(program));
  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(program, CL_PROGRAM_REFERENCE_COUNT,
                                           sizeof(refcnt), &refcnt, &size_ret));
  CHECK_EQUAL(sizeof(refcnt), size_ret);
  CHECK_EQUAL(2, refcnt);
  CHECK_EQUAL(CL_SUCCESS, clReleaseProgram(program));

  cl_context context = 0;
  CHECK_EQUAL(CL_SUCCESS,
              clGetProgramInfo(program, CL_PROGRAM_CONTEXT, sizeof(context),
                               &context, &size_ret));
  CHECK_EQUAL(sizeof(context), size_ret);
  CHECK_EQUAL(m_context, context);

  cl_uint num_devices;
  CHECK_EQUAL(CL_SUCCESS,
              clGetProgramInfo(program, CL_PROGRAM_NUM_DEVICES,
                               sizeof(num_devices), &num_devices, &size_ret));
  CHECK_EQUAL(sizeof(num_devices), size_ret);
  CHECK_EQUAL(m_num_devices_in_context, num_devices);

  cl_device_id device[ACL_MAX_DEVICE];
  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(program, CL_PROGRAM_DEVICES,
                                           sizeof(device), device, &size_ret));
  CHECK_EQUAL(sizeof(device[0]) * num_devices, size_ret);
  for (size_t i = 0; i < num_devices; i++) {
    // Overspecified here.  Order ought not to matter.
    CHECK_EQUAL(m_device[i], device[i]);
  }

  // Check we can query without providing underlying storage.
  CHECK_EQUAL(CL_SUCCESS,
              clGetProgramInfo(program, CL_PROGRAM_CONTEXT, 0, 0, &size_ret));
  CHECK_EQUAL(sizeof(cl_context), size_ret);

  // When created with binary, there is no source.
  CHECK_EQUAL(CL_SUCCESS,
              clGetProgramInfo(program, CL_PROGRAM_SOURCE, 0, 0, &size_ret));
  CHECK_EQUAL(0, size_ret);

  {

    EXAMPLE_BINARY
    // Check program binary queries.
    size_t ret_lengths[m_num_devices_in_context];
    char ret_binaries[m_num_devices_in_context][100];
    char *ret_binaries_ptr[m_num_devices_in_context];
    {
      for (unsigned i = 0; i < m_num_devices_in_context; i++) {
        ret_binaries_ptr[i] = ret_binaries[i];
      }
    }
    memset(ret_binaries, 0, sizeof(ret_binaries));

    // sizes
    // First just get size
    size_ret = 0;
    CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES,
                                             0, 0, &size_ret));
    CHECK_EQUAL(sizeof(size_t) * m_num_devices_in_context, size_ret);
    // now get the sizes
    size_ret = 0;
    CHECK_EQUAL(CL_SUCCESS,
                clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES,
                                 sizeof(ret_lengths), ret_lengths, &size_ret));
    CHECK_EQUAL(sizeof(size_t) * m_num_devices_in_context, size_ret);
    for (unsigned i = 0; i < m_num_devices_in_context; i++) {
      CHECK_EQUAL(l_bin_lengths[i], ret_lengths[i]);
    }
    // binaries themselves
    // First just get size
    size_ret = 0;
    CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(program, CL_PROGRAM_BINARIES, 0, 0,
                                             &size_ret));
    CHECK_EQUAL(sizeof(char *) * m_num_devices_in_context, size_ret);
    // now get the binaries themselves
    size_ret = 0;
    CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(program, CL_PROGRAM_BINARIES,
                                             sizeof(ret_binaries_ptr),
                                             ret_binaries_ptr, &size_ret));
    CHECK_EQUAL(sizeof(char *) * m_num_devices_in_context, size_ret);
    for (unsigned i = 0; i < m_num_devices_in_context; i++) {
      // They copied into the buffers we asked for.
      CHECK_EQUAL(ret_binaries_ptr[i], ret_binaries[i]);
      // The actual binaries are what we wanted
      CHECK_EQUAL(0, strncmp((const char *)l_bin[i], ret_binaries_ptr[i],
                             l_bin_lengths[i]));
    }
  }

  // info about kernels

  // bad ones:
  CHECK_EQUAL(
      CL_INVALID_VALUE,
      clGetProgramInfo(program, CL_PROGRAM_NUM_KERNELS, 0, &num_kernels, 0));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetProgramInfo(program, CL_PROGRAM_NUM_KERNELS, sizeof(size_t),
                               NULL, &size_ret));

  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetProgramInfo(program, CL_PROGRAM_KERNEL_NAMES, 0, names, 0));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetProgramInfo(program, CL_PROGRAM_KERNEL_NAMES, sizeof(size_t),
                               NULL, &size_ret));

  // before building the program
  // This won't happen if program is built with binary since we set the program
  // built stat. to success even before calling clbuildprogram
  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(program, CL_PROGRAM_NUM_KERNELS,
                                           sizeof(size_t), &num_kernels, 0));
  CHECK_EQUAL(15, num_kernels);

  // This won't happen if program is built with binary since we set the program
  // built stat. to success even before calling clbuildprogram
  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(program, CL_PROGRAM_KERNEL_NAMES, 0,
                                           NULL, &size_ret));
  CHECK_EQUAL(341, size_ret);

  CHECK_EQUAL(CL_SUCCESS, clBuildProgram(program, 0, 0, "", 0, 0));

  // after building the program
  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(program, CL_PROGRAM_NUM_KERNELS,
                                           sizeof(size_t), &num_kernels, 0));
  CHECK_EQUAL(15, num_kernels);

  CHECK_EQUAL(CL_SUCCESS,
              clGetProgramInfo(program, CL_PROGRAM_NUM_KERNELS, sizeof(size_t),
                               &num_kernels, &size_ret));
  CHECK_EQUAL(15, num_kernels);
  CHECK_EQUAL(sizeof(size_t), size_ret);

  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(program, CL_PROGRAM_NUM_KERNELS, 0,
                                           0, &size_ret));
  CHECK_EQUAL(sizeof(size_t), size_ret);

  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(program, CL_PROGRAM_KERNEL_NAMES, 0,
                                           NULL, &size_ret));
  CHECK_EQUAL(341, size_ret);
  // CHECK_EQUAL(321, size_ret);

  names[size_ret] = 100; // making sure extra bytes of memory are not affected.
  CHECK_EQUAL(CL_SUCCESS,
              clGetProgramInfo(program, CL_PROGRAM_KERNEL_NAMES,
                               2000 * sizeof(char), names, &size_ret));
  CHECK_EQUAL(341, size_ret); // only one kernel named: "foo"
  CHECK_EQUAL(100, names[size_ret]);
  CHECK_EQUAL(0, strcmp("kernel0_copy_vecin_vecout;"
                        "kernel11_task_double;"
                        "kernel12_task_double;"
                        "kernel13_multi_vec_lane;"
                        "kernel14_svm_arg_alignment;"
                        "kernel15_dev_global;"
                        "kernel1_vecadd_vecin_vecin_vecout;"
                        "kernel2_vecscale_vecin_scalar_vecout;"
                        "kernel3_locals;"
                        "kernel4_task_double;"
                        "kernel5_double;"
                        "kernel6_profiletest;"
                        "kernel7_emptyprofiletest;"
                        "kernel8_svm_args;"
                        "kernel9_image_args",
                        names));

  this->unload_program(program);
}

MT_TEST(acl_program, build_good) {
  cl_program program = this->load_program();

  ACL_LOCKED(CHECK(acl_program_is_valid(program)));
  CHECK_EQUAL(0, program->num_kernels);
  CHECK_EQUAL(m_num_devices_in_context, program->num_devices);

  CHECK_EQUAL(CL_SUCCESS, clBuildProgram(program, 0, 0, "", 0, 0));

  // NULL options string is acceptable, defacto follow Nvidia example.
  CHECK_EQUAL(CL_SUCCESS, clBuildProgram(program, 0, 0, 0, 0, 0));

  ACL_LOCKED(CHECK(acl_program_is_valid(program)));
  CHECK_EQUAL(0, program->num_kernels);
  CHECK_EQUAL(m_num_devices_in_context, program->num_devices);

  // Build info

  this->unload_program(program);
}

MT_TEST(acl_program, build_info) {
  cl_program program = this->load_program();
  cl_build_status build_status;
  cl_program_binary_type binary_type;
  size_t size_ret;
  char str_result[] = "hello";

  // Bad program
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(1);
  syncThreads();
  struct _cl_program fake_program = {0};
  CHECK_EQUAL(CL_INVALID_PROGRAM,
              clGetProgramBuildInfo(0, m_device[0], CL_PROGRAM_BUILD_STATUS,
                                    sizeof(build_status), &build_status,
                                    &size_ret));
  CHECK_EQUAL(
      CL_INVALID_PROGRAM,
      clGetProgramBuildInfo(&fake_program, m_device[0], CL_PROGRAM_BUILD_STATUS,
                            sizeof(build_status), &build_status, &size_ret));
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(0);
  syncThreads();

  // Bad device
  CHECK_EQUAL(CL_INVALID_DEVICE,
              clGetProgramBuildInfo(program, 0, CL_PROGRAM_BUILD_STATUS,
                                    sizeof(build_status), &build_status,
                                    &size_ret));
  CHECK_EQUAL(
      CL_INVALID_DEVICE,
      clGetProgramBuildInfo(program, (cl_device_id)1, CL_PROGRAM_BUILD_STATUS,
                            sizeof(build_status), &build_status, &size_ret));

  // Bad param return args.
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetProgramBuildInfo(program, m_device[0],
                                    CL_PROGRAM_BUILD_STATUS, 1, &build_status,
                                    &size_ret));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetProgramBuildInfo(program, m_device[0],
                                    CL_PROGRAM_BUILD_STATUS,
                                    sizeof(build_status), 0, &size_ret));

  // Bad query
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetProgramBuildInfo(
                  program, m_device[0], (cl_program_build_info)ACL_OPEN,
                  sizeof(build_status), &build_status, &size_ret));

  // Good queries
  build_status = ACL_OPEN;
  CHECK_EQUAL(CL_SUCCESS, clGetProgramBuildInfo(
                              program, m_device[0], CL_PROGRAM_BUILD_STATUS,
                              sizeof(build_status), &build_status, &size_ret));
  CHECK_EQUAL(sizeof(build_status), size_ret);
  CHECK_EQUAL(CL_BUILD_SUCCESS, build_status);

  build_status = ACL_OPEN;
  CHECK_EQUAL(CL_SUCCESS, clGetProgramBuildInfo(
                              program, m_device[m_num_devices_in_context - 1],
                              CL_PROGRAM_BUILD_STATUS, sizeof(build_status),
                              &build_status, &size_ret));
  CHECK_EQUAL(sizeof(build_status), size_ret);
  CHECK_EQUAL(CL_BUILD_SUCCESS, build_status);

  strncpy(str_result, "abc", 5);
  CHECK_EQUAL(CL_SUCCESS, clGetProgramBuildInfo(
                              program, m_device[0], CL_PROGRAM_BUILD_OPTIONS,
                              sizeof(str_result) / sizeof(str_result[0]),
                              str_result, &size_ret));
  CHECK_EQUAL(strlen(str_result) + 1,
              size_ret); // don't forget the trailing NUL
  CHECK_EQUAL(0, strncmp(str_result, "", 5));

  strncpy(str_result, "abc", 5);
  CHECK_EQUAL(CL_SUCCESS,
              clGetProgramBuildInfo(program, m_device[0], CL_PROGRAM_BUILD_LOG,
                                    sizeof(str_result) / sizeof(str_result[0]),
                                    str_result, &size_ret));
  CHECK_EQUAL(strlen(str_result) + 1,
              size_ret); // don't forget the trailing NUL
  CHECK_EQUAL(0, strncmp(str_result, "", 5));

  binary_type = (cl_program_binary_type)-1;
  CHECK_EQUAL(CL_SUCCESS, clGetProgramBuildInfo(
                              program, m_device[0], CL_PROGRAM_BINARY_TYPE,
                              sizeof(int), &binary_type, &size_ret));
  CHECK_EQUAL(sizeof(int), size_ret);
  // CHECK_EQUAL( CL_PROGRAM_BINARY_TYPE_NONE, binary_type ); //This cannot
  // happen when programs are created with binary, since the build status is
  // success without build, the type is already excecutable. Hence, next line:
  CHECK_EQUAL(CL_PROGRAM_BINARY_TYPE_EXECUTABLE,
              binary_type); // since the build status is success without build,
                            // the type is already excecutable.

  // Now build the program and query status again
  CHECK_EQUAL(CL_SUCCESS, clBuildProgram(program, 0, 0, "", 0, 0));

  build_status = ACL_OPEN;
  CHECK_EQUAL(CL_SUCCESS, clGetProgramBuildInfo(
                              program, m_device[0], CL_PROGRAM_BUILD_STATUS,
                              sizeof(build_status), &build_status, &size_ret));
  CHECK_EQUAL(sizeof(build_status), size_ret);
  CHECK_EQUAL(CL_BUILD_SUCCESS, build_status);

  build_status = ACL_OPEN;
  CHECK_EQUAL(CL_SUCCESS, clGetProgramBuildInfo(
                              program, m_device[m_num_devices_in_context - 1],
                              CL_PROGRAM_BUILD_STATUS, sizeof(build_status),
                              &build_status, &size_ret));
  CHECK_EQUAL(sizeof(build_status), size_ret);
  CHECK_EQUAL(CL_BUILD_SUCCESS, build_status);

  binary_type = (cl_program_binary_type)-1;
  CHECK_EQUAL(CL_SUCCESS, clGetProgramBuildInfo(
                              program, m_device[0], CL_PROGRAM_BINARY_TYPE,
                              sizeof(int), &binary_type, &size_ret));
  CHECK_EQUAL(sizeof(int), size_ret);
  CHECK_EQUAL(
      CL_PROGRAM_BINARY_TYPE_EXECUTABLE,
      binary_type); // Currently, we only have one type for a "built" program.

  this->unload_program(program);
}

//////////////////////////////////////////
//////////////////////////////////////////
// Building from source
//

MT_TEST_GROUP(from_source) {
  // Preloads a standard platform, and finds all devices.
  enum { MAX_DEVICES = 100 };
  void setup() {
    m_basedir = std::string(".from_source/") + std::to_string(threadNum());
    m_offline_device = ACLTEST_DEFAULT_BOARD;
    m_context = 0;
    m_program = 0;

    if (threadNum() == 0) {
      acl_test_setenv("CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA", m_offline_device);
      acl_test_setup_generic_system();
    }
    syncThreads();

    load_hashdir();
    this->load();
  }
  void teardown() {
    this->unload();

    syncThreads();
    if (threadNum() == 0) {
      acl_test_teardown_generic_system();
      acl_test_unsetenv("CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA");
    }

    acl_test_run_standard_teardown_checks();
  }

  void load(int compiler_mode =
                CL_CONTEXT_COMPILER_MODE_OFFLINE_CREATE_EXE_LIBRARY_INTELFPGA,
            const char *compile_command = 0, bool print_notify = true) {
    unload();
    CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &m_platform, 0));
    CHECK(acl_platform_is_valid(m_platform));
    CHECK_EQUAL(CL_SUCCESS,
                clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, MAX_DEVICES,
                               &m_device[0], &m_num_devices));
    CHECK(m_num_devices > 0);

    cl_int status = CL_INVALID_DEVICE;
    cl_context_properties props[] = {
        CL_CONTEXT_PROGRAM_EXE_LIBRARY_ROOT_INTELFPGA,
        (cl_context_properties)m_basedir.c_str(),
        CL_CONTEXT_COMPILER_MODE_INTELFPGA,
        compiler_mode,
        (compile_command ? CL_CONTEXT_COMPILE_COMMAND_INTELFPGA : 0),
        (cl_context_properties)compile_command,
        0,
        0};
    m_context =
        clCreateContext(props, 1, m_device,
                        (print_notify ? acl_test_notify_print : 0), 0, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
  }

  void unload(void) {
    unload_program();
    if (m_context) {
      clReleaseContext(m_context);
      m_context = 0;
    }
  }

  void load_program(void) {
    unload_program();
    cl_int status = CL_INVALID_VALUE;
    const char *kernel[] = {"kernel void foo(){}"};
    size_t len = strlen(kernel[0]);
    m_program = clCreateProgramWithSource(m_context, 1, kernel, &len, &status);
    CHECK_EQUAL(CL_SUCCESS, status);
  }

  void unload_program(void) {
    if (m_program) {
      clReleaseProgram(m_program);
      m_program = 0;
    }
  }

  void remove_hashdir_contents() {
    CHECK_EQUAL(0, system("rm -rf .from_source"));
  }

protected:
  cl_platform_id m_platform;
  cl_device_id m_device[MAX_DEVICES];
  cl_uint m_num_devices;
  cl_context m_context;
  cl_program m_program;
  std::string m_basedir;
  std::string m_hashdir;
  const char *m_offline_device;
  void load_hashdir(void) {
    static const char *const hash =
        ACDS_PRO == 0
            ? "71/d5/f16a4278ecbb06af25aaebe7d8abf4fe0b29"  // s5_net_small
            : "71/28/ac1c937694f5b54a12229f19e4ca3a494c16"; // a10_ref_small
    m_hashdir = m_basedir + std::string("/") + std::string(hash);
    CHECK(std::string(hash).length() < m_hashdir.length());
  }
};

MT_TEST(from_source, hash_dir_name) {

  std::string hash("abcdef...z");
  std::string expected =
      std::string(m_context->program_library_root) + "/ab/cd/ef...z";
  std::string result;
  ACL_LOCKED(result = acl_compute_hash_dir_name(m_context, "abcdef...z"));
  ACL_LOCKED(acl_print_debug_msg(" hashing: %s + %s --> %s %s\n",
                                 m_basedir.c_str(), hash.c_str(),
                                 expected.c_str(), result.c_str()));
  CHECK(expected == result);

  // error cases
  ACL_LOCKED(CHECK("" == acl_compute_hash_dir_name(m_context, "")));
  ACL_LOCKED(
      CHECK("" == acl_compute_hash_dir_name(m_context, "abc"))); // too short
}

/**
 * The following API calls work for building from source
 * 1) clBuildProgram
 * 2) clGetProgramBuildInfo -> sanity check only, extensive test in
 * MT_TEST(acl_program,build_info) 3) clGetProgramInfo -> sanity check only,
 * extensive test in MT_TEST(acl_program,program_info)
 */
MT_TEST(from_source, make_prog_dir_and_build_command) {
  cl_program_binary_type binary_type;
  size_t size_ret;
  size_t num_kernels;
  char names[2000];

  // Default arguments, just turning off printing of errors for invalid calls to
  // be tested below
  load(CL_CONTEXT_COMPILER_MODE_OFFLINE_CREATE_EXE_LIBRARY_INTELFPGA, 0, false);
  load_program();

  // Before build. No executable.
  binary_type = (cl_program_binary_type)-1;
  CHECK_EQUAL(CL_SUCCESS, clGetProgramBuildInfo(
                              m_program, m_device[0], CL_PROGRAM_BINARY_TYPE,
                              sizeof(int), &binary_type, &size_ret));
  CHECK_EQUAL(sizeof(int), size_ret);
  CHECK_EQUAL(CL_PROGRAM_BINARY_TYPE_NONE, binary_type);
  CHECK_EQUAL(CL_INVALID_PROGRAM_EXECUTABLE,
              clGetProgramInfo(m_program, CL_PROGRAM_NUM_KERNELS,
                               sizeof(size_t), &num_kernels, 0));
  CHECK_EQUAL(
      CL_INVALID_PROGRAM_EXECUTABLE,
      clGetProgramInfo(m_program, CL_PROGRAM_KERNEL_NAMES, 0, NULL, &size_ret));

  ACL_LOCKED(acl_print_debug_msg("Compile command '%s'\n",
                                 m_context->compile_command.c_str()));

  syncThreads();

  if (threadNum() == 0) {
    remove_hashdir_contents();
  }

  syncThreads();

  cl_int status = clBuildProgram(m_program, 1, m_device, "", 0, 0);
  CHECK_EQUAL(CL_SUCCESS, status);

  std::string path;
  ACL_LOCKED(CHECK((path = acl_realpath_existing(m_hashdir)) != ""));

  auto check_str = m_hashdir + std::string("/kernels.cl");
  ACL_LOCKED(CHECK((path = acl_realpath_existing(check_str)) != ""));
  check_str = m_hashdir + std::string("/build.cmd");
  ACL_LOCKED(CHECK((path = acl_realpath_existing(check_str)) != ""));

  // Probe the binary and package data.
  CHECK(m_program);
  CHECK(m_program->dev_prog[0]);
  CHECK(m_program->dev_prog[0]->device_binary.get_content());
  CHECK(0 < m_program->dev_prog[0]->device_binary.get_binary_len());
  CHECK(m_program->dev_prog[0]->device_binary.get_binary_pkg());

  // Check that we parsed the autodiscovery string.
  CHECK_EQUAL(1, m_program->dev_prog[0]
                     ->device_binary.get_devdef()
                     .autodiscovery_def.accel.size());
  CHECK("foo" == m_program->dev_prog[0]
                     ->device_binary.get_devdef()
                     .autodiscovery_def.accel[0]
                     .iface.name);
  CHECK_EQUAL(0, m_program->dev_prog[0]
                     ->device_binary.get_devdef()
                     .autodiscovery_def.accel[0]
                     .iface.args.size());

  // Most of the test for BuildInfo is in MT_TEST(acl_program,build_info)
  binary_type = (cl_program_binary_type)-1;
  CHECK_EQUAL(CL_SUCCESS, clGetProgramBuildInfo(
                              m_program, m_device[0], CL_PROGRAM_BINARY_TYPE,
                              sizeof(int), &binary_type, &size_ret));
  CHECK_EQUAL(sizeof(int), size_ret);
  CHECK_EQUAL(CL_PROGRAM_BINARY_TYPE_EXECUTABLE, binary_type);

  // Most of the test for ProgramInfo done in MT_TEST(acl_program,program_info)
  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(m_program, CL_PROGRAM_NUM_KERNELS,
                                           sizeof(size_t), &num_kernels, 0));
  CHECK_EQUAL(1, num_kernels);

  CHECK_EQUAL(CL_SUCCESS,
              clGetProgramInfo(m_program, CL_PROGRAM_NUM_KERNELS,
                               sizeof(size_t), &num_kernels, &size_ret));
  CHECK_EQUAL(1, num_kernels);
  CHECK_EQUAL(sizeof(size_t), size_ret);

  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(m_program, CL_PROGRAM_NUM_KERNELS, 0,
                                           0, &size_ret));
  CHECK_EQUAL(sizeof(size_t), size_ret);

  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(m_program, CL_PROGRAM_KERNEL_NAMES,
                                           0, NULL, &size_ret));
  CHECK_EQUAL(4, size_ret); // only one kernel named: "foo"

  names[size_ret] =
      100; // making sure return value doesn not affect extra bytes.
  CHECK_EQUAL(CL_SUCCESS,
              clGetProgramInfo(m_program, CL_PROGRAM_KERNEL_NAMES,
                               2000 * sizeof(char), names, &size_ret));
  CHECK_EQUAL(4, size_ret); // only one kernel named: "foo"
  CHECK_EQUAL(100, names[size_ret]);
  CHECK_EQUAL(0, strcmp("foo", names));
}

MT_TEST(from_source, online_mode) {
  // Actually run the kernel compiler command.
  // Pretend we have an online compiler
  unload();
  load(CL_CONTEXT_COMPILER_MODE_ONLINE_INTELFPGA, "aoc -rtl -v --no-env-check",
       false);
  load_program();

  ACL_LOCKED(acl_print_debug_msg("Compile command '%s'\n",
                                 m_context->compile_command.c_str()));

  syncThreads();

  if (threadNum() == 0) {
    remove_hashdir_contents();
  }

  syncThreads();

  CHECK(m_program);
  CHECK_EQUAL(0, m_program->dev_prog[0]);

  cl_int status = clBuildProgram(m_program, 1, m_device, "", 0, 0);
  // Expect it to fail because we didn't invoke the entire build.
  // We cut it short with "-rtl"
  CHECK_EQUAL(CL_BUILD_PROGRAM_FAILURE, status);

  // But we should have got far enough to produce the following outputs:

  ACL_LOCKED(
      acl_print_debug_msg(" check existing path %s\n", m_hashdir.c_str()));
  ACL_LOCKED(CHECK(acl_realpath_existing(m_hashdir) != ""));

  auto check_str = m_hashdir + std::string("/kernels.cl");
  ACL_LOCKED(CHECK(acl_realpath_existing(check_str) != ""));
  check_str = m_hashdir + std::string("/build.cmd");
  ACL_LOCKED(CHECK(acl_realpath_existing(check_str) != ""));
  // Since we invoked compiler with -rtl, we only get an .aoco and .aocr
  check_str = m_hashdir + std::string("/kernels.aocr");
  ACL_LOCKED(CHECK(acl_realpath_existing(check_str) != ""));
  check_str = m_hashdir + std::string("/kernels/kernels.v");
  ACL_LOCKED(CHECK(acl_realpath_existing(check_str) != ""));

  // Check the build log.
  size_t size_ret;
  enum { MAX_SIZE = 10240 };
  CHECK_EQUAL(CL_SUCCESS,
              clGetProgramBuildInfo(m_program, m_device[0],
                                    CL_PROGRAM_BUILD_LOG, 0, 0, &size_ret));
  CHECK(size_ret < MAX_SIZE);
  char build_log[MAX_SIZE];
  CHECK_EQUAL(CL_SUCCESS, clGetProgramBuildInfo(m_program, m_device[0],
                                                CL_PROGRAM_BUILD_LOG, size_ret,
                                                build_log, 0));

  // Yes, we built it.  But only part way.
  // So full online mode fails.
  CHECK(m_program->dev_prog[0]);
  CHECK_EQUAL(CL_BUILD_ERROR, m_program->dev_prog[0]->build_status);
  CHECK_EQUAL(
      0, m_program->dev_prog[0]
             ->device_binary
             .get_content()); // No binary, though since we wanted full .aclx
  CHECK_EQUAL(0, m_program->dev_prog[0]->device_binary.get_binary_len());
  CHECK_EQUAL(0, m_program->dev_prog[0]->device_binary.get_binary_pkg());

  ACL_LOCKED(acl_print_debug_msg("ALL DONE\n"));
}

MT_TEST(from_source, offline_mode_build_failure) {
  // Actually run the kernel compiler command.
  // Only the default compiler mode, i.e. offline.
  unload();
  load(CL_CONTEXT_COMPILER_MODE_OFFLINE_INTELFPGA, "aoc -rtl --no-env-check",
       false);
  load_program();

  // Attempt a build, but have it fail since no compiler should be available
  cl_int status = clBuildProgram(m_program, 1, m_device, "", 0, 0);
  CHECK_EQUAL(CL_COMPILER_NOT_AVAILABLE, status);

  // Should not have generated a binary!
  CHECK(m_program);
  CHECK_EQUAL(0, m_program->dev_prog[0]);

  // Now check some program queries that would normally access the dev_prog

  // Query sizes
  size_t size_ret = 99;
  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(m_program, CL_PROGRAM_BINARY_SIZES,
                                           0, 0, &size_ret));
  CHECK_EQUAL(sizeof(size_t), size_ret);
  // now get the sizes
  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(m_program, CL_PROGRAM_BINARY_SIZES,
                                           sizeof(size_ret), &size_ret, 0));
  CHECK_EQUAL(0, size_ret);

  // Try to get the binaries.
  unsigned char bin[1] = {'h'};
  unsigned char *bins[1] = {bin};
  size_ret = 99;
  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(m_program, CL_PROGRAM_BINARIES, 0, 0,
                                           &size_ret));
  CHECK_EQUAL(sizeof(bins), size_ret);
  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(m_program, CL_PROGRAM_BINARIES,
                                           sizeof(bins), bins, 0));
  // Should not have overwritten!
  CHECK_EQUAL('h', bins[0][0]);
  bins[0][0] = 'i';
  CHECK_EQUAL(CL_SUCCESS, clGetProgramInfo(m_program, CL_PROGRAM_BINARIES,
                                           sizeof(bins), bins, 0));
  CHECK_EQUAL('i', bins[0][0]);
}

MT_TEST(from_source, compile_program) {
  load(CL_CONTEXT_COMPILER_MODE_OFFLINE_INTELFPGA, "aoc -rtl --no-env-check",
       false);
  load_program();

  CHECK_EQUAL(CL_COMPILER_NOT_AVAILABLE,
              clCompileProgram(m_program, 1, m_device, "", 0, 0, 0, 0, 0));

  // Bad program
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(1);
  syncThreads();
  struct _cl_program fake_program = {0};
  CHECK_EQUAL(CL_INVALID_PROGRAM,
              clCompileProgram(0, 1, m_device, "", 0, 0, 0, 0, 0));
  CHECK_EQUAL(CL_INVALID_PROGRAM,
              clCompileProgram(&fake_program, 1, m_device, "", 0, 0, 0, 0, 0));
  syncThreads();
  acl_set_allow_invalid_type<cl_program>(0);
  syncThreads();
}

MT_TEST(from_source, link_program) {
  load(CL_CONTEXT_COMPILER_MODE_OFFLINE_INTELFPGA, "aoc -rtl --no-env-check",
       false);
  load_program();

  cl_int status = CL_SUCCESS;
  clLinkProgram(m_context, 1, m_device, "", 1, &m_program, 0, 0, &status);
  CHECK_EQUAL(CL_LINKER_NOT_AVAILABLE, status);

  // Bad context
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(1);
  syncThreads();
  struct _cl_context fake_context = {0};
  status = CL_SUCCESS;
  clLinkProgram(0, 1, m_device, "", 1, &m_program, 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);
  status = CL_SUCCESS;
  clLinkProgram(&fake_context, 1, m_device, "", 1, &m_program, 0, 0, &status);
  CHECK_EQUAL(CL_INVALID_CONTEXT, status);
  syncThreads();
  acl_set_allow_invalid_type<cl_context>(0);
  syncThreads();
}
