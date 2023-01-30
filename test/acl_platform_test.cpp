// Copyright (C) 2010-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter
#pragma warning(disable : 4266) // no override available for virtual member
                                // function from base
#endif
#include <CppUTest/TestHarness.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <CL/opencl.h>

#include <cinttypes>
#include <stdio.h>
#include <string>
#include <vector>

#include <acl.h>
#include <acl_globals.h>
#include <acl_hal.h>
#include <acl_platform.h>
#include <acl_support.h>
#include <acl_util.h>

#include "acl_globals_test.h"
#include "acl_test.h"

MT_TEST_GROUP(GetPlatforms){
    void setup(){if (threadNum() == 0){acl_test_setup_generic_system();
}
syncThreads();
}
void teardown() {
  syncThreads();
  if (threadNum() == 0) {
    acl_test_teardown_generic_system();
  }
  acl_test_run_standard_teardown_checks();
}
}
;

MT_TEST(GetPlatforms, basic) {
  const cl_uint max = 20;
  cl_platform_id p[max];
  cl_uint num_platforms = 0;

  // Invalid fill-in-array args
  num_platforms = 42;
  CHECK_EQUAL(CL_INVALID_VALUE, clGetPlatformIDs(0, &p[0], &num_platforms));
  CHECK_EQUAL(1, num_platforms);

  // Valid to send null for platforms ret value
  num_platforms = 42;
  CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, 0, &num_platforms));
  CHECK_EQUAL(1, num_platforms);

  num_platforms = 42;
  CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(0, 0, &num_platforms));
  CHECK_EQUAL(1, num_platforms);

  // Must request *something*
  CHECK_EQUAL(CL_INVALID_VALUE, clGetPlatformIDs(0, 0, 0));
  // Valid to send null for num_platforms ret value
  CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(max, &p[0], 0));

  // Now check return values.
  for (size_t i = 0; i < max; i++) {
    p[i] = 0;
  }
  CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(max, &p[0], &num_platforms));
  CHECK(num_platforms > 0);
  for (size_t i = 0; i < MIN(max, num_platforms); i++) {
    CHECK(p[i] != 0)
  }
  for (size_t i = MIN(max, num_platforms); i < max; i++) {
    CHECK(p[i] == 0)
  }

  // check internal APIs
  ACL_LOCKED(CHECK(!acl_platform_is_valid(0)));
  ACL_LOCKED(
      CHECK(!acl_platform_is_valid((cl_platform_id)&max))); // random pointer
  ACL_LOCKED(CHECK(acl_platform_is_valid(p[0])));

  ACL_LOCKED(CHECK(!acl_is_valid_ptr<cl_context>(0)));
  ACL_LOCKED(CHECK(!acl_context_is_valid(0)));
  ACL_LOCKED(CHECK(!acl_device_is_valid(0)));
  ACL_LOCKED(CHECK(!acl_command_queue_is_valid(0)));
  ACL_LOCKED(CHECK(!acl_is_valid_ptr<cl_mem>(0)));
  ACL_LOCKED(CHECK(!acl_mem_is_valid(0)));
  ACL_LOCKED(CHECK(!acl_is_valid_ptr<cl_event>(0)));
  ACL_LOCKED(CHECK(!acl_event_is_valid(0)));

  static AtomicUInt num_cq;
  if (threadNum() == 0) {
    num_cq.unsafeReset();
  }
  syncThreads();
}

MT_TEST_GROUP(PlatformInfo) {
public:
  void setup() {
    if (threadNum() == 0) {
      acl_test_setup_generic_system();
    }

    syncThreads();

    clGetPlatformIDs(1, &m_platform, 0);
  }
  void teardown() {
    m_platform = 0;

    syncThreads();

    if (threadNum() == 0) {
      acl_test_teardown_generic_system();
    }

    acl_test_run_standard_teardown_checks();
  }

protected:
  cl_platform_id m_platform;
};

MT_TEST(PlatformInfo, basic) {
  char str[1024]; // must be big enough to hold biggest property
  char short_str[1];
  const size_t str_size = sizeof(str) / sizeof(str[0]);
  const size_t short_str_size = sizeof(short_str) / sizeof(short_str[0]);
  size_t size_ret;
  cl_platform_info queries[] = {CL_PLATFORM_PROFILE, CL_PLATFORM_VERSION,
                                CL_PLATFORM_NAME, CL_PLATFORM_VENDOR,
                                CL_PLATFORM_EXTENSIONS};

  ACL_LOCKED(CHECK(acl_platform_is_valid(m_platform)));

  // Check platform arg
  CHECK_EQUAL(CL_INVALID_PLATFORM,
              clGetPlatformInfo(0, queries[0], 1, &str[0], &size_ret));
  CHECK_EQUAL(
      CL_INVALID_PLATFORM,
      clGetPlatformInfo((cl_platform_id)1, queries[0], 1, &str[0], &size_ret));

  // Good cases:
  // Ensure we can query all info enums, and in various combinations of
  // returning the values.
  for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); i++) {
    size_ret = 0;
    CHECK_EQUAL(CL_SUCCESS,
                clGetPlatformInfo(m_platform, queries[i], 0, 0, &size_ret));
    CHECK(size_ret < str_size); // If this fails, just make str[] bigger.
    CHECK_EQUAL(CL_SUCCESS, clGetPlatformInfo(m_platform, queries[i], str_size,
                                              &str[0], 0));
    CHECK_EQUAL(CL_SUCCESS, clGetPlatformInfo(m_platform, queries[i], str_size,
                                              &str[0], &size_ret));

    // Non empty string.  Is this a functional requirement?
    if (queries[i] != CL_PLATFORM_EXTENSIONS) {
      CHECK(size_ret > 0);
      CHECK(str[0] != 0);
    }
  }

  // Check retvalue args
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetPlatformInfo(m_platform, queries[0], 0, &str[0], &size_ret));
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetPlatformInfo(m_platform, queries[0], 1, 0, &size_ret));

  // That constraint is oddly missing from the API spec, even though other
  // query APIs have it.
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetPlatformInfo(m_platform, queries[0], 0, 0, 0));

  // Check string length check
  for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); i++) {
    if (queries[i] != CL_PLATFORM_EXTENSIONS) {
      CHECK_EQUAL(CL_INVALID_VALUE,
                  clGetPlatformInfo(m_platform, queries[i], short_str_size,
                                    &short_str[0], &size_ret));
    }
  }

  // Invalid info query enum
  CHECK_EQUAL(CL_INVALID_VALUE,
              clGetPlatformInfo(m_platform, 0, str_size, &str[0], &size_ret));

  // Specific value checks.
  CHECK_EQUAL(CL_SUCCESS, clGetPlatformInfo(m_platform, CL_PLATFORM_VERSION,
                                            str_size, &str[0], 0));
  // Prefix check on platform version.
  const char *strstr_result = strstr(&str[0], "OpenCL 1.");
  CHECK(&str[0] == strstr_result);
}

MT_TEST(PlatformInfo, size) {
  ACL_LOCKED(debug_mode++);
  ACL_LOCKED(acl_print_debug_msg("\n"));
#define SHOWSIZE(X)                                                            \
  ACL_LOCKED(acl_print_debug_msg("   sizeof(%-30s) = %8zu\n", #X, sizeof(X)));

  SHOWSIZE(_cl_platform_id);
  SHOWSIZE(_cl_context);
  SHOWSIZE(_cl_device_id);
  SHOWSIZE(_cl_command_queue);
  SHOWSIZE(_cl_event);
  SHOWSIZE(_cl_mem);
  SHOWSIZE(_cl_kernel);
  SHOWSIZE(_cl_program);
  SHOWSIZE(acl_mem_region_t);
  SHOWSIZE(acl_command_info_t);

  ACL_LOCKED(debug_mode--);
}

MT_TEST_GROUP(offline_device) {

  void setup() {
    if (threadNum() == 0) {
      m_env = "CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA";
      acl_test_unsetenv(m_env);
    }
    syncThreads();
  }
  void teardown() {
    syncThreads();
    if (threadNum() == 0) {
      acl_test_unsetenv(m_env);
    }
    acl_test_run_standard_teardown_checks();
  }

protected:
  const char *m_env = nullptr;
};

MT_TEST(offline_device, count_devices) {
  for (int use_offline = 0; use_offline < 2; use_offline++) {
    for (int plus_offline = 0; plus_offline < 1 + use_offline; plus_offline++) {
      for (int use_empty = 0; use_empty < 2; use_empty++) {

        const char *offline_device = ACLTEST_DEFAULT_BOARD;
        char offline_setting[100] = {0};

        syncThreads();

        if (threadNum() == 0) {
          if (use_offline) {
            sprintf(offline_setting, "%s%s", (plus_offline ? "+" : ""),
                    offline_device);
            acl_test_setenv(m_env, offline_setting);
          }

          if (use_empty) {
            acl_test_setup_empty_system();
          } else {
            acl_test_setup_generic_system();
          }
        }

        syncThreads();

        // Don't have a HAL until the system is setup

        ACL_LOCKED(
            acl_print_debug_msg("\n\n use_offline %d '%s'  use_empty %d\n",
                                use_offline, offline_setting, use_empty));

        enum { N = 100 };
        cl_platform_id platform;
        cl_device_id device[N];

        CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &platform, 0));
        cl_uint num_devices = 0;
        cl_int status = 42;

        ACL_LOCKED(
            acl_print_debug_msg(" empty_mumdev %d\n",
                                acl_test_get_empty_system_def()->num_devices));
        ACL_LOCKED(acl_print_debug_msg(" nd %d\n", acl_platform.num_devices));

        status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, N, device,
                                &num_devices);
        CHECK_EQUAL(CL_DEVICE_NOT_FOUND, status);
        status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, N, device,
                                &num_devices);
        CHECK_EQUAL(CL_DEVICE_NOT_FOUND, status);

        status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, N, device,
                                &num_devices);

        cl_context context = NULL;

        if (use_empty && !use_offline) {
          CHECK_EQUAL(0, num_devices);
          CHECK_EQUAL(CL_DEVICE_NOT_FOUND, status);
        } else {
          CHECK(num_devices > 0);
          CHECK_EQUAL(CL_SUCCESS, status);
          // Have to create context to force the finalization of the platform
          // init
          context = clCreateContext(0, num_devices, device, 0, 0, &status);
        }

        int num_absent = 0;
        int num_present = 0;
        bool found_it = false;
        for (unsigned i = 0; i < num_devices; i++) {
          if (device[i]->present) {
            num_present++;
          } else {
            num_absent++;
          }
          int this_one =
              offline_device == device[i]->def.autodiscovery_def.name;
          found_it = found_it || this_one;
          ACL_LOCKED(
              acl_print_debug_msg(" device[%d] = %s present? %d \n", i,
                                  device[i]->def.autodiscovery_def.name.c_str(),
                                  device[i]->present));
        }
        ACL_LOCKED(acl_print_debug_msg(" present %d absent %d\n", num_present,
                                       num_absent));

        // Check number of present devices.
        const bool offline_only = use_offline && !plus_offline;
        if (offline_only) {
          CHECK_EQUAL(0, num_present);
        } else {
          if (use_empty) {
            CHECK_EQUAL(0, num_present);
          } else {
            CHECK(num_present > 0);
          }
        }

        if (use_offline) {
          CHECK_EQUAL(1, num_absent);
        } else {
          CHECK_EQUAL(0, num_absent);
        }

        CHECK(num_absent <= 1);

        if (use_offline) {
          CHECK(found_it);
        }

        if (context) {
          clReleaseContext(context);
        }

        syncThreads();

        if (threadNum() == 0) {
          acl_test_unsetenv(m_env);

          acl_test_teardown_system();
          CHECK_EQUAL(0, acl_getenv(m_env));
        }

        syncThreads();
      }
    }
  }
}

// Make sure we get the offline HAL.
TEST(offline_device, offline_hal) {
  const char *offline_device = ACLTEST_DEFAULT_BOARD;
  cl_bool result;

  acl_test_setenv(m_env, offline_device);
  ACL_LOCKED(acl_reset());
  ACL_LOCKED(result = acl_init_from_hal_discovery());
  CHECK_EQUAL(CL_TRUE, result);
  // Exercise the offline HAL: printing, and the timestamps.

  CHECK_EQUAL(1, acl_platform.num_devices);

  // We might be testing on a machine that has this board!!!
  cl_ulong now;
  ACL_LOCKED(now = acl_get_hal()->get_timestamp());
  ACL_LOCKED(acl_print_debug_msg("offline hal time is %08" PRIx64 "%08" PRIx64,
                                 (now >> 32), (now & 0xffffffff)));
}

struct live_info_t {
  void *object;
  const char *type_name;
  cl_uint refcount;
};

typedef std::vector<live_info_t> live_info_vec_t;

void leak_notify(void *user_data, void *object, const char *type_name,
                 cl_uint refcount) {
  live_info_vec_t *vec = (live_info_vec_t *)user_data;
  live_info_t info;
  info.object = object;
  info.type_name = type_name;
  info.refcount = refcount;
  vec->push_back(info);
}

void dump_leaks(live_info_vec_t &vec) {
  for (size_t i = 0; i < vec.size(); i++) {
    live_info_t &info = vec[i];
    ACL_LOCKED(acl_print_debug_msg(" [%d]  %p %s %u\n", (int)i, info.object,
                                   info.type_name, info.refcount));
  }
}

MT_TEST_GROUP(track_object) {
  void setup() {
    if (threadNum() == 0) {
      m_context = 0;
      m_program = 0;
      m_kernel = 0;
      m_mem0 = 0;
      m_mem1 = 0;
      m_offline_env = "CL_CONTEXT_OFFLINE_DEVICE_INTELFPGA";
      acl_test_setenv(m_offline_env, ACLTEST_DEFAULT_BOARD);
    }
    syncThreads();
  }
  void teardown() {
    syncThreads();
    if (threadNum() == 0) {
      acl_test_unsetenv(m_offline_env);
      ACL_LOCKED(acl_reset());
    }
    acl_test_run_standard_teardown_checks();
  }
  void load_platform(void) {
    cl_uint num = 0;
    CHECK_EQUAL(CL_SUCCESS, clGetPlatformIDs(1, &m_platform, &num));
    CHECK_EQUAL(1, num);
  }
  void load_stuff(void) {
    cl_int status;
    CHECK_EQUAL(CL_SUCCESS,
                clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ACCELERATOR, 1,
                               &m_device, 0));
    CHECK(m_device);
    cl_context_properties properties[] = {
        CL_CONTEXT_COMPILER_MODE_INTELFPGA,
        CL_CONTEXT_COMPILER_MODE_OFFLINE_CREATE_EXE_LIBRARY_INTELFPGA, 0, 0};
    CHECK((m_context = clCreateContext(properties, 1, &m_device,
                                       acl_test_notify_print, 0, &status)));
    CHECK_EQUAL(CL_SUCCESS, status);

    size_t example_bin_len = 0;
    const unsigned char *example_bin =
        acl_test_get_example_binary(&example_bin_len);

    m_program = clCreateProgramWithBinary(m_context, 1, &m_device,
                                          &example_bin_len, &example_bin, 0, 0);
    CHECK(m_program);
    CHECK_EQUAL(CL_SUCCESS, clBuildProgram(m_program, 0, 0, "", 0, 0));

    m_kernel = clCreateKernel(m_program, "vecaccum", &status);
    CHECK_EQUAL(CL_SUCCESS, status);
    CHECK(m_kernel);

    m_mem0 = clCreateBuffer(m_context, CL_MEM_READ_WRITE, 2048, 0, 0);
    m_mem1 = clCreateBuffer(m_context, CL_MEM_READ_WRITE, 2048, 0, 0);
    CHECK(m_mem0);
    CHECK(m_mem1);

    m_event = clCreateUserEvent(m_context, 0);
    CHECK(m_event);

    m_cq0 =
        clCreateCommandQueue(m_context, m_device, CL_QUEUE_PROFILING_ENABLE, 0);
    CHECK(m_cq0);
    m_cq1 =
        clCreateCommandQueue(m_context, m_device, CL_QUEUE_PROFILING_ENABLE, 0);
    CHECK(m_cq1);
  }

protected:
  static const char *m_offline_env;
  cl_platform_id m_platform;
  static cl_device_id m_device;
  static cl_context m_context;
  static cl_mem m_mem0, m_mem1;
  static cl_program m_program;
  static cl_kernel m_kernel;
  static cl_command_queue m_cq0, m_cq1;
  static cl_event m_event;
};

TEST_GROUP_STATIC(track_object, const char *, m_offline_env);
TEST_GROUP_STATIC(track_object, cl_device_id, m_device);
TEST_GROUP_STATIC(track_object, cl_context, m_context);
TEST_GROUP_STATIC(track_object, cl_mem, m_mem0);
TEST_GROUP_STATIC(track_object, cl_mem, m_mem1);
TEST_GROUP_STATIC(track_object, cl_program, m_program);
TEST_GROUP_STATIC(track_object, cl_kernel, m_kernel);
TEST_GROUP_STATIC(track_object, cl_command_queue, m_cq0);
TEST_GROUP_STATIC(track_object, cl_command_queue, m_cq1);
TEST_GROUP_STATIC(track_object, cl_event, m_event);

MT_TEST(track_object, link) {
  load_platform();
  clTrackLiveObjectsIntelFPGA(0);
  clReportLiveObjectsIntelFPGA(0, 0, 0);
}

MT_TEST(track_object, some_leak) {
  live_info_vec_t live_vec;

  load_platform();
  clTrackLiveObjectsIntelFPGA(m_platform);

  clReportLiveObjectsIntelFPGA(m_platform, leak_notify, &live_vec);
  CHECK_EQUAL(0, live_vec.size());

  syncThreads();

  if (threadNum() == 0) {
    load_stuff();
  }

  syncThreads();

  clReportLiveObjectsIntelFPGA(m_platform, leak_notify, &live_vec);

  dump_leaks(live_vec);

  // Must include the hidden objects: mems, autodma queue, etc.
  CHECK_EQUAL(11, live_vec.size());

  CHECK_EQUAL(m_cq1, live_vec[0].object);
  CHECK_EQUAL(1, live_vec[0].refcount);
  CHECK_EQUAL(0, strcmp("cl_command_queue", live_vec[0].type_name));

  CHECK_EQUAL(m_cq0, live_vec[1].object);
  CHECK_EQUAL(1, live_vec[1].refcount);
  CHECK_EQUAL(0, strcmp("cl_command_queue", live_vec[1].type_name));

  CHECK_EQUAL(m_event, live_vec[2].object);
  CHECK_EQUAL(1, live_vec[2].refcount);
  CHECK_EQUAL(0, strcmp("cl_event", live_vec[2].type_name));

  CHECK_EQUAL(m_mem1, live_vec[3].object);
  CHECK_EQUAL(1, live_vec[3].refcount);
  CHECK_EQUAL(0, strcmp("cl_mem", live_vec[3].type_name));

  CHECK_EQUAL(m_mem0, live_vec[4].object);
  CHECK_EQUAL(1, live_vec[4].refcount);
  CHECK_EQUAL(0, strcmp("cl_mem", live_vec[4].type_name));

  CHECK_EQUAL(m_kernel, live_vec[5].object);
  CHECK_EQUAL(1, live_vec[5].refcount);
  CHECK_EQUAL(0, strcmp("cl_kernel", live_vec[5].type_name));

  CHECK_EQUAL(m_program, live_vec[6].object);
  CHECK_EQUAL(2, live_vec[6].refcount); // kernel references it.
  CHECK_EQUAL(0, strcmp("cl_program", live_vec[6].type_name));

  CHECK_EQUAL(m_context, live_vec[7].object);
  CHECK_EQUAL(9, live_vec[7].refcount);
  CHECK_EQUAL(0, strcmp("cl_context", live_vec[7].type_name));

  syncThreads();

  if (threadNum() == 0) {
    clSetUserEventStatus(m_event, CL_COMPLETE);
    clReleaseCommandQueue(m_cq0);
    clReleaseCommandQueue(m_cq1);
    clReleaseMemObject(m_mem0);
    clReleaseMemObject(m_mem1);
    clReleaseKernel(m_kernel);
    clReleaseEvent(m_event);
    clReleaseProgram(m_program);
    clReleaseContext(m_context);
  }

  syncThreads();

  live_vec.clear();
  clReportLiveObjectsIntelFPGA(m_platform, leak_notify, &live_vec);
  dump_leaks(live_vec);

  CHECK_EQUAL(0, live_vec.size());
}
