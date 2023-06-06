// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter
#pragma warning(disable : 4266) // no override available for virtual member
                                // function from base
#endif
#include <CppUTest/CommandLineTestRunner.h>
#include <CppUTest/SimpleString.h>
#include <CppUTest/TestHarness.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <assert.h>
#include <cmath>
#include <deque>
#include <iostream>
#include <map>
#include <numeric>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <time.h>

#include <CL/opencl.h>

#include <acl_auto_configure.h>
#include <acl_globals.h>
#include <acl_hal.h>
#include <acl_mem.h>
#include <acl_support.h>
#include <acl_thread.h>
#include <acl_util.h>
#include <pkg_editor/pkg_editor.h>
#include <unref.h>

#include "acl_fuzz_test.h"
#include "acl_globals_fuzz_test.h"
#include "acl_hal_fuzz_test.h"

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

// An example binary we cache for reuse many times, with offline-capture mode.
// It's built for the default board naned by ACLTEST_DEFAULT_BOARD
static unsigned char *acl_test_example_binary = 0;
static size_t acl_test_example_binary_len = 0;
// The corresponding sysdef. Used by some tests.
static acl_system_def_t acl_test_example_binary_sysdef{};

static void l_load_example_binary();
static void l_run_benchmark();

int main(int argc, const char **argv) {
  for (int i = 0; i < argc; ++i) {
    if (std::string(argv[i]) == std::string("--benchmark")) {
      l_run_benchmark();
      return 0;
    }
  }

  acl_test_unsetenv("CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA");
  acl_test_unsetenv("CL_CONTEXT_COMPILER_MODE_INTELFPGA");
  printf("sizeof platform %llu\n", (unsigned long long)sizeof(acl_platform));
  printf("sizeof context %llu\n",
         (unsigned long long)sizeof(struct _cl_context));
  printf("sizeof command queue %llu. Initially allocated %d queues\n",
         (unsigned long long)sizeof(struct _cl_command_queue),
         ACL_INIT_COMMAND_QUEUE_ALLOC);
  printf("sizeof event %llu\n", (unsigned long long)sizeof(struct _cl_event));
  printf("sizeof cl_mem %llu\n", (unsigned long long)sizeof(struct _cl_mem));
  printf("sizeof kernel %llu\n", (unsigned long long)sizeof(struct _cl_kernel));
  printf("sizeof device_op_queue %llu\n",
         (unsigned long long)sizeof(struct acl_device_op_queue_t));

  if (getenv("ACL_SKIP_BB")) { // for faster test turnaround in some cases
    printf("Skipping building binary\n");
  } else {
    l_load_example_binary();
  }

  return CommandLineTestRunner::RunAllTests(argc, argv);
}

void acl_test_setup_generic_system() {
  acl_mutex_wrapper.lock();
  assert(1 == acl_set_hal(acl_test_get_simple_hal()));
  assert(1 == acl_init(acl_test_get_complex_system_def()));
  acl_mutex_wrapper.unlock();
}

void acl_test_setup_empty_system() {
  acl_mutex_wrapper.lock();
  assert(1 == acl_set_hal(acl_test_get_simple_hal()));
  assert(1 == acl_init(acl_test_get_empty_system_def()));
  acl_mutex_wrapper.unlock();
}

void acl_test_setup_sample_default_board_system(void) {
  acl_mutex_wrapper.lock();
  assert(1 == acl_set_hal(acl_test_get_simple_hal()));
  assert(1 == acl_init(&acl_test_example_binary_sysdef));
  acl_mutex_wrapper.unlock();
}

void acl_test_teardown_sample_default_board_system(void) {
  acl_test_teardown_system();
}

void acl_test_teardown_generic_system(void) { acl_test_teardown_system(); }
void acl_test_teardown_system(void) {
  acl_mutex_wrapper.lock();
  acl_reset();
  acl_reset_hal();
  acltest_hal_teardown();
  acl_mutex_wrapper.unlock();
}

void acl_hal_test_setup_generic_system(void) { return; };

void acl_hal_test_teardown_generic_system(void) { return; };

void acl_test_run_standard_teardown_checks() {
  CHECK(!acl_is_locked());
  if (acl_get_num_alloc_cl_mem() != 0)
    printf("num aclloc cl_mem= %d\n", acl_get_num_alloc_cl_mem());
  CHECK_EQUAL(0, acl_get_num_alloc_cl_mem());
  CHECK_EQUAL(0, acl_get_num_alloc_cl_program());
  CHECK_EQUAL(0, acl_get_num_alloc_cl_context());
  acl_set_allow_invalid_type<cl_mem>(0);
  acl_set_allow_invalid_type<cl_program>(0);
  acl_set_allow_invalid_type<cl_context>(0);
  acl_set_allow_invalid_type<cl_event>(0);
}

TEST_GROUP(Min){};

TEST(Min, basic) {
  CHECK_EQUAL(-50, MIN(-50, 10));
  CHECK_EQUAL(10, MIN(10, 50));
  CHECK_EQUAL(19, MIN(19, 19));
}

#ifdef _WIN32
#define snprintf sprintf_s
#endif

SimpleString StringFrom(cl_uint x) {
  char buf[30]; // probably 12 will do
  snprintf(&buf[0], sizeof(buf) / sizeof(buf[0]), "%u", x);
  const char *start_of_buf = &buf[0];
  return StringFrom(start_of_buf);
}
#if ACL_TARGET_BIT == 32
SimpleString StringFrom(cl_ulong x) {
  char buf[30]; // probably 12 will do
  snprintf(&buf[0], sizeof(buf) / sizeof(buf[0]), "%lu", x);
  const char *start_of_buf = &buf[0];
  return StringFrom(start_of_buf);
}
#endif
#ifdef _WIN64
SimpleString StringFrom(intptr_t x) {
  char buf[30]; // probably 12 will do
  snprintf(&buf[0], sizeof(buf) / sizeof(buf[0]), "%Iu", x);
  const char *start_of_buf = &buf[0];
  return StringFrom(start_of_buf);
}
#endif
// If ACL_TARGET_BIT is 32, then size_t == cl_ulong == cl_uint, and we've
// already got a body for that.
#if ACL_TARGET_BIT > 32
SimpleString StringFrom(size_t x) {
  char buf[30]; // probably 12 will do
  snprintf(&buf[0], sizeof(buf) / sizeof(buf[0]), "%zd",
           x); // format string might be platform dependent..?
  const char *start_of_buf = &buf[0];
  return StringFrom(start_of_buf);
}
#endif

void acl_test_unsetenv(const char *var) {
#ifdef _WIN32
  _putenv_s(var, "");
#else
  unsetenv(var);
#endif
}

void acl_test_setenv(const char *var, const char *value) {
#ifdef _WIN32
  _putenv_s(var, value);
#else
  setenv(var, value, 1);
#endif
}

void CL_CALLBACK acl_test_notify_print(const char *errinfo,
                                       const void *private_info, size_t cb,
                                       void *user_data) {
  printf("Context error: %s\n", errinfo);
  cb = cb;                     // avoid warning on windows
  private_info = private_info; // avoid warning on windows
  user_data = user_data;       // avoid warning on windows
}

const unsigned char *acl_test_get_example_binary(size_t *binary_len) {
  *binary_len = acl_test_example_binary_len;
  return acl_test_example_binary;
}

static void l_load_example_binary(void) {
  const char *envvar_offline_device = "CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA";
  const char *envvar_program_lib =
      "CL_CONTEXT_PROGRAM_EXE_LIBRARY_ROOT_INTELFPGA";
  const char *offline_old_value = acl_getenv(envvar_offline_device);
  const char *program_lib_old_value = acl_getenv(envvar_program_lib);
  int system_ret = -1;
  enum { MAX_DEVICES = 100 };
  cl_platform_id platform;
  cl_device_id device[MAX_DEVICES];
  cl_context context;
  cl_program program;
  cl_int status;

  acl_test_setenv(envvar_offline_device, ACLTEST_DEFAULT_BOARD);
  acl_test_setenv(envvar_program_lib, ".acltest_builtin_prog");
  system_ret = system("rm -rf .acltest_builtin_prog");
  assert(system_ret != -1);

  ACL_LOCKED(acl_test_setup_generic_system());

  // Since this runs before the CppUTest runner is set up, we can't use
  // the CHECK* macros.
  // Just use asserts.

  assert(CL_SUCCESS == clGetPlatformIDs(1, &platform, 0));
  assert(CL_SUCCESS == clGetDeviceIDs(platform, CL_DEVICE_TYPE_ACCELERATOR,
                                      MAX_DEVICES, device, 0));

  cl_context_properties props[] = {
      CL_CONTEXT_COMPILER_MODE_INTELFPGA,
      CL_CONTEXT_COMPILER_MODE_OFFLINE_CREATE_EXE_LIBRARY_INTELFPGA, 0};
  context = clCreateContext(props, 1, device, acl_test_notify_print, 0, 0);
  assert(context);

  const char *src =
      "kernel void vecaccum(global int*A, global int*B) {\n"
      "   size_t gid = get_global_id(0);\n"
      "   A[gid] += B[gid];\n"
      "};\n"
      // This one has two constant arguments.
      "kernel void vecsum(global int*A, constant int*B, constant int*C) {\n"
      "   size_t gid = get_global_id(0);\n"
      "   A[gid] = B[gid] + C[gid];\n"
      "};\n"

      // This has a printf.
      "kernel void printit(global int*A) {\n"
      "   printf(\"Hello world! %d\\n\", A[0]);\n"
      "};\n";

  program = clCreateProgramWithSource(context, 1, &src, 0, 0);
  assert(program);

  status = clBuildProgram(program, 1, device, "-cl-kernel-arg-info", 0, 0);
  if (status != CL_SUCCESS) {
    printf("Compilation failed.  Kernel source is:\n-----\n%s\n----\n", src);
    size_t log_size = 0;
    clGetProgramBuildInfo(program, device[0], CL_PROGRAM_BUILD_LOG, 0, 0,
                          &log_size);
    char *log = (char *)acl_malloc(log_size);
    clGetProgramBuildInfo(program, device[0], CL_PROGRAM_BUILD_LOG, log_size,
                          log, 0);
    if (log)
      printf("Build log is:\n-----\n%s\n----\n", log);
    exit(1);
  }

  // The build log should not be empty
  size_t log_size = 0;
  size_t empty_log_size = 1;
  clGetProgramBuildInfo(program, device[0], CL_PROGRAM_BUILD_LOG, 0, 0,
                        &log_size);
  assert(log_size > empty_log_size);

  acl_test_example_binary_len = 0;
  assert(CL_SUCCESS == clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES,
                                        sizeof(size_t),
                                        &acl_test_example_binary_len, 0));
  acl_test_example_binary =
      (unsigned char *)acl_malloc(acl_test_example_binary_len);
  assert(acl_test_example_binary);
  assert(CL_SUCCESS == clGetProgramInfo(program, CL_PROGRAM_BINARIES,
                                        sizeof(acl_test_example_binary),
                                        &acl_test_example_binary, 0));

  // Save the derived sysdef for later tests.
  {
    acl_pkg_file_t pkg;
    size_t autodiscovery_len = 0;
    static char autodiscovery[4097];
    pkg = acl_pkg_open_file_from_memory((char *)acl_test_example_binary,
                                        acl_test_example_binary_len, 0);
    assert(pkg);

    assert(acl_pkg_section_exists(pkg, ACL_PKG_SECTION_AUTODISCOVERY,
                                  &autodiscovery_len));
    assert(autodiscovery_len < sizeof(autodiscovery)); // won't overflow.
    assert(acl_pkg_read_section(pkg, ACL_PKG_SECTION_AUTODISCOVERY,
                                autodiscovery, autodiscovery_len + 1));

    // Now parse it.
    static std::string errstr;
    auto result = false;
    ACL_LOCKED(result = acl_load_device_def_from_str(
                   std::string(autodiscovery),
                   acl_test_example_binary_sysdef.device[0]
                       .autodiscovery_def, // populating this
                   errstr));
    assert(result);
    ACL_LOCKED(acl_test_example_binary_sysdef.num_devices = 1);

    assert(ACLTEST_DEFAULT_BOARD ==
           acl_test_example_binary_sysdef.device[0].autodiscovery_def.name);
    acl_pkg_close_file(pkg);
  }

  // Don't leak
  clReleaseProgram(program);
  clReleaseContext(context);

  acl_test_unsetenv(envvar_offline_device);
  if (offline_old_value) {
    acl_test_setenv(envvar_offline_device, offline_old_value);
  }
  acl_test_unsetenv(envvar_program_lib);
  if (program_lib_old_value) {
    acl_test_setenv(envvar_program_lib, program_lib_old_value);
  }

  ACL_LOCKED(acl_test_teardown_generic_system());
}

// Return a context properties array that specifies preloaded binary only.
// This was the default for all releases prior to 13.0, but is not
// conformant.
cl_context_properties *acl_test_context_prop_preloaded_binary_only(void) {
  static cl_context_properties props[] = {
      CL_CONTEXT_COMPILER_MODE_INTELFPGA,
      (cl_context_properties)
          CL_CONTEXT_COMPILER_MODE_PRELOADED_BINARY_ONLY_INTELFPGA,
      0};
  return &(props[0]);
}

TEST_GROUP(envsets){void setup(){} void teardown(){}};

TEST(envsets, test) {
  const char *foo = "ACLFOO";
  acl_test_setenv("ACLFOO", "abc");
  const char *fooenv = acl_getenv(foo);
  CHECK(fooenv);
  CHECK_EQUAL(0, strncmp("abc", fooenv, MAX_NAME_SIZE));
  acl_test_unsetenv("ACLFOO");
  CHECK_EQUAL(0, acl_getenv(foo));
}

// --- benchmark ---------------------------------------------------------------

#ifdef _WIN32
static LONGLONG l_ticks_per_second = 0;
#endif

static inline cl_ulong l_get_timestamp() {
#ifdef __linux__
  struct timespec time;
#ifdef CLOCK_MONOTONIC_RAW
  int ret = clock_gettime(CLOCK_MONOTONIC_RAW, &time);
#else
  int ret = clock_gettime(CLOCK_MONOTONIC, &time);
#endif

  assert(ret == 0);
  return time.tv_sec * 1000 * 1000 * 1000 + time.tv_nsec;
#else
  LARGE_INTEGER li;
  double seconds;
  INT64 ticks;

  const INT64 NS_PER_S = 1000000000;

  QueryPerformanceCounter(&li);
  ticks = li.QuadPart;
  seconds = ticks / (double)l_ticks_per_second;
  return (cl_ulong)((double)seconds * (double)NS_PER_S + 0.5);
#endif
}

static inline void l_sleep(int milliseconds) {
#ifdef _WIN32
  Sleep((DWORD)milliseconds);
#else
  struct timespec delay;
  delay.tv_sec = milliseconds / 1000;
  delay.tv_nsec = (milliseconds % 1000) * 1000 * 1000;
  nanosleep(&delay, NULL);
#endif
}

static void l_generic_context_callback(const char *errinfo,
                                       const void *private_info, size_t cb,
                                       void *user_data) {
  UNREFERENCED_PARAMETER(private_info);
  UNREFERENCED_PARAMETER(cb);
  UNREFERENCED_PARAMETER(user_data);
  std::cout << "Error from context callback: " << errinfo << std::endl;
}

static void l_run_benchmark() {
  std::cout << "Starting benchmark..." << std::endl;

  if (debug_mode > 0) {
    std::cout << "WARNING! You are running this benchmark in debug mode!"
              << std::endl;
  }

#ifdef _WIN32
  LARGE_INTEGER li;
  QueryPerformanceFrequency(&li);
  l_ticks_per_second = li.QuadPart;
  assert(l_ticks_per_second != 0);
#endif

  acl_test_setup_generic_system();

  const int INNER_REPS = 100000;
  const int OUTER_REPS = 5;

  typedef std::deque<cl_ulong> times_t;
  typedef std::map<std::string, times_t> results_t;

  results_t results;

  for (int outer_rep = 0; outer_rep < OUTER_REPS; ++outer_rep) {
    std::cout << "Iteration " << (outer_rep + 1) << std::endl;

    cl_int status;
    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_program program;
    cl_command_queue cq;
    cl_mem mem;
    cl_kernel kernel;

    cl_ulong start_time, end_time;
    times_t *times;
    times_t *create_times;
    times_t *release_times;

    std::cout << "Measuring acl_lock/acl_unlock..." << std::endl;
    times = &results["acl_lock/acl_unlock"];
    for (int inner_rep = 0; inner_rep < INNER_REPS; ++inner_rep) {
      start_time = l_get_timestamp();
      acl_mutex_wrapper.lock();
      acl_mutex_wrapper.unlock();
      end_time = l_get_timestamp();
      times->push_back(end_time - start_time);
    }

    std::cout << "Measuring acl_assert_locked..." << std::endl;
    times = &results["acl_assert_locked"];
    for (int inner_rep = 0; inner_rep < INNER_REPS; ++inner_rep) {
      acl_mutex_wrapper.lock();
      start_time = l_get_timestamp();
      acl_assert_locked();
      end_time = l_get_timestamp();
      acl_mutex_wrapper.unlock();
      times->push_back(end_time - start_time);
    }

    std::cout << "Measuring clGetPlatformIDs..." << std::endl;
    times = &results["clGetPlatformIDs"];
    for (int inner_rep = 0; inner_rep < INNER_REPS; ++inner_rep) {
      start_time = l_get_timestamp();
      status = clGetPlatformIDs(1, &platform, 0);
      end_time = l_get_timestamp();
      times->push_back(end_time - start_time);
      assert(status == CL_SUCCESS);
    }

    std::cout << "Measuring clGetDeviceIDs..." << std::endl;
    times = &results["clGetDeviceIDs"];
    for (int inner_rep = 0; inner_rep < INNER_REPS; ++inner_rep) {
      start_time = l_get_timestamp();
      status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 1, &device, NULL);
      end_time = l_get_timestamp();
      times->push_back(end_time - start_time);
      assert(status == CL_SUCCESS);
    }

    std::cout << "Measuring clCreateContext/clReleaseContext..." << std::endl;
    create_times = &results["clCreateContext"];
    release_times = &results["clReleaseContext"];
    for (int inner_rep = 0; inner_rep < INNER_REPS; ++inner_rep) {
      start_time = l_get_timestamp();
      context =
          clCreateContext(acl_test_context_prop_preloaded_binary_only(), 1,
                          &device, l_generic_context_callback, 0, &status);
      end_time = l_get_timestamp();
      create_times->push_back(end_time - start_time);
      assert(status == CL_SUCCESS);
      assert(context);

      start_time = l_get_timestamp();
      status = clReleaseContext(context);
      end_time = l_get_timestamp();
      release_times->push_back(end_time - start_time);
      assert(status == CL_SUCCESS);
    }

    context = clCreateContext(acl_test_context_prop_preloaded_binary_only(), 1,
                              &device, 0, 0, &status);
    assert(status == CL_SUCCESS);
    assert(context);

    const unsigned char *binary = (const unsigned char *)"0";
    size_t binary_length = 1;
    program = clCreateProgramWithBinary(context, 1, &device, &binary_length,
                                        &binary, NULL, &status);
    assert(status == CL_SUCCESS);

    status = clBuildProgram(program, 1, &device, "", NULL, NULL);
    assert(status == CL_SUCCESS);

    std::cout << "Measuring clCreateCommandQueue/clReleaseCommandQueue..."
              << std::endl;
    create_times = &results["clCreateCommandQueue"];
    release_times = &results["clReleaseCommandQueue"];
    for (int inner_rep = 0; inner_rep < INNER_REPS; ++inner_rep) {
      start_time = l_get_timestamp();
      cq = clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE,
                                &status);
      end_time = l_get_timestamp();
      create_times->push_back(end_time - start_time);
      assert(status == CL_SUCCESS);
      assert(cq);

      start_time = l_get_timestamp();
      status = clReleaseCommandQueue(cq);
      end_time = l_get_timestamp();
      release_times->push_back(end_time - start_time);
      assert(status == CL_SUCCESS);
    }

    cq = clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE,
                              &status);
    assert(status == CL_SUCCESS);
    assert(cq);

    std::cout << "Measuring clCreateBuffer/clReleaseMemObject..." << std::endl;
    create_times = &results["clCreateBuffer"];
    release_times = &results["clReleaseMemObject"];
    for (int inner_rep = 0; inner_rep < INNER_REPS; ++inner_rep) {
      start_time = l_get_timestamp();
      mem = clCreateBuffer(context, CL_MEM_READ_ONLY, 64, 0, &status);
      end_time = l_get_timestamp();
      create_times->push_back(end_time - start_time);
      assert(status == CL_SUCCESS);
      assert(mem);

      start_time = l_get_timestamp();
      status = clReleaseMemObject(mem);
      end_time = l_get_timestamp();
      release_times->push_back(end_time - start_time);
      assert(status == CL_SUCCESS);
    }

    mem = clCreateBuffer(context, CL_MEM_READ_ONLY, 64, 0, &status);
    assert(status == CL_SUCCESS);
    assert(mem);

    std::cout << "Measuring clEnqueueWriteBuffer..." << std::endl;
    times = &results["clEnqueueWriteBuffer"];
    for (int inner_rep = 0; inner_rep < INNER_REPS; ++inner_rep) {
      char host_buf[64] = {0};
      start_time = l_get_timestamp();
      status = clEnqueueWriteBuffer(cq, mem, CL_FALSE, 0, 64, host_buf, 0, NULL,
                                    NULL);
      end_time = l_get_timestamp();
      times->push_back(end_time - start_time);
      assert(status == CL_SUCCESS);

      status = clFinish(cq);
      assert(status == CL_SUCCESS);
    }

    std::cout << "Measuring clEnqueueReadBuffer..." << std::endl;
    times = &results["clEnqueueReadBuffer"];
    for (int inner_rep = 0; inner_rep < INNER_REPS; ++inner_rep) {
      char host_buf[64] = {0};
      start_time = l_get_timestamp();
      status = clEnqueueReadBuffer(cq, mem, CL_FALSE, 0, 64, host_buf, 0, NULL,
                                   NULL);
      end_time = l_get_timestamp();
      times->push_back(end_time - start_time);
      assert(status == CL_SUCCESS);

      status = clFinish(cq);
      assert(status == CL_SUCCESS);
    }

    std::cout << "Measuring clCreateKernel/clReleaseKernel..." << std::endl;
    create_times = &results["clCreateKernel"];
    release_times = &results["clReleaseKernel"];
    for (int inner_rep = 0; inner_rep < INNER_REPS; ++inner_rep) {
      start_time = l_get_timestamp();
      kernel = clCreateKernel(program, "kernel4_task_double", &status);
      end_time = l_get_timestamp();
      create_times->push_back(end_time - start_time);
      assert(status == CL_SUCCESS);
      assert(kernel);

      start_time = l_get_timestamp();
      status = clReleaseKernel(kernel);
      end_time = l_get_timestamp();
      release_times->push_back(end_time - start_time);
      assert(status == CL_SUCCESS);
    }

    kernel = clCreateKernel(program, "kernel4_task_double", &status);
    assert(status == CL_SUCCESS);
    assert(kernel);

    std::cout << "Measuring clSetKernelArg..." << std::endl;
    times = &results["clSetKernelArg"];
    for (int inner_rep = 0; inner_rep < INNER_REPS; ++inner_rep) {
      start_time = l_get_timestamp();
      status = clSetKernelArg(kernel, 0, sizeof(cl_mem), &mem);
      end_time = l_get_timestamp();
      times->push_back(end_time - start_time);
      assert(status == CL_SUCCESS);
    }

    std::cout << "Measuring clEnqueueTask/clReleaseEvent..." << std::endl;
    create_times = &results["clEnqueueTask"];
    release_times = &results["clReleaseEvent"];
    for (int inner_rep = 0; inner_rep < INNER_REPS; ++inner_rep) {
      cl_event kernel_event;
      start_time = l_get_timestamp();
      status = clEnqueueTask(cq, kernel, 0, NULL, &kernel_event);
      end_time = l_get_timestamp();
      create_times->push_back(end_time - start_time);
      assert(status == CL_SUCCESS);

      // wait for kernel to be submitted
      cl_int execution_status = CL_QUEUED;
      while (execution_status != CL_SUBMITTED) {
        status = clGetEventInfo(kernel_event, CL_EVENT_COMMAND_EXECUTION_STATUS,
                                sizeof(cl_int), &execution_status, NULL);
        assert(status == CL_SUCCESS);
      }

      int activation_id = kernel_event->cmd.info.ndrange_kernel
                              .invocation_wrapper->image->activation_id;
      acltest_call_kernel_update_callback(activation_id, CL_RUNNING);
      acltest_call_kernel_update_callback(activation_id, CL_COMPLETE);

      status = clWaitForEvents(1, &kernel_event);
      assert(status == CL_SUCCESS);

      start_time = l_get_timestamp();
      status = clReleaseEvent(kernel_event);
      end_time = l_get_timestamp();
      release_times->push_back(end_time - start_time);
      assert(status == CL_SUCCESS);
    }

    status = clReleaseKernel(kernel);
    assert(status == CL_SUCCESS);
    status = clReleaseMemObject(mem);
    assert(status == CL_SUCCESS);
    status = clReleaseCommandQueue(cq);
    assert(status == CL_SUCCESS);
    status = clReleaseProgram(program);
    assert(status == CL_SUCCESS);
    status = clReleaseContext(context);
    assert(status == CL_SUCCESS);

    std::cout << std::endl;
    l_sleep(10 * 1000); // 10 seconds
  }

  acl_test_teardown_generic_system();

  std::cout << "Results:" << std::endl << std::endl;

  if (debug_mode > 0) {
    std::cout << "WARNING! You are running this benchmark in debug mode!"
              << std::endl;
  }

  std::cout << "Name,Time (ns),Standard Deviation (ns)" << std::endl;
  results_t::iterator iter = results.begin();
  results_t::iterator iter_end = results.end();
  for (; iter != iter_end; ++iter) {
    const std::string &name = iter->first;
    times_t &times = iter->second;
    assert(times.size() == INNER_REPS * OUTER_REPS);

    // The ULL at the end of the constant 0 is actually essential. The type
    // of this argument controls the type of the return value of
    // std::accumulate(). Without ULL, a 0 by itself is considered to be a
    // 32-bit int, which isn't wide enough to store the result.
    double mean =
        static_cast<double>(std::accumulate(times.begin(), times.end(), 0ULL)) /
        static_cast<double>(times.size());

    double stddev = 0;
    times_t::iterator iter2 = times.begin();
    times_t::iterator iter_end2 = times.end();
    for (; iter2 != iter_end2; ++iter2) {
      stddev += pow(static_cast<double>(*iter2) - mean, 2);
    }
    stddev = sqrt(stddev / static_cast<double>(times.size()));

    std::cout << name << "," << mean << "," << stddev << std::endl;
  }

  if (debug_mode > 0) {
    std::cout << "WARNING! You are running this benchmark in debug mode!"
              << std::endl;
  }

  std::cout << std::endl;
}
